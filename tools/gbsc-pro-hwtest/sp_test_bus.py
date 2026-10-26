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

**MOST OF THE PAIRS ARE LIVE WIRES AND A SINGLE READ OF ONE MEANS NOTHING**,
which is what makes a bare across-draw table misleading: 37 of the 64 dither
WITHIN one draw, so their readings vary whether or not the phase did. Each pair
is therefore read several times per draw and reported as static or live, and
only a pair that is static within a draw can be compared across draws at all.

**64 CANDIDATES AGAINST A HANDFUL OF DRAWS WILL THROW UP AGREEMENTS BY CHANCE.**
Nothing here confirms one -- it reports the per-pair table so a candidate can be
re-run on draws it was not chosen from.

docs/investigations/the-adc-sampling-phase-is-a-lottery.md
"""

import argparse
import time

import numpy as np

from gbs_unit import read_fields, read_named, write_named
from hdmi_capture import device
from phase_curve import WITNESS, locate, report, route, shiftAgainst, sweep

# TEST_BUS_SEL's value for the sync processor, as Tv5725::TestBus names it.
SYNC_PROCESSOR = 0xA

MODULES = range(8)
SIGNALS = range(8)

# RD-5725-1.1's own table, for the report.
STAGE = {0: "none", 1: "hs_pol_det", 2: "hs_act_det", 3: "vs_pol_det",
         4: "vs_act_det", 5: "cs_sep", 6: "retiming", 7: "out proc"}


def busValue(host):
    """The test bus's own value. RD-5725-1.1 documents a 24-bit block at s0_2E
    and the register map carries two of its bytes, which is what is read."""
    both = read_fields(host, ["TEST_BUS_2E", "TEST_BUS_2F"])
    return (both["TEST_BUS_2F"] << 8) | both["TEST_BUS_2E"]


def scan(host, reads):
    """Every stage and signal of the sync processor, each read several times.

    The value per pair and whether it held still: a pair that dithers inside
    one draw is a live wire, and comparing one read of it against another
    draw's measures the sampling instant rather than the draw.
    """
    held = read_fields(host, ["TEST_BUS_SEL", "TEST_BUS_EN", "SP_TEST_EN",
                              "SP_TEST_MODULE", "SP_TEST_SIGNAL_SEL"])
    write_named(host, "TEST_BUS_SEL", SYNC_PROCESSOR)
    write_named(host, "TEST_BUS_EN", 1)
    write_named(host, "SP_TEST_EN", 1)
    values = {}
    try:
        for module in MODULES:
            write_named(host, "SP_TEST_MODULE", module)
            for signal in SIGNALS:
                write_named(host, "SP_TEST_SIGNAL_SEL", signal)
                seen = [busValue(host) for _ in range(reads)]
                values[(module, signal)] = (seen[0], len(set(seen)) == 1)
    finally:
        for name, was in held.items():
            write_named(host, name, was)
    return values


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
    parser.add_argument("--reads", type=int, default=4,
                        help="reads per pair per draw, which is what tells a "
                             "latched value from a live wire")
    args = parser.parse_args()

    held = read_fields(args.host, WITNESS)
    print("held:", held, flush=True)
    if held["DAC_RGBS_BYPS2DAC"] != 1:
        print("NOT in pass-through, where the phase bites -- /uc?x switches.")

    dev = device()
    box = locate(dev)
    print("wedge box", box, "(found once and held)", flush=True)

    draws = []
    try:
        for draw in range(args.draws):
            if draw:
                drawAgain(args.host)
            values = scan(args.host, args.reads)
            print("values,%d,%s" % (draw + 1, ",".join(
                "%d:%d=%d%s" % (module, signal, values[(module, signal)][0],
                                "" if values[(module, signal)][1] else "?")
                for module, signal in sorted(values))), flush=True)
            health = read_fields(args.host, ["PLLAD_MD",
                                             "STATUS_SYNC_PROC_HTOTAL"])
            if health["STATUS_SYNC_PROC_HTOTAL"] != health["PLLAD_MD"]:
                print("  the count left the divider, so test mode disturbed "
                      "the lock:", health, flush=True)
            curve = sweep(args.host, dev, box)
            previous = draws[-1][1] if draws else None
            report("draw %d" % (draw + 1), curve, csv=True, reference=previous)
            draws.append((values, curve))
    finally:
        route(args.host, "/phase?clear")

    print("\nshift per draw, against the one before it:", flush=True)
    shifts = [0]
    for (_, curve), (_, before) in zip(draws[1:], draws[:-1]):
        shifts.append(shiftAgainst(np.array(curve), np.array(before))[0])
    print(" ", shifts, flush=True)

    live = [pair for pair in sorted(draws[0][0])
            if not all(values[pair][1] for values, _ in draws)]
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
        series = [values[pair][0] for values, _ in draws]
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
