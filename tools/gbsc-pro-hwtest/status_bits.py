#!/usr/bin/env python3
"""What every segment 0 status bit reads, across states with a known verdict.

    python3 status_bits.py --host <ip>

**THE INDICATORS ARE NOT INTERCHANGEABLE AND SOME OF THEM LIE.** Three have been
caught separately: STATUS_SYNC_PROC_VSACT reports the sync path rather than the
lock, STATUS_IF_HT_OK reads 1 with HPERIOD_IF railed, and STATUS_IF_VT_OK has
been seen at 0 on a perfect lock. Each was found by accident, mid-diagnosis,
which is the expensive way. This drives the unit through states whose verdict is
already known and prints what every bit does in each, so a bit that carries no
information is visible as a row that never changes.

Segment 0 is read whole, one request per sample, so every bit in a row comes
from the same instant. A bit sampled at a different moment from its neighbour
invents disagreements -- the same reason the geometry tools read in one pass.

The clock question is the point of the divider states: a bit that only tells the
truth when the ADC PLL is locked is a bit nothing may key on before checking the
lock. STATUS_MISC_PLLAD_LOCK is the reference, because it is the one indicator
today's sweep showed changing exactly where the hardware does.

Every state is restored before the next, and the run ends by putting the unit
back and letting detection re-solve. The divider stays inside the span measured
as safe: driving it to 3008 took the unit off the network entirely.
"""

import argparse
import json
import os
import time

import gbs_unit
from bench_probe import Probe

# **LOOKED UP BY NAME, NEVER HAND-WRITTEN.** A wrong offset does not error, it
# returns a plausible number: written out by hand this file read HPERIOD_IF as
# 1455 where it holds 431, and nothing about 1455 looks wrong.
CATALOGUE = json.load(open(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "tv5725_registers.json")))

BIT_NAMES = [
    "STATUS_IF_HT_OK", "STATUS_IF_VT_OK", "STATUS_IF_HT_BAD", "STATUS_IF_VT_BAD",
    "STATUS_IF_NO_SYNC",
    "STATUS_MISC_PLLAD_LOCK", "STATUS_MISC_PLL648_LOCK",
    "STATUS_MISC_HSYNC", "STATUS_MISC_VSYNC", "STATUS_MISC_HBLK", "STATUS_MISC_VBLK",
    "STATUS_INT_SOG_BAD", "STATUS_INT_SOG_SW", "STATUS_INT_SOG_OK",
    "STATUS_INT_INP_SW", "STATUS_INT_INP_NO_SYNC", "STATUS_INT_INP_HSYNC",
    "STATUS_INT_INP_VSYNC", "STATUS_INT_INP_CSYNC",
    "STATUS_VDS_OUT_VSYNC", "STATUS_VDS_OUT_HSYNC", "STATUS_VDS_OUT_BLANK",
    "STATUS_SYNC_PROC_HSACT", "STATUS_SYNC_PROC_VSACT",
    "STATUS_SYNC_PROC_HSPOL", "STATUS_SYNC_PROC_VSPOL",
]

COUNTER_NAMES = [
    "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HTOTAL",
    "HPERIOD_IF", "VPERIOD_IF", "STATUS_SYNC_PROC_HLOW_LEN",
]


def segment0(name):
    d = CATALOGUE[name]
    assert d["seg"] == 0, f"{name} is not in segment 0"
    return (name.replace("STATUS_", "").replace("SYNC_PROC_", "SP_"),
            d["reg"], d["off"], d["width"])


BITS = [segment0(n) for n in BIT_NAMES]
COUNTERS = [segment0(n) for n in COUNTER_NAMES]


def spec(name):
    d = CATALOGUE[name]
    return (name, d["seg"], d["reg"], d["off"], d["width"])


PLLAD_MD = spec("PLLAD_MD")
PLLAD_LAT = spec("PLLAD_LAT")
SP_PRE_COAST = spec("SP_PRE_COAST")
SP_POST_COAST = spec("SP_POST_COAST")
SP_DLT_REG = spec("SP_DLT_REG")

MODE_DETECT = (1, 0x60, 0x84)          # the Mode Detect section, s1 0x60..0x83
KNOWN_GOOD_DIVIDER = 2250
SAFE_DIVIDER_MAX = 2900                # 3008 took the unit off the network


def read_register(host, segment, register, attempts=10):
    """A dropped read must never reach a restore path."""
    for _ in range(attempts):
        value = gbs_unit.read_reg(host, segment, register)
        if value is not None:
            return value
        time.sleep(0.4)
    return None


def sample(host, count=40, interval=0.05):
    """Every bit, `count` times, from whole-segment reads.

    Returns name -> sorted list of the distinct values seen. One value means the
    bit held still; more than one means it wobbles, and a wobbling indicator is
    not one anything may branch on.
    """
    seen = {name: set() for name, *_ in BITS + COUNTERS}
    for _ in range(count):
        registers = gbs_unit.read_segment(host, 0)
        if registers:
            for name, reg, off, width in BITS + COUNTERS:
                value = gbs_unit.field_from(registers, reg, off, width)
                if value is not None:
                    seen[name].add(value)
        time.sleep(interval)
    return {name: sorted(values) for name, values in seen.items()}


def show(values):
    if not values:
        return "-"
    if len(values) == 1:
        return str(values[0])
    return f"{values[0]}..{values[-1]}~"       # ~ means it moved while sampled


def restore(host, probe, latch):
    """Unfreeze FIRST: with automation running the firmware re-solves the
    divider itself, so even a failed write leaves the unit recovering rather
    than parked at whatever the sweep last wrote."""
    for _ in range(10):
        if gbs_unit.get(host, "/freeze?on=0", timeout=3)[0] == 200:
            break
        time.sleep(1)
    try:
        latch(KNOWN_GOOD_DIVIDER)
        gbs_unit.recover_lock(host)
    except Exception as e:                      # noqa: BLE001 - must not raise
        print("could not put the unit back:", e, flush=True)


def report(results, axis):
    labels = list(results)
    width = max(len(n) for n, *_ in BITS + COUNTERS) + 1
    cell = max(12, max(len(l) for l in labels) + 2)
    print(f"\n{axis}" + " " * (width - len(axis))
          + "".join(f"{l:>{cell}}" for l in labels))
    for name, *_ in BITS + COUNTERS:
        row = "".join(f"{show(results[l][name]):>{cell}}" for l in labels)
        varies = len({show(results[l][name]) for l in labels}) > 1
        print(f"{name:{width}}{row}{'   <- tracks it' if varies else ''}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--samples", type=int, default=40)
    parser.add_argument(
        "--sweep", metavar="LOW,HIGH,STEP",
        help="instead of the state survey, walk PLLAD_MD and report which "
             "fields track it. Stays at or below the divider measured safe: "
             f"{SAFE_DIVIDER_MAX}.")
    args = parser.parse_args()
    host = args.host

    probe = Probe(lambda s, r: read_register(host, s, r),
                  lambda s, r, v: gbs_unit.write_reg(host, s, r, v))

    def latch(divider):
        probe.write_field(PLLAD_MD, min(divider, SAFE_DIVIDER_MAX))
        probe.write_field(PLLAD_LAT, 0)
        probe.write_field(PLLAD_LAT, 1)

    results = {}

    def record(label):
        results[label] = sample(host, args.samples)
        print(f"  sampled {label}", flush=True)

    if args.sweep:
        low, high, step = (int(v) for v in args.sweep.split(","))
        high = min(high, SAFE_DIVIDER_MAX)
        try:
            gbs_unit.recover_lock(host)
            time.sleep(6)
            gbs_unit.get(host, "/freeze?on=1")
            time.sleep(1)
            for divider in range(low, high + 1, step):
                latch(divider)
                time.sleep(1.5)
                results[str(divider)] = sample(host, args.samples)
                lock = results[str(divider)]["MISC_PLLAD_LOCK"]
                print(f"  MD={divider:5} f={divider * 15575 / 1e6:6.2f} MHz "
                      f"lock={show(lock)}", flush=True)
        finally:
            restore(host, probe, latch)
        report(results, "PLLAD_MD")
        return

    try:
        gbs_unit.recover_lock(host)
        time.sleep(8)
        record("locked")

        gbs_unit.get(host, "/freeze?on=1")
        time.sleep(1)
        for divider, label in ((1600, "md1600-unlocked"), (700, "md700-halfcount")):
            latch(divider)
            time.sleep(2)
            record(label)
        latch(KNOWN_GOOD_DIVIDER)
        time.sleep(2)

        saved = [read_register(host, MODE_DETECT[0], r)
                 for r in range(MODE_DETECT[1], MODE_DETECT[2])]
        if None not in saved:
            for r in range(MODE_DETECT[1], MODE_DETECT[2]):
                gbs_unit.write_reg(host, MODE_DETECT[0], r, 0)
            time.sleep(2)
            record("mode-detect-zeroed")
            for offset, value in enumerate(saved):
                gbs_unit.write_reg(host, MODE_DETECT[0], MODE_DETECT[1] + offset, value)
            time.sleep(2)

        coast = [(spec, probe.read_field(spec))
                 for spec in (SP_PRE_COAST, SP_POST_COAST, SP_DLT_REG)]
        if all(v is not None for _, v in coast):
            for spec, _ in coast:
                probe.write_field(spec, 0)
            time.sleep(2)
            record("coast-zeroed")
            for spec, value in coast:
                probe.write_field(spec, value)
            time.sleep(2)

        gbs_unit.get(host, "/freeze?on=0")
        time.sleep(2)
        gbs_unit.get(host, "/sc?%29")           # the real fault, not a hand-set divider
        time.sleep(25)
        record("trapped-by-preset-load")
    finally:
        restore(host, probe, latch)

    report(results, "state")


if __name__ == "__main__":
    main()
