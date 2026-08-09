#!/usr/bin/env python3
"""Check every position register against the raster it is a position in.

    python3 tools/gbsc-pro-hwtest/inrange.py --host 192.168.88.108

A zigzag on every vertical edge -- one pixel of horizontal displacement,
alternating line to line -- is `IF_HB_ST` holding **1347 on a 1277 unit line**.
Finding it took a nine round bisect against a known-good snapshot, because
nothing in the tree could say "that register is 70 units past the end of the
line".

docs/tv5725-chip.md states the consequence -- "an edge at or beyond IF_HSYNC_RST
is out of bounds, blanking then never starts, the line buffer wraps" -- and a
fact in prose is not a check. The same run found `IF_LINE_SP` at 1341 in a state
that had just been confirmed clean by eye, so an out-of-range register does not
have to be visible to be there.

**Each axis is measured against its own raster**, which is the thing a checker
like this gets wrong and thereby becomes useless:

    IF_ horizontal    IF_HSYNC_RST + 1        IF units
    IF_ vertical      2 x (VTOTAL + 1)        HALF-LINES, not lines
    VDS_ horizontal   VDS_HSYNC_RST + 1       real output pixels
    VDS_ vertical     VDS_VSYNC_RST + 1       real output lines
    SP_               PLLAD_MD                ADC samples, NOT IF units

**THE SP_ ROW IS ADC SAMPLES, NOT IF UNITS.** Three sync processor registers
hold values above the IF line, and HLOW_LEN only matches the source's mode file
read as ADC samples. docs/scaler-geometry-model.md.

The limit is exclusive and one short of the total: a position may reach
`total - 1`. That value is the wrap point, and a window written onto it does not
clamp, it rolls.
"""

import argparse
import collections
import json
import os
import sys

Finding = collections.namedtuple("Finding", "name value limit because")

# **IF_LINE_SP IS DELIBERATELY NOT HERE.** It reads above IF_HSYNC_RST by design:
# it is the stop of the input formatter's progressive line window, IF_LINE_ST
# plus one whole line, so it rolls. A stop measured from a start is not a
# position within the raster, and checking it as one reports a fault that does
# not exist.
#
# Each group: the register holding the total, how to turn it into a limit, and
# the positions measured against it.
GROUPS = [
    ("IF_HSYNC_RST", lambda t: t + 1, "IF_HSYNC_RST + 1", [
        "IF_HB_ST", "IF_HB_SP", "IF_HB_ST1", "IF_HB_SP1", "IF_HB_ST2", "IF_HB_SP2",
        "IF_LINE_ST", "IF_HBIN_ST", "IF_HBIN_SP"]),
    ("STATUS_SYNC_PROC_VTOTAL", lambda t: 2 * (t + 1), "2 x (VTOTAL + 1)", [
        "IF_VB_ST", "IF_VB_SP", "IF_INI_ST"]),
    ("VDS_HSYNC_RST", lambda t: t + 1, "VDS_HSYNC_RST + 1", [
        "VDS_HB_ST", "VDS_HB_SP", "VDS_DIS_HB_ST", "VDS_DIS_HB_SP",
        "VDS_HS_ST", "VDS_HS_SP"]),
    ("VDS_VSYNC_RST", lambda t: t + 1, "VDS_VSYNC_RST + 1", [
        "VDS_VB_ST", "VDS_VB_SP", "VDS_DIS_VB_ST", "VDS_DIS_VB_SP",
        "VDS_VS_ST", "VDS_VS_SP"]),
    ("PLLAD_MD", lambda t: t, "PLLAD_MD", [
        "SP_RT_HS_ST", "SP_RT_HS_SP", "SP_CS_CLP_ST", "SP_CS_CLP_SP",
        "SP_H_CST_ST", "SP_H_CST_SP", "SP_CS_HS_ST", "SP_CS_HS_SP"]),
]

# Below this a raster total is not a small raster, it is a unit that is not
# running: in RGBHV bypass the VDS is out of the video path entirely and
# VDS_?SYNC_RST reads 0, which would otherwise report every VDS position as a
# fault. See docs/rgbhv-bypass-trap.md.
RASTER_MIN = 64


def check(read):
    """Findings for every position register outside its raster.

    `read(name)` returns the field's value, or None if it could not be read. A
    dropped read is not a finding -- reporting one would send the next session
    after a register that is fine.
    """
    findings = []
    for total_name, to_limit, because, positions in GROUPS:
        total = read(total_name)
        if total is None or total < RASTER_MIN:
            continue
        limit = to_limit(total)
        for name in positions:
            value = read(name)
            if value is not None and value >= limit:
                findings.append(Finding(name, value, limit,
                                        f"{because} = {limit}"))
    return findings


def report(findings):
    if not findings:
        return "  every position register is inside its raster"
    out = []
    for f in findings:
        out.append(f"  {f.name:16s} {f.value:5d}   past {f.because} "
                   f"by {f.value - f.limit + 1}")
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True, help="the unit, e.g. 192.168.88.108")
    args = parser.parse_args(argv)

    import bench_probe
    from gbs_unit import read_reg

    here = os.path.dirname(os.path.abspath(__file__))
    regs = json.load(open(os.path.join(here, "tv5725_registers.json")))
    probe = bench_probe.Probe(lambda s, r: read_reg(args.host, s, r),
                              lambda s, r, v: None)

    cache = {}

    def read(name):
        if name not in cache:
            entry = regs.get(name)
            if entry is None:
                cache[name] = None
            else:
                cache[name] = probe.read_field(
                    (name, entry["seg"], entry["reg"], entry["off"],
                     entry["width"]))
        return cache[name]

    findings = check(read)
    print(f"\n  {args.host}\n")
    print(report(findings))
    print()
    return 1 if findings else 0


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.exit(main())
