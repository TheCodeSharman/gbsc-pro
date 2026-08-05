#!/usr/bin/env python3
"""Measure where the scaler starts writing, by eye. The near-edge twin of
measure_produced.py.

    python3 tools/gbsc-pro-hwtest/measure_origin.py \
        --host 192.168.88.108 --axis h

WHAT IT MEASURES. The write start moves:

    write start = VDS_?B_SP + START_CONST + START_PER_MAG x magnification

Measured from there, `produced` is exactly capture x 1024 / scale on both axes,
with no loss term. Kept to re-measure the constants on another unit or another
raster. docs/investigations/moving-write-origin.md

HOW. TestPat's animation exists for this. Its screen border and outermost ring
flip colour twice a second, so anything flipping is being written this frame and
anything frozen is scratch the scaler has stopped writing to. Park the display
window's near edge before the write start, and a band of frozen scratch shows
between the edge and the live picture. Creep the edge up until the band goes:
that value is the write start.

**THE TRAP: MEASURING THE BEZEL AND CALLING IT THE SCALER.** At the bench's
usual placement the panel's left edge and the write start are two pixels apart,
so a probe parked between them watches the band vanish at the bezel and records
the panel's edge. That is where the irreconcilable 78 and 94 came from: 49 + 78
= 127 and 35 + 94 = 129, one edge measured twice while VDS_HB_SP moved underneath
it. `geometry_math.probe_park` refuses to park anywhere it can happen, so this
tool moves the picture well right before creeping anything.

WHAT THE MATRIX SEPARATES. Two independent questions, three points each, because
two points lie on a line by construction and cannot disconfirm one:

    A  vary VDS_?B_SP at one scale   -- does the write start track the memory
                                       window, or is it pinned near the bezel?
    B  vary the scale at one VDS_?B_SP -- does the write start move with
                                       magnification? If it does, output_loss is
                                       a start-position artefact and the scaling
                                       really is a pure multiply.

The unit must be FROZEN (`curl 'http://<ip>/freeze?on=1'`), or applyPresets will
rewrite the registers underneath the measurement. Leave regpanel's pads alone
while this runs -- a pad press recomputes the display window and destroys the
probe. Film the screen rather than photographing it; a still cannot show which
half of the flash you caught.
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

# (win_sp, scale). Capture is held fixed so the only thing moving is the thing
# being asked about. The shared point is deliberate: it ties the two fits
# together, so a disagreement between them shows up as a residual rather than as
# two separate stories.
MATRIX = {
    "h": {"capture": 200,
          "A": [(160, 512), (260, 512), (360, 512)],
          "B": [(260, 1023), (260, 512), (260, 320)]},
    "v": {"capture": 200,
          "A": [(160, 512), (240, 512), (320, 512)],
          "B": [(240, 1023), (240, 512), (240, 300)]},
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
              visible=geometry_math.PANEL_VISIBLE_LEFT,
              axis=geometry_math.AXIS_H,
              base=264, unit="px", edge="left"),
    "v": dict(cap_sp=("IF_VB_SP", 1, 0x1E, 0, 11),
              cap_st=("IF_VB_ST", 1, 0x1C, 0, 11),
              scale=("VDS_VSCALE", 3, 0x17, 4, 10),
              dis_sp=("VDS_DIS_VB_SP", 3, 0x14, 4, 11),
              dis_st=("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
              win_sp=("VDS_VB_SP", 3, 0x08, 4, 11),
              win_st=("VDS_VB_ST", 3, 0x07, 0, 11),
              rst=("VDS_VSYNC_RST", 3, 0x02, 4, 11),
              visible=geometry_math.PANEL_VISIBLE_TOP,
              axis=geometry_math.AXIS_V,
              base=30, unit="lines", edge="top"),
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


CREEP_HELP = """    Enter  up one step        d  down one step
    s N    set the step         N  jump straight to N
    ok     the band has gone    q  skip this point"""


def creep_up(probe, field, start, step=4):
    """Walk the display edge up until the frozen band goes, one step at a time.

    Creeping rather than bisecting, for the reason measure_produced gives: a
    bisection asks fewer questions but each lands on an unfamiliar value with no
    context, and every measurement that has actually worked on this bench let the
    band shrink under the eye. The last value the eye rejects and the first it
    accepts are one step apart, so the step is the resolution -- finish at 1.
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
        if answer == "d":
            value -= step
        elif answer.startswith("s "):
            try:
                step = max(1, int(answer[2:]))
            except ValueError:
                print("    ? step wants a number")
            continue
        elif answer == "":
            value += step
        else:
            try:
                value = int(answer)
            except ValueError:
                print(CREEP_HELP)
                continue
        probe.write_field(field, value)


def measure(probe, axis_key, capture, win_sp):
    axis = FIELDS[axis_key]
    scale = probe.read_field(axis["scale"])
    raster = probe.read_field(axis["rst"])
    if raster is None:
        print("    could not read the raster total -- is the unit reachable?")
        return None
    raster += 1

    expected = win_sp + axis["axis"].origin_offset(1024 / scale)
    try:
        park = geometry_math.probe_park(expected, axis["visible"])
    except ValueError as refusal:
        print(f"    SKIPPED: {refusal}")
        return None

    probe.write_field(axis["cap_sp"], axis["base"])
    probe.write_field(axis["cap_st"], axis["base"] + capture)
    probe.write_field(axis["win_sp"], win_sp)
    probe.write_field(axis["win_st"], raster - 2)
    # Rail the far edge. A scratch band at the far end as well is not a nuisance
    # here, it is the control: if that band is not frozen while this one is, the
    # eye is not reading the flash the way the measurement assumes.
    probe.write_field(axis["dis_st"], raster - 2)

    magnification = geometry_math.HSCALE_UNITY / scale
    print(f"\n  {axis['win_sp'][0]} {win_sp}, scale {scale} (x{magnification:.3f}), "
          f"capture {capture}")
    print(f"    parked at {park}; predicted write start ~{expected:.0f}")
    start = creep_up(probe, axis["dis_sp"], park)
    if start is None:
        return None
    print(f"    -> write start {start}, i.e. {start - win_sp} "
          f"{axis['unit']} after {axis['win_sp'][0]}")
    return dict(win_sp=win_sp, scale=scale, capture=capture, write_start=start,
                offset=start - win_sp, magnification=magnification)


def report(question, heading, xs_label, xs, points, unit):
    print(f"\n  {question}  {heading}")
    if len(points) < 3:
        print("    fewer than three points -- cannot disconfirm anything")
        return None
    starts = [p["write_start"] for p in points]
    try:
        slope, intercept = geometry_math.fit_line(xs, starts)
    except ValueError as refusal:
        print(f"    {refusal}")
        return None
    residuals = geometry_math.line_residuals(xs, starts, slope, intercept)
    print(f"    write_start = {slope:.3f} x {xs_label} + {intercept:.2f}")
    print(f"    {xs_label:>12}{'start':>8}{'fitted':>9}{'resid':>8}")
    for x, point, residual in zip(xs, points, residuals):
        print(f"    {x:>12.3f}{point['write_start']:>8}"
              f"{point['write_start'] - residual:>9.1f}{residual:>8.2f}")
    worst = max(abs(r) for r in residuals)
    print(f"    worst residual {worst:.2f} {unit}")
    return slope, intercept, worst


def main():
    global HOST
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--axis", choices=("h", "v"), default="h")
    parser.add_argument("--out", help="write the readings here as JSON")
    args = parser.parse_args()
    HOST = args.host

    if json.loads(_get("/freeze") or "{}").get("frozen") is not True:
        print(f"the unit is NOT frozen -- applyPresets will rewrite these "
              f"registers mid-measurement. curl 'http://{HOST}/freeze?on=1'")
        return 1

    axis = FIELDS[args.axis]
    plan = MATRIX[args.axis]
    probe = bench_probe.Probe(read_reg, write_reg)

    print(__doc__.split("WHAT IS BEING ASKED")[0].strip())
    print(f"\nRun TestPat and watch the {axis['edge']} of the picture. Creep the "
          f"display edge\nuntil the FROZEN band just disappears and everything "
          f"left is flipping, then\ntype ok. Frozen is scratch; flipping is live.")
    print("\nDo not touch regpanel's pads while this runs.\n")

    readings = []
    for question, pairs in (("A", plan["A"]), ("B", plan["B"])):
        for win_sp, scale in pairs:
            probe.write_field(axis["scale"], scale)
            point = measure(probe, args.axis, plan["capture"], win_sp)
            if point is not None:
                point["question"] = question
                readings.append(point)

    unit = axis["unit"]
    a = [p for p in readings if p["question"] == "A"]
    b = [p for p in readings if p["question"] == "B"]

    fit_a = report("A", "does the write start track the memory window?",
                   axis["win_sp"][0], [p["win_sp"] for p in a], a, unit)
    fit_b = report("B", "does the write start move with magnification?",
                   "magnification", [p["magnification"] for p in b], b, unit)

    print("\n  reading:")
    if fit_a:
        slope = fit_a[0]
        if abs(slope - 1.0) < 0.05:
            print(f"    A: slope {slope:.3f} -- the write start tracks "
                  f"{axis['win_sp'][0]} one for one, so the offset is real and "
                  f"is {sum(p['offset'] for p in a) / len(a):.1f} {unit}")
        elif abs(slope) < 0.05:
            print(f"    A: slope {slope:.3f} -- the write start does NOT move "
                  f"with {axis['win_sp'][0]}. It is pinned, and every previous "
                  f"'origin offset' was the panel edge.")
        else:
            print(f"    A: slope {slope:.3f} -- neither 1 nor 0. Look at the "
                  f"residuals before believing any of it.")
    if fit_b:
        slope = fit_b[0]
        if abs(slope) < 1.0:
            print(f"    B: slope {slope:.3f} {unit} per x -- the write start "
                  f"does not move with magnification, so output_loss is NOT a "
                  f"start-position artefact.")
        else:
            print(f"    B: slope {slope:.3f} {unit} per x -- the write start "
                  f"moves with magnification. Model says "
                  f"{axis["axis"].start_per_mag:.1f}.")

    print(f"\n  readings = {readings}")
    if args.out:
        with open(args.out, "w") as handle:
            json.dump(readings, handle, indent=1)
        print(f"  written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
