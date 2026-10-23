#!/usr/bin/env python3
"""WHERE the emitted picture is unsteady, as a grid of cells over the frame.

    python3 tools/gbsc-pro-hwtest/frame_cells.py --size 1280x720
    python3 tools/gbsc-pro-hwtest/frame_cells.py --cell 48 --frames 160

**A FIGURE AVERAGED OVER THE PICTURE MISSES A DISTURBANCE CONFINED TO PART OF
IT.** `picture_jitter.py` reports four bands each spanning the whole width, so
a test card's fine grating -- a few per cent of the frame, and the only place a
sub-pixel displacement is visible at all -- contributes a few per cent of the
reading. This asks the same question per cell and reports the map, so the
answer carries where as well as how much.

Two channels, because a sub-pixel disturbance reaches detail in two different
ways and one channel is blind to each:

- **displacement**, the cell's column profile correlated against the first
  frame's, in columns. Coarse edges move.
- **contrast**, the temporal spread of the cell's own spatial spread. Detail
  near the sampling limit does not move when the sample grid shifts under it:
  its amplitude changes instead, which is what the eye reads as shimmer.

Both are judged against the rest of the picture rather than against an
absolute, because the dongle's own gain and noise move every cell together.
A floor stops a still frame's rounding from ranking as a disturbance.

`picture_jitter.py` owns the estimator and what calibrates it.
`docs/investigations/the-sampling-phase-cannot-reach-the-shimmer.md` is the
artefact. `docs/bench-output-capture.md` is the dongle.
"""

import argparse
import sys

import numpy as np

from hdmi_capture import device, frames, luma
from picture_jitter import displacement, gain_for, lit_box

CELL = 64

# Grey levels of spatial spread a cell's column profile needs before a
# displacement read off it means anything. Below this the cell is flat and the
# estimator reports its own noise.
MEASURABLE_CONTRAST = 2.0

# A probe gain outside this is content the estimator cannot calibrate against,
# so dividing by it inflates noise instead of correcting bias.
GAIN_RANGE = (0.2, 5.0)

# How far above the picture's own median a cell has to read to be called out.
MOVING_MULTIPLE = 5.0
BREATHING_MULTIPLE = 5.0

# And the floor those multiples apply to, so a still picture's rounding cannot
# set a threshold of nearly zero and flag every cell in the frame.
MOVE_FLOOR = 0.005
BREATH_FLOOR = 0.002

RAMP = " .:-=+*#%@"


def _origins(lo, hi, size):
    """Cell origins from `lo`, the last one ending on `hi`."""
    span = hi - lo
    if span < size:
        return [lo]
    count = max(1, span // size)
    if count == 1:
        return [lo]
    step = (span - size) / (count - 1)
    return sorted({int(lo + round(i * step)) for i in range(count)})


def cell_grid(box, size=CELL):
    """(left, top) of every cell tiling `box`, given as (left, right, top,
    bottom)."""
    left, right, top, bottom = box
    return [(x, y) for y in _origins(top, bottom, size)
            for x in _origins(left, right, size)]


def cell_series(grey, origin, size=CELL):
    """A cell's column profile and its spatial spread, per frame."""
    x, y = origin
    cut = grey[:, y:y + size, x:x + size]
    profiles = cut.mean(axis=1)
    return profiles, profiles.std(axis=1)


def cell_reading(profiles, contrast):
    """One cell's displacement spread, peak-to-peak and contrast modulation."""
    gain = gain_for(profiles[0])
    usable = (float(contrast.mean()) >= MEASURABLE_CONTRAST
              and GAIN_RANGE[0] <= gain <= GAIN_RANGE[1])
    moves = np.array([displacement(profiles[0], p)[0] for p in profiles]) / gain
    mean_contrast = float(contrast.mean())
    return dict(measurable=usable,
                gain=float(gain),
                contrast=mean_contrast,
                move_sd=float(moves.std()),
                move_p2p=float(moves.max() - moves.min()),
                cstd=float(contrast.std()),
                cmod=float(contrast.std() / mean_contrast) if mean_contrast else 0.0)


def measure(grey, size=CELL, box=None):
    """Every cell's reading, plus the box they tile."""
    box = box or lit_box(grey[0])
    if box is None:
        return None
    cells = []
    for origin in cell_grid(box, size):
        profiles, contrast = cell_series(grey, origin, size)
        cells.append(dict(x=origin[0], y=origin[1],
                          **cell_reading(profiles, contrast)))
    return dict(box=box, size=size, cells=cells)


def _above(cells, key, multiple, floor):
    """Measurable cells reading more than `multiple` times the picture's own
    level, worst first."""
    usable = [c for c in cells if c["measurable"]]
    if not usable:
        return []
    level = max(float(np.median([c[key] for c in usable])), floor)
    return sorted((c for c in usable if c[key] > multiple * level),
                  key=lambda c: -c[key])


def moving(cells, multiple=MOVING_MULTIPLE):
    return _above(cells, "move_sd", multiple, MOVE_FLOOR)


def breathing(cells, multiple=BREATHING_MULTIPLE):
    return _above(cells, "cmod", multiple, BREATH_FLOOR)


def summary(result, key="move_sd"):
    """The picture's level and its worst cell, over the measurable cells."""
    usable = [c for c in result["cells"] if c["measurable"]]
    if not usable:
        return None
    values = np.array([c[key] for c in usable])
    worst = max(usable, key=lambda c: c[key])
    return dict(cells=len(usable), median=float(np.median(values)),
                p90=float(np.percentile(values, 90)), worst=float(values.max()),
                at=(worst["x"], worst["y"]))


def heat_map(result, key="move_sd"):
    """The grid as rows of characters, scaled to the worst measurable cell."""
    size = result["size"]
    xs = sorted({c["x"] for c in result["cells"]})
    ys = sorted({c["y"] for c in result["cells"]})
    at = {(c["x"], c["y"]): c for c in result["cells"]}
    top = max((c[key] for c in result["cells"] if c["measurable"]), default=0.0)
    rows = []
    for y in ys:
        line = ""
        for x in xs:
            cell = at[(x, y)]
            if not cell["measurable"]:
                line += "_"
            elif top <= 0:
                line += RAMP[0]
            else:
                line += RAMP[min(len(RAMP) - 1,
                                 int(cell[key] / top * (len(RAMP) - 1)))]
        rows.append(line)
    return rows, xs, ys, size


def _report(result, key, title):
    rows, xs, ys, size = heat_map(result, key)
    stats = summary(result, key)
    print(f"\n{title}   cell {size}px, '_' is content the estimator cannot read")
    for y, line in zip(ys, rows):
        print(f"  {y:5}  {line}")
    print(f"  {'':5}  {''.join('|' if i % 5 == 0 else ' ' for i in range(len(xs)))}"
          f"   x from {xs[0]} step {xs[1] - xs[0] if len(xs) > 1 else 0}")
    if stats is None:
        print("  nothing measurable")
        return
    print(f"  {stats['cells']} measurable   median {stats['median']:.4f}   "
          f"p90 {stats['p90']:.4f}   worst {stats['worst']:.4f} at "
          f"{stats['at'][0]},{stats['at'][1]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--cell", type=int, default=CELL)
    parser.add_argument("--size", default="1920x1080",
                        help="what to ask the dongle for. PASS-THROUGH emits the "
                             "SOURCE's raster, and asking for anything else makes "
                             "the dongle resample, which is itself a shimmer")
    parser.add_argument("--device", default=None)
    parser.add_argument("--warmup", type=int, default=12)
    args = parser.parse_args()

    width, height = (int(n) for n in args.size.split("x"))
    grey = luma(frames(args.frames, args.device or device(), width, height,
                       warmup=args.warmup))
    result = measure(grey, args.cell)
    if result is None:
        print("ALL BLACK -- ask the board before the connector, see hdmi_capture.py")
        return 1

    left, right, top, bottom = result["box"]
    print(f"picture {right - left}x{bottom - top} at {left},{top}   "
          f"{len(grey)} frames of {width}x{height}")
    _report(result, "move_sd", "DISPLACEMENT, columns")
    _report(result, "cmod", "CONTRAST modulation, fraction")

    for name, found in (("moving", moving(result["cells"])),
                        ("breathing", breathing(result["cells"]))):
        if found:
            where = ", ".join(f"{c['x']},{c['y']}" for c in found[:6])
            print(f"\n{len(found)} cells {name}: {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
