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
import bench_probe
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
              axis=geometry_math.AXIS_H, base=264, unit="px"),
    "v": dict(cap_sp=("IF_VB_SP", 1, 0x1E, 0, 11),
              cap_st=("IF_VB_ST", 1, 0x1C, 0, 11),
              scale=("VDS_VSCALE", 3, 0x17, 4, 10),
              dis_sp=("VDS_DIS_VB_SP", 3, 0x14, 4, 11),
              dis_st=("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
              win_sp=("VDS_VB_SP", 3, 0x08, 4, 11),
              win_st=("VDS_VB_ST", 3, 0x07, 0, 11),
              rst=("VDS_VSYNC_RST", 3, 0x02, 4, 11),
              axis=geometry_math.AXIS_V, base=30, unit="lines"),
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


CREEP_HELP = """    Enter  down one step      u  up one step
    s N    set the step         N  jump straight to N
    ok     this is the edge     q  skip this point"""


def creep_edge(probe, field, start, step=4):
    """Walk the display edge down until the band goes, one step at a time.

    Deliberately not a bisection. Bisecting asks fewer questions, but each one
    lands on an unfamiliar value with no context; creeping lets the band shrink
    under your eye, which is what every successful measurement in this project
    has actually used. The last value the eye rejects and the first it accepts
    are one step apart, so the step size is the measurement's resolution -- keep
    it at 1 for the final approach.
    """
    value = start
    probe.write_field(field, value)
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
        probe.write_field(field, value)


def measure(probe, axis_key, capture, scale):
    """Set every register this point needs, then hand the far edge to the eye.

    The raster is read BEFORE anything is placed against it. Everything the
    creep reports is `edge - corner`, so a corner computed from a raster that
    had not been read yet does not fail loudly -- it returns a plausible number
    wrong by however far the corner moved, which is the failure this whole tool
    exists to have stopped making.
    """
    axis = FIELDS[axis_key]
    raster = probe.read_field(axis["rst"])
    if raster is None:
        print("    could not read the raster total -- is the unit reachable?")
        return None
    raster += 1

    sp = axis["base"]
    probe.write_field(axis["cap_st"], sp + capture)
    probe.write_field(axis["cap_sp"], sp)
    probe.write_field(axis["scale"], scale)

    # The corner is not a constant -- it is where the scaler starts writing at
    # THIS magnification, which is the whole finding of 2026-08-05. Taking it
    # from a fixed value is what made `produced` look like it had a loss term.
    magnification = geometry_math.magnification(scale)
    predicted = geometry_math.produced_px(capture, scale)
    corner, window_sp = geometry_math.place_picture(
        predicted, raster, magnification, axis["axis"])
    probe.write_field(axis["dis_sp"], corner)
    probe.write_field(axis["win_sp"], window_sp)
    probe.write_field(axis["win_st"], raster - 2)

    # produced is capture x 1024 / scale exactly, so park a little above it and
    # the band is showing; the creep only ever goes down from there.
    ceiling = min(corner + int(predicted) + 8, raster - 2)
    probe.write_field(axis["dis_st"], ceiling)

    print(f"\n  capture {capture}, scale {scale}  (x{magnification:.3f}, "
          f"model says {predicted:.1f}, corner {corner})")
    edge = creep_edge(probe, axis["dis_st"], ceiling)
    if edge is None:
        return None
    produced = edge - corner
    print(f"    -> produced {produced} {axis['unit']}  "
          f"(against the model {produced - predicted:+.1f})")
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

    probe = bench_probe.Probe(read_reg, write_reg)

    print(__doc__.split("WHY IT EXISTS")[0].strip())
    print("\nFor each point, creep the display edge down until the band of "
          "unwritten\nmemory past the picture just disappears, then type ok.\n")

    points = []
    for capture, scale in MATRIX[args.axis]:
        produced = measure(probe, args.axis, capture, scale)
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
