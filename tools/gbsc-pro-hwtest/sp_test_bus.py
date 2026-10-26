#!/usr/bin/env python3
"""Does any sync-processor test-bus signal carry the sampling phase's draw?

    python3 sp_test_bus.py --host 192.168.88.108 --draws 8

The draw is the sync processor's reset: `SFTRST_SYNC_RSTZ` is the only pulse of
eleven that moves the ADC phase curve, and it moves it differently every time.
`SP_TEST_MODULE` s5_63[1:3] exposes eight stages of that block by name --
**6 is the retiming module**, which generates the pulse the ADC PLL locks to --
and `SP_TEST_SIGNAL_SEL` eight signals of each. 64 pairs, read as the test
bus's own value rather than counted as edges.

**WHAT MAKES A SLOW READ ADEQUATE HERE.** The level path was closed for scoring
a phase because an HTTP read samples the whole frame at 43 Hz and the wedge is
0.7% of it. This asks a different question: the draw's offset is STATIC for the
life of a draw, changing only on the next reset, so a value that encodes it is
not moving while it is read.

Per draw it reads all 64 pairs, measures the phase curve, and reports the curve's
circular shift against the previous draw. A pair whose value tracks that shift
across draws is a predictor; one that is constant, or that moves without the
curve moving, is not.

Each draw's values are printed as they are taken, because a draw costs about
five minutes -- 64 pairs of deferred read-modify-writes plus a 32-point sweep --
so a run reporting only at the end yields nothing from an interrupted one.

**MOST OF THE PAIRS ARE LIVE WIRES AND A SINGLE READ OF ONE MEANS NOTHING.**
37 of the 64 dither WITHIN one draw, so a read of one samples the wire's state
and varies whether or not the phase did -- which makes an across-draw table of
single reads read as 44 varying signals when 37 of them are the read's own
timing. So the sampling is done ON THE DEVICE, by `/sptestbus`, which takes a
distribution per pair from `loop()` and reports its count, extremes, mean and
value table. An HTTP read answers at tens of hertz at best and cannot take one.

**64 CANDIDATES AGAINST A HANDFUL OF DRAWS WILL THROW UP AGREEMENTS BY CHANCE.**
Nothing here confirms one -- it reports the per-pair table so a candidate can be
re-run on draws it was not chosen from.

docs/investigations/the-adc-sampling-phase-is-a-lottery.md
"""

import argparse
import sys
import time

import numpy as np

from gbs_unit import Console, get, read_fields, read_named, write_named
from hdmi_capture import device
from phase_curve import WITNESS, locate, report, route, shiftAgainst, sweep

# TEST_BUS_SEL's value for the sync processor, as Tv5725::TestBus names it.
SYNC_PROCESSOR = 0xA

MODULES = range(8)
SIGNALS = range(8)

# How long delivery has to stop before the scan is taken as finished.
QuietEnough = 4.0

# RD-5725-1.1's own table, for the report.
STAGE = {0: "none", 1: "hs_pol_det", 2: "hs_act_det", 3: "vs_pol_det",
         4: "vs_act_det", 5: "cs_sep", 6: "retiming", 7: "out proc"}


def pairFrom(line):
    """One `sptb,` row as ((module, signal), distribution), or None.

    The distribution is (count, low, high, mean, distinct, overflowed). A row
    with fewer fields than that is a truncated console line rather than a
    reading, and the console drops bursts.
    """
    if not line.startswith("sptb,") or line.startswith("sptb,header"):
        return None
    part = line.split(",")
    if len(part) < 9 or not part[1].isdigit():
        return None
    return ((int(part[1]), int(part[2])),
            (int(part[3]), int(part[4]), int(part[5]), int(part[6]),
             int(part[7]), part[8] == "1"))


def scan(host, console, reads):
    """Every stage and signal, as the distribution the device sampled.

    Per pair: (count, low, high, mean, distinct, overflowed). A pair whose
    distinct count is 1 held still across the whole run; anything above that is
    a live wire, and comparing one read of it between draws measures the read's
    timing rather than the draw.
    """
    console.drain()
    status, body = get(host, "/sptestbus?reads=%d" % reads)
    if status == 404 or (status == 500 and not body.strip()):
        sys.exit("no /sptestbus on this firmware; it is behind GBS_DEBUG")

    # JUDGED BY QUIESCENCE, NOT BY THE done MARKER OR A FULL SET. The console
    # drops bursts and 65 lines in quick succession is one, so neither the
    # marker nor the 64th pair is guaranteed to arrive -- waiting on either
    # stalls a run whose scan has already finished. Delivery stopping is the
    # signal that survives a drop.
    wanted = len(MODULES) * len(SIGNALS)
    pairs, deadline, quiet = {}, time.time() + 300, 0.0
    while time.time() < deadline and len(pairs) < wanted:
        arrived = len(pairs)
        for line in console.collect(0.3):
            row = pairFrom(line)
            if row:
                pairs[row[0]] = row[1]
        console.drain()
        quiet = 0.0 if len(pairs) > arrived else quiet + 0.3
        if pairs and quiet >= QuietEnough:
            break
    if not pairs:
        sys.exit("the scan delivered nothing -- /sptestbus is behind GBS_DEBUG, "
                 "and the console is deaf for ~15 s after a boot")
    if len(pairs) < len(MODULES) * len(SIGNALS):
        print("  only %d of 64 pairs arrived, the rest dropped by the console"
              % len(pairs), flush=True)
    return pairs


def drawAgain(host):
    """Reset the sync processor, which is what re-rolls the phase."""
    was = read_named(host, "SFTRST_SYNC_RSTZ")
    write_named(host, "SFTRST_SYNC_RSTZ", 0 if was else 1)
    write_named(host, "SFTRST_SYNC_RSTZ", was)
    # The source is briefly absent across the reset, and a sweep started inside
    # that window scores unlit frames as 0.
    time.sleep(10.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--draws", type=int, default=8)
    parser.add_argument("--reads", type=int, default=2000,
                        help="samples per pair per draw, taken on the device")
    args = parser.parse_args()

    held = read_fields(args.host, WITNESS)
    print("held:", held, flush=True)
    if held["DAC_RGBS_BYPS2DAC"] != 1:
        print("NOT in pass-through, where the phase bites -- /uc?x switches.")

    console = Console(args.host)
    dev = device()
    box = locate(dev)
    print("wedge box", box, "(found once and held)", flush=True)

    draws = []
    try:
        for draw in range(args.draws):
            if draw:
                drawAgain(args.host)
            values = scan(args.host, console, args.reads)
            print("values,%d,%s" % (draw + 1, ",".join(
                "%d/%d=%d..%d~%d(%d)" % ((module, signal)
                                         + values[(module, signal)][1:5])
                for module, signal in sorted(values))), flush=True)
            # A register read is deferred to loop(), which the scan has just
            # held for the length of the sampling, so the first one after it
            # can time out and answer nothing. That is the loop being busy
            # rather than a sick unit.
            health = read_fields(args.host, ["PLLAD_MD",
                                             "STATUS_SYNC_PROC_HTOTAL"])
            if health is None:
                time.sleep(2.0)
                health = read_fields(args.host, ["PLLAD_MD",
                                                 "STATUS_SYNC_PROC_HTOTAL"])
            if health is None:
                print("  the unit did not answer a register read after the "
                      "scan", flush=True)
            elif health["STATUS_SYNC_PROC_HTOTAL"] != health["PLLAD_MD"]:
                print("  the count left the divider, so test mode disturbed "
                      "the lock:", health, flush=True)
            curve = sweep(args.host, dev, box)
            previous = draws[-1][1] if draws else None
            report("draw %d" % (draw + 1), curve, csv=True, reference=previous)
            draws.append((values, curve))
    finally:
        route(args.host, "/phase?clear")
        console.close()

    print("\nshift per draw, against the one before it:", flush=True)
    shifts = [0]
    for (_, curve), (_, before) in zip(draws[1:], draws[:-1]):
        shifts.append(shiftAgainst(np.array(curve), np.array(before))[0])
    print(" ", shifts, flush=True)

    live = [pair for pair in sorted(draws[0][0])
            if not all(values[pair][4] == 1 for values, _ in draws)]
    print("\n%d of 64 pairs dither WITHIN a draw, so a read of one samples it "
          "asynchronously and says nothing about the draw:" % len(live),
          flush=True)
    print(" ", " ".join("%d/%d" % pair for pair in live), flush=True)

    print("\nof the %d that hold still, those whose value moved between draws "
          "-- the only candidates:" % (64 - len(live)), flush=True)
    moved = False
    for pair in sorted(draws[0][0]):
        if pair in live:
            continue
        series = [values[pair][1] for values, _ in draws]
        if len(set(series)) > 1:
            moved = True
            print("  module %d (%-10s) signal %d: %s"
                  % (pair[0], STAGE[pair[0]], pair[1], series), flush=True)
    if not moved:
        print("  none -- every pair that holds still reads the same value at "
              "every draw, so none of them carries the offset")
    print("\nA pair that moved in the same draws as most of the others moved "
          "in is the bus in another state across that scan, not a signal.")

    print("\nwitnesses now:", read_fields(args.host, WITNESS))


if __name__ == "__main__":
    main()
