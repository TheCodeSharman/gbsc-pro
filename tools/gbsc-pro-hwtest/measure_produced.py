#!/usr/bin/env python3
"""Measure how many output units the scaler actually writes, by eye.

    python3 tools/gbsc-pro-hwtest/measure_produced.py \
        --host 192.168.88.108 --axis h

`produced` is the one quantity in the geometry model that nothing on the chip
reports. The only way to read it is to move the display window's far edge until
the band of unwritten memory just disappears: that value is
`origin + floor(produced)`, and the band is visible because the scaler leaves
frame buffer beyond the picture untouched.

This drives that measurement. For each (capture, scale) it sets every register
the point needs, parks the display edge above the answer, and lets you creep it
down by eye until the band goes -- then records the value and moves on. The
creeping is yours; the bookkeeping, the register writes and the fit are not.

WHY IT EXISTS. Three separate models of the loss looked confirmed on 2026-08-05
and were each refuted by the next measurement, because each rested on two points
-- and two points lie on a line by construction, so they cannot disconfirm one.
The matrix below spans several magnifications per axis for exactly that reason,
and the fit prints residuals so the model can fail visibly.

The unit must be FROZEN (`curl 'http://<ip>/freeze?on=1'`), or applyPresets will
rewrite the registers underneath the measurement. Leave regpanel's pads alone
while this runs -- a pad press recomputes the display window and destroys the
probe.
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geometry_math

# (capture, scale) per axis. Chosen so every product fits inside the raster from
# the measured corner, and so each axis spans at least four magnifications --
# three is the fewest that can disconfirm a straight line, and four leaves one
# to spare when a reading is ambiguous.
MATRIX = {
    "h": [(798, 1023), (798, 800), (798, 640),
          (400, 1023), (400, 512), (200, 320)],
    "v": [(511, 1023), (511, 700), (511, 512),
          (300, 1023), (300, 512), (200, 300)],
}

FIELDS = {
    "h": dict(cap_sp=("IF_HB_SP2", 1, 0x1A, 0, 11),
              cap_st=("IF_HB_ST2", 1, 0x18, 0, 11),
              scale=("VDS_HSCALE", 3, 0x16, 0, 10),
              dis_sp=("VDS_DIS_HB_SP", 3, 0x11, 4, 12),
              dis_st=("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
              win_sp=("VDS_HB_SP", 3, 0x05, 4, 12),
              win_st=("VDS_HB_ST", 3, 0x04, 0, 12),
              rst=("VDS_HSYNC_RST", 3, 0x01, 0, 12),
              corner=geometry_math.CORNER_H, base=264, unit="px"),
    "v": dict(cap_sp=("IF_VB_SP", 1, 0x1E, 0, 11),
              cap_st=("IF_VB_ST", 1, 0x1C, 0, 11),
              scale=("VDS_VSCALE", 3, 0x17, 4, 10),
              dis_sp=("VDS_DIS_VB_SP", 3, 0x14, 4, 11),
              dis_st=("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
              win_sp=("VDS_VB_SP", 3, 0x08, 4, 11),
              win_st=("VDS_VB_ST", 3, 0x07, 0, 11),
              rst=("VDS_VSYNC_RST", 3, 0x02, 4, 11),
              corner=geometry_math.CORNER_V, base=30, unit="lines"),
}

HOST = None


def _get(path):
    try:
        with urllib.request.urlopen(f"http://{HOST}{path}", timeout=6) as r:
            return r.read().decode("utf-8", "replace")
    except (urllib.error.URLError, OSError):
        return None


def read_reg(segment, register):
    body = _get(f"/getreg?s={segment}&r=0x{register:02x}")
    try:
        return int(json.loads(body)["value"], 16)
    except (TypeError, ValueError, KeyError):
        return None


def write_reg(segment, register, value):
    return _get(f"/setreg?s={segment}&r=0x{register:02x}&v=0x{value:02x}") is not None


def _span(offset, width):
    return (offset + width + 7) // 8


def read_field(spec):
    _, segment, register, offset, width = spec
    raw = 0
    for i in range(_span(offset, width)):
        byte = read_reg(segment, register + i)
        if byte is None:
            return None
        raw |= byte << (8 * i)
    return (raw >> offset) & ((1 << width) - 1)


def write_field(spec, value):
    """Read-modify-write, so neighbouring bits in the same register survive."""
    _, segment, register, offset, width = spec
    value = max(0, min(int(value), (1 << width) - 1))
    span = _span(offset, width)
    raw = 0
    for i in range(span):
        byte = read_reg(segment, register + i)
        if byte is None:
            return False
        raw |= byte << (8 * i)
    mask = ((1 << width) - 1) << offset
    raw = (raw & ~mask) | ((value << offset) & mask)
    return all(write_reg(segment, register + i, (raw >> (8 * i)) & 0xFF)
               for i in range(span))


def ask(question):
    """y / n, or q to give up on this point."""
    while True:
        answer = input(f"{question} [y/n/q] ").strip().lower()
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        if answer in ("q", "quit"):
            return None


CREEP_HELP = """    Enter  down one step      u  up one step
    s N    set the step         N  jump straight to N
    ok     this is the edge     q  skip this point"""


def creep_edge(field, start, step=4):
    """Walk the display edge down until the band goes, one step at a time.

    Deliberately not a bisection. Bisecting asks fewer questions, but each one
    lands on an unfamiliar value with no context; creeping lets the band shrink
    under your eye, which is what every successful measurement in this project
    has actually used. The last value the eye rejects and the first it accepts
    are one step apart, so the step size is the measurement's resolution -- keep
    it at 1 for the final approach.
    """
    value = start
    write_field(field, value)
    print(CREEP_HELP)
    while True:
        answer = input(f"    {field[0]} = {value:5}  step {step} > ").strip().lower()
        if answer == "ok":
            return value
        if answer in ("q", "quit"):
            return None
        if answer == "u":
            value += step
        elif answer.startswith("s "):
            try:
                step = max(1, int(answer[2:]))
            except ValueError:
                print("    ? step wants a number")
            continue
        elif answer == "":
            value -= step
        else:
            try:
                value = int(answer)
            except ValueError:
                print(CREEP_HELP)
                continue
        write_field(field, value)


def measure(axis_key, capture, scale):
    axis = FIELDS[axis_key]
    sp = axis["base"]
    write_field(axis["cap_st"], sp + capture)
    write_field(axis["cap_sp"], sp)
    write_field(axis["scale"], scale)

    corner = axis["corner"]
    write_field(axis["dis_sp"], corner)
    raster = read_field(axis["rst"]) + 1
    write_field(axis["win_sp"], corner - (geometry_math.ORIGIN_OFFSET_H_SCALED
                                          if axis_key == "h"
                                          else geometry_math.ORIGIN_OFFSET_V))
    write_field(axis["win_st"], raster - 2)

    # The old formula is an upper bound on what can be written -- both losses
    # only ever subtract -- so starting there guarantees the band is showing and
    # the creep only ever goes down.
    ceiling = min(corner + int(capture * 1024 / scale) + 2, raster - 2)
    write_field(axis["dis_st"], ceiling)

    print(f"\n  capture {capture}, scale {scale}  "
          f"(x{1024/scale:.3f}, old formula said {capture*1024/scale:.1f})")
    edge = creep_edge(axis["dis_st"], ceiling)
    if edge is None:
        return None
    produced = edge - corner
    print(f"    -> produced {produced} {axis['unit']}  "
          f"(deficit {capture*1024/scale - produced:.1f})")
    return produced


def main():
    global HOST
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--axis", choices=("h", "v"), required=True)
    args = parser.parse_args()
    HOST = args.host

    if json.loads(_get("/freeze") or "{}").get("frozen") is not True:
        print("the unit is NOT frozen — applyPresets will rewrite these "
              "registers mid-measurement. curl 'http://%s/freeze?on=1'" % HOST)
        return 1

    print(__doc__.split("WHY IT EXISTS")[0].strip())
    print("\nFor each point, creep the display edge down until the band of "
          "unwritten\nmemory past the picture just disappears, then type ok.\n")

    points = []
    for capture, scale in MATRIX[args.axis]:
        produced = measure(args.axis, capture, scale)
        if produced is not None:
            points.append((capture, scale, float(produced)))

    if len(points) < 3:
        print("\nfewer than three points — not enough to disconfirm anything")
        return 1

    c, k = geometry_math.fit_loss(points)
    residuals = geometry_math.loss_residuals(points, c, k)
    unit = FIELDS[args.axis]["unit"]
    print(f"\n  produced = (capture - {c:.2f}) x 1024 / scale - {k:.2f}\n")
    print(f"  {'capture':>8}{'scale':>7}{'produced':>10}{'fitted':>9}{'resid':>8}")
    for (capture, scale, produced), residual in zip(points, residuals):
        print(f"  {capture:>8}{scale:>7}{produced:>10.0f}"
              f"{produced - residual:>9.1f}{residual:>8.2f}")
    worst = max(abs(r) for r in residuals)
    print(f"\n  worst residual {worst:.2f} {unit} over {len(points)} points")
    print("  within +-1, so the straight line holds" if worst <= 1.0 else
          "  ABOVE +-1 — the model does not fit, look at the pattern above")
    print(f"\n  points = {points}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
