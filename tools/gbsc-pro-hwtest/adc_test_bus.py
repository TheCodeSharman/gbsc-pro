#!/usr/bin/env python3
"""Does the ADC's own test bus reach the chip's test bus?

    python3 adc_test_bus.py --host 192.168.88.108
    python3 adc_test_bus.py --host … --phases 0,8,16,24

`ADC_TA_EN` s5_05[0] gates a test bus inside the ADC and `ADC_TA_CTRL` s5_05[1:4]
selects among sixteen, documented only as "ADC test bus control bit". The
59-combination sweep that closed transition counting walked `TEST_BUS_SEL` and
`IF_TEST_SEL`, neither of which is this one, and the ADC's own domain is where
the sampling phase lives.

So the prior question is whether any `ADC_TA_CTRL` setting changes what any
`TEST_BUS_SEL` carries at all. A scan whose every leg sits inside the control's
own envelope closes the candidate without a search being designed on top of it.

**A SINGLE CONTROL SWEEP CANNOT SCORE ONE.** Several selectors carry live
analog-derived signals -- the three channel levels among them -- whose
transition count moves by hundreds between two windows on an untouched unit, so
a leg differing from one control reading is the ordinary swing. The envelope is
taken from repeated controls first, interleaved with the legs so a drift belongs
to the envelope too, and a candidate is then confirmed against a control taken
beside it.

**ENABLING IT PUTS THE ADC IN TEST MODE, SO THE PICTURE IS AT RISK.** The
witnesses are read per leg and `/sc?~` is the recovery.

With `--phases`, a confirmed selector is re-read at those `PA_ADC_S` values: a
signal that does not move with the sampling phase is not an instrument for it
however alive it is.
"""

import argparse
import sys
import time

from gbs_unit import Console, get, read_fields, read_named, write_named

WINDOW_MS = 25
CONTROLS = 5
CONFIRMS = 3

# The narrowest spread a selector is credited with, per quantity. A selector the
# control happened to read identically every time has a range of zero width,
# against which any difference at all reads as infinitely far outside -- and the
# low-transition selectors do move: selector 0 reads 2 transitions at a duty of
# 0.04 and 4 at 0.08 between two windows with nothing touched.
TRANSITION_FLOOR = 4.0
DUTY_FLOOR = 0.05
WITNESS = ["PLLAD_MD", "STATUS_SYNC_PROC_HTOTAL", "STATUS_SYNC_PROC_VTOTAL",
           "PA_ADC_S"]


def sweep(host, console, window_ms):
    """Transitions and the share of samples high, per TEST_BUS_SEL, in one pass."""
    console.drain()
    status, body = get(host, f"/testbus?ms={window_ms}")
    if status == 404 or (status == 500 and not body.strip()):
        sys.exit("no /testbus on this firmware; it is behind GBS_DEBUG")

    rows, done, deadline = {}, False, time.time() + 60
    while time.time() < deadline and not done:
        for line in console.collect(0.3):
            if line == "tb,done":
                done = True
            elif line.startswith("tb,") and not line.startswith("tb,header"):
                parts = line.split(",")
                if len(parts) >= 5:
                    transitions, high, spins = (int(parts[2]), int(parts[3]),
                                                int(parts[4]))
                    rows[int(parts[1])] = (transitions,
                                           high / spins if spins else 0.0)
        console.drain()
    if not done:
        sys.exit("the sweep never said tb,done -- the console drops bursts, so "
                 "take one sweep at a time rather than widening the window")
    return rows


class Envelope:
    """What each selector reads with the ADC left alone, over several windows."""

    def __init__(self):
        self.seen = {}

    def add(self, rows):
        for sel, reading in rows.items():
            self.seen.setdefault(sel, []).append(reading)

    def outside(self, sel, reading):
        """How far outside the control's own range this reading sits, in the
        range's own width, for each of the two quantities. A selector the
        control read identically every time has no width, so a floor stands in
        for one."""
        seen = self.seen.get(sel)
        if not seen:
            return 0.0
        out = []
        for index, floor in ((0, TRANSITION_FLOOR), (1, DUTY_FLOOR)):
            values = [reading[index] for reading in seen]
            low, high = min(values), max(values)
            width = max(high - low, floor)
            out.append(max(low - reading[index], reading[index] - high) / width)
        return max(out)


BAR = 1.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--window-ms", type=int, default=WINDOW_MS)
    parser.add_argument("--phases", default=None,
                        help="PA_ADC_S values to re-read a confirmed selector at")
    args = parser.parse_args()

    print("held:", read_fields(args.host, WITNESS), flush=True)
    keep_en = read_named(args.host, "ADC_TA_EN")
    keep_ctrl = read_named(args.host, "ADC_TA_CTRL")

    console = Console(args.host)
    envelope = Envelope()
    started = time.time()
    try:
        for _ in range(CONTROLS):
            envelope.add(sweep(args.host, console, args.window_ms))
        print("envelope over %d controls, %.0fs; the widest swings are %s"
              % (CONTROLS, time.time() - started, ", ".join(
                  "%d:%d" % (sel, max(r[0] for r in seen)
                             - min(r[0] for r in seen))
                  for sel, seen in sorted(envelope.seen.items(),
                                          key=lambda kv: -(max(r[0] for r in kv[1])
                                                           - min(r[0] for r in kv[1])))[:5])),
              flush=True)

        candidates = []
        write_named(args.host, "ADC_TA_EN", 1)
        for ctrl in range(16):
            write_named(args.host, "ADC_TA_CTRL", ctrl)
            time.sleep(0.3)
            rows = sweep(args.host, console, args.window_ms)
            out = [(sel, envelope.outside(sel, rows[sel])) for sel in sorted(rows)]
            live = [(sel, score) for sel, score in out if score > BAR]
            print("TA_CTRL=%-2d %s" % (ctrl, " ".join(
                "%d:%d/%.2f out %.1f" % (sel, rows[sel][0], rows[sel][1], score)
                for sel, score in live) or "inside the envelope"), flush=True)
            candidates += [(ctrl, sel) for sel, _ in live]

            write_named(args.host, "ADC_TA_EN", 0)
            envelope.add(sweep(args.host, console, args.window_ms))
            write_named(args.host, "ADC_TA_EN", 1)

            witness = read_fields(args.host, WITNESS)
            if witness["STATUS_SYNC_PROC_HTOTAL"] != witness["PLLAD_MD"]:
                print("   the count left the divider:", witness, flush=True)

        if not candidates:
            print("\nNothing the ADC's test bus carries reaches TEST_BUS_SEL, "
                  "so it is not an instrument for the sampling phase.")
            return

        print("\nconfirming %d candidate(s) against the whole envelope"
              % len(candidates), flush=True)
        confirmed = []
        for ctrl, sel in candidates:
            write_named(args.host, "ADC_TA_CTRL", ctrl)
            scores = []
            for _ in range(CONFIRMS):
                rows = sweep(args.host, console, args.window_ms)
                scores.append(envelope.outside(sel, rows[sel]))
            print("TA_CTRL=%-2d sel %-2d out %s" % (ctrl, sel, " ".join(
                "%.1f" % score for score in scores)), flush=True)
            if min(scores) > BAR:
                confirmed.append((ctrl, sel))

        if not confirmed:
            print("\nEvery candidate was the ordinary swing. The ADC's test "
                  "bus does not reach TEST_BUS_SEL.")
            return

        print("\nconfirmed:", confirmed, flush=True)
        if not args.phases:
            print("re-run with --phases to ask whether any of them moves with "
                  "the sampling phase")
            return

        phases = [int(v) for v in args.phases.split(",")]
        for ctrl, sel in confirmed:
            write_named(args.host, "ADC_TA_CTRL", ctrl)
            for phase in phases:
                get(args.host, f"/phase?adc={phase}")
                time.sleep(0.8)
                rows = sweep(args.host, console, args.window_ms)
                print("TA_CTRL=%-2d sel %-2d PA_ADC_S=%-2d %d/%.3f"
                      % ((ctrl, sel, phase) + rows[sel]), flush=True)
    finally:
        get(args.host, "/phase?clear")
        write_named(args.host, "ADC_TA_CTRL", keep_ctrl)
        write_named(args.host, "ADC_TA_EN", keep_en)
        console.close()
        print("\nwitnesses now:", read_fields(args.host, WITNESS))


if __name__ == "__main__":
    main()
