#!/usr/bin/env python3
"""Whether the emitted line carries the samples it should, scored off the frame.

    python3 tools/gbsc-pro-hwtest/picture_shear.py
    python3 tools/gbsc-pro-hwtest/picture_shear.py --frames 8

The horizontal zoom used to shear the picture on about half its steps -- wrong
sample SELECTION, which reads as the frequency wedge's bars splitting into
hairlines and the card's curves going ragged. `Axis::solve()` biases the memory
window to an ODD width against it and the artefact does not appear.
docs/investigations/horizontal-scale-corruption.md

Every tool that marked one took its verdict FROM THE SCREEN, so nothing could
regression-test the bias. This scores the emitted frame instead.

**ROW AGAINST ROW, never a fixed band of the frame.** Zoom moves the picture and
changes what sits at any column, so a count inside a fixed band reports the
movement rather than the artefact. Adjacent rows of the card are near-identical
wherever it carries vertical bars, and that similarity is a property of the
PICTURE rather than of where it landed -- so it survives a zoom step, which is
the whole point.

Two numbers, because one of them is blind to half of it:

  displaced   the fraction of adjacent row pairs that correlate best at a
              non-zero lag. A line built from the wrong samples sits beside its
              neighbour at an offset.
  rough       how far an adjacent pair differs at its own best lag, against the
              contrast in the pair. A line built from the wrong samples differs
              from its neighbour in a way no lag explains, which is what the
              hairline splitting is.

Pairs whose rows genuinely differ -- a feature boundary, the caption, the edge
of the circle -- carry no verdict either way and are dropped: a pair is scored
only where the two rows are similar enough for the lag to mean something.
"""

import argparse
import sys

import numpy as np

from hdmi_capture import device, frames, luma

# How far a line may be looked for beside its neighbour. The artefact is a
# wrong sample within the line rather than a pan, so a lag this side of a few
# columns is all that has to be reachable.
MAXLAG = 4

# A pair is scored only where the two rows are alike at their best lag. Above
# this they are a feature boundary and say nothing about sample selection.
PAIR_LIMIT = 0.35

# A row with no contrast correlates with anything, so it carries no verdict.
MIN_CONTRAST = 6.0


def _pair_scores(above, below, maxlag=MAXLAG):
    """The best lag for `below` against `above`, and how far apart they stay."""
    width = above.shape[0]
    best, where = None, 0
    for lag in range(-maxlag, maxlag + 1):
        lo = max(0, lag)
        hi = min(width, width + lag)
        if hi - lo < width // 2:
            continue
        cost = np.abs(above[lo:hi] - below[lo - lag:hi - lag]).mean()
        if best is None or cost < best:
            best, where = cost, lag
    return where, best


def measure(frame, maxlag=MAXLAG, pair_limit=PAIR_LIMIT,
            min_contrast=MIN_CONTRAST):
    """`displaced` and `rough` over one frame, with how many pairs were scored."""
    grey = luma(frame).astype(np.float32)
    displaced, roughness = [], []
    for row in range(grey.shape[0] - 1):
        above, below = grey[row], grey[row + 1]
        contrast = max(above.std(), below.std())
        if contrast < min_contrast:
            continue
        lag, cost = _pair_scores(above, below, maxlag)
        if cost is None:
            continue
        relative = cost / contrast
        if relative > pair_limit:
            continue
        displaced.append(1 if lag != 0 else 0)
        roughness.append(relative)
    if not displaced:
        return {"displaced": None, "rough": None, "pairs": 0}
    return {"displaced": float(np.mean(displaced)),
            "rough": float(np.mean(roughness)),
            "pairs": len(displaced)}


def measure_clip(clip, **kwargs):
    """The WORST frame of a clip on each number, the artefact not being on every
    frame and a mean over a clip hiding one that is."""
    scored = [measure(f, **kwargs) for f in clip]
    scored = [s for s in scored if s["pairs"]]
    if not scored:
        return {"displaced": None, "rough": None, "pairs": 0}
    return {"displaced": max(s["displaced"] for s in scored),
            "rough": max(s["rough"] for s in scored),
            "pairs": min(s["pairs"] for s in scored)}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--device", default=None)
    args = parser.parse_args()

    dev = args.device or device()
    clip = frames(args.frames, dev)
    for i, frame in enumerate(clip):
        got = measure(frame)
        print(f"  frame {i}  displaced {got['displaced']}  rough {got['rough']}"
              f"  pairs {got['pairs']}")
    worst = measure_clip(clip)
    print(f"worst  displaced {worst['displaced']}  rough {worst['rough']}"
          f"  pairs {worst['pairs']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
