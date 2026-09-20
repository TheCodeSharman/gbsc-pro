#!/usr/bin/env python3
"""Measure how far a vertical edge moves, line to line and frame to frame.

    python3 tools/gbsc-pro-hwtest/line_shift.py clip.mp4 [clip.mp4 ...]
    python3 tools/gbsc-pro-hwtest/line_shift.py --detail --band 0.2,0.6 clip.mp4

A vertical edge on the card should land in one column on every line of every
frame. Two separate failures move it, and one number cannot hold both:

  ragged    the edge sits somewhere different on each LINE of one frame, so it
            frays. This is what the bad modes show.
  temporal  the whole edge walks between FRAMES while staying straight.

Every edge the card offers is measured, at its own column, and the columns are
reported -- so a comparison between modes is not resting on one feature that
the framing moved. Ragged against column also says whether the displacement
grows along the line, which is what a sampling phase that accumulates would do.

Positions come from the sub-pixel centroid of the gradient peak, summed over R,
G and B rather than taken from luma: the card's bars separate in colour and a
yellow-to-cyan edge nearly vanishes in luma.

Photograph pixels, not output pixels, and comparable only between clips shot at
one camera position.
"""
import argparse
import subprocess

import numpy as np

WINDOW = 5          # columns searched either side of an edge
CENTROID = 2        # columns either side of the peak that carry the position
MIN_STRENGTH = 0.35  # of the edge's own collapsed peak, below which a line is absent


def size(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", path],
        capture_output=True, text=True, check=True).stdout.strip()
    w, h = out.split("x")
    return int(w), int(h)


def gradient(path, top, height, count):
    """|d/dx| summed over colour, shape (frames, rows, width - 1)."""
    w, _ = size(path)
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-vf",
         f"crop=in_w:{height}:0:{top},format=rgb24",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True).stdout
    band = np.frombuffer(raw, dtype=np.uint8).reshape(-1, height, w, 3).astype(np.float32)
    step = max(1, len(band) // count)
    band = band[::step][:count]
    return np.abs(np.diff(band, axis=2)).sum(axis=3)


def edge_columns(grad, wanted):
    """Columns holding an edge on nearly every line, strongest first.

    Selection is by the low percentile across lines rather than by the mean:
    the card's frequency wedge and the circle's flank carry the strongest
    gradients on the card and genuinely move from one line to the next, so a
    mean picks them and then reports the card as a fault in the scaler.
    """
    profile = np.percentile(grad.reshape(-1, grad.shape[2]), 25, axis=0)
    columns = []
    order = np.argsort(profile)[::-1]
    for c in order:
        if c < WINDOW + CENTROID or c >= len(profile) - WINDOW - CENTROID:
            continue
        if all(abs(c - k) > 2 * WINDOW for k in columns):
            columns.append(int(c))
        if len(columns) == wanted:
            break
    return sorted(columns), profile


def positions(grad, column, floor):
    """Sub-pixel column of the edge on every line of every frame, or nan."""
    window = grad[:, :, column - WINDOW - CENTROID:column + WINDOW + CENTROID + 1]
    peak = window[:, :, CENTROID:window.shape[2] - CENTROID].argmax(axis=2) + CENTROID
    rows, cols = np.indices(peak.shape)
    offsets = np.arange(-CENTROID, CENTROID + 1)
    taken = window[rows[..., None], cols[..., None], peak[..., None] + offsets]
    weight = taken.sum(axis=2)
    centre = (taken * offsets).sum(axis=2) / np.where(weight > 0, weight, 1)
    place = column - WINDOW + (peak - CENTROID) + centre
    return np.where(window.max(axis=2) >= floor, place, np.nan)


def sd(values):
    values = values[~np.isnan(values)]
    return float(values.std()) if values.size >= 8 else float("nan")


def line_to_line(place):
    """Spread of the step between ADJACENT lines.

    Differencing is what makes the number the artefact rather than the bench:
    an edge crossing a few hundred lines tilts by several pixels if the camera
    is a fraction of a degree off square, and a spread taken about the mean
    reports that tilt on a perfectly straight edge.
    """
    steps = np.diff(place, axis=1)
    return sd(steps.reshape(-1)) / np.sqrt(2)


def measure(path, band, count, wanted):
    _, h = size(path)
    top, height = int(band[0] * h), int((band[1] - band[0]) * h)
    grad = gradient(path, top, height, count)
    columns, profile = edge_columns(grad, wanted)

    edges = []
    for column in columns:
        place = positions(grad, column, MIN_STRENGTH * profile[column])
        whole = np.nanmean(place, axis=1)
        edges.append(dict(column=column, lines=line_to_line(place),
                          frames=sd(whole),
                          seen=float(np.mean(~np.isnan(place)))))
    return [e for e in edges if not np.isnan(e["lines"])], len(grad)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("clips", nargs="+")
    ap.add_argument("--band", default="0.10,0.80",
                    help="fraction of frame height to analyse, top,bottom")
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--edges", type=int, default=8)
    ap.add_argument("--detail", action="store_true", help="every edge, not the summary")
    args = ap.parse_args()
    band = tuple(float(x) for x in args.band.split(","))

    if not args.detail:
        print(f"{'clip':<24} {'frames':>6} {'edges':>5} {'ragged px':>10} "
              f"{'worst':>6} {'temporal px':>11}")
    for clip in args.clips:
        edges, count = measure(clip, band, args.frames, args.edges)
        name = clip.rsplit("/", 1)[-1]
        if not edges:
            print(f"{name:<24} no edge found")
            continue
        if args.detail:
            print(f"\n{name}  ({count} frames)")
            print(f"  {'column':>7} {'lines seen':>11} {'ragged px':>10} {'temporal px':>11}")
            for e in edges:
                print(f"  {e['column']:>7} {e['seen']:>10.0%} {e['lines']:>10.3f} "
                      f"{e['frames']:>11.3f}")
            continue
        ragged = [e["lines"] for e in edges]
        print(f"{name:<24} {count:>6} {len(edges):>5} {np.median(ragged):>10.3f} "
              f"{max(ragged):>6.2f} {np.median([e['frames'] for e in edges]):>11.3f}")


if __name__ == "__main__":
    main()
