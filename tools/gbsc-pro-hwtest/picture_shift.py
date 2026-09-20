#!/usr/bin/env python3
"""Measure the picture off a tv-snap frame: where it sits, and what stands beyond it.

    python3 tools/gbsc-pro-hwtest/picture_shift.py --shift clean.jpg suspect.jpg
    python3 tools/gbsc-pro-hwtest/picture_shift.py --bands shot.jpg --from 1030

--shift cross-correlates each frame's column profile against the first and
reports how far the picture has panned. The whole profile is used rather than an
edge, because an edge measure cannot tell the picture's end from a band standing
beyond it -- which is the case this exists for.

--bands reports runs of columns standing above the background, for reading a
band beyond the picture.

Profiles take the STRONGEST of R, G and B per column rather than luma. The band
past the picture changes colour with the framing and the mode, and a saturated
blue one weighs 0.114 in luma and sinks into the black around it -- a luma
profile reports no band at a screen that plainly has one.

Photo columns are not output pixels. Calibrate by panning a known number of
capture units and dividing, and re-calibrate after any output excursion:
docs/investigations/the-bar-and-the-pan-are-one-displacement.md
"""

import argparse
import statistics
import subprocess


def _run(args):
    return subprocess.run(args, capture_output=True, check=True).stdout


def width_of(path):
    out = _run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                "-show_entries", "stream=width", "-of", "csv=p=0", path])
    return int(out.split(b",")[0])


def profile(path, top, bottom):
    """One value per column: the strongest channel, averaged down the rows."""
    raw = _run(["ffmpeg", "-v", "error", "-i", path, "-vf",
                f"crop=iw:{bottom - top}:0:{top},format=rgb24",
                "-f", "rawvideo", "-"])
    width = width_of(path)
    rows = len(raw) // (width * 3)
    cols = []
    for column in range(width):
        totals = [0, 0, 0]
        for row in range(rows):
            base = (row * width + column) * 3
            totals[0] += raw[base]
            totals[1] += raw[base + 1]
            totals[2] += raw[base + 2]
        cols.append(max(total / rows for total in totals))
    return cols


def bands(cols, first, last, over):
    """Runs of columns standing `over` above the median of the window."""
    window = cols[first:last]
    background = statistics.median(window)
    found, start = [], None
    for index, value in enumerate(window):
        if value > background + over and start is None:
            start = index
        elif value <= background + over and start is not None:
            found.append((first + start, first + index - 1, max(window[start:index])))
            start = None
    if start is not None:
        found.append((first + start, last - 1, max(window[start:])))
    return background, found


def shift(reference, other, span):
    """The offset that best aligns `other` onto `reference`, in columns."""
    centred = [value - statistics.mean(reference) for value in reference]
    against = [value - statistics.mean(other) for value in other]
    count = len(centred)
    best, best_score = 0, None
    for offset in range(-span, span + 1):
        low = max(0, -offset) + 60
        high = min(count, count - offset) - 60
        if high - low < 300:
            continue
        score = sum(centred[i] * against[i + offset] for i in range(low, high))
        score /= high - low
        if best_score is None or score > best_score:
            best, best_score = offset, score
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("frames", nargs="+")
    parser.add_argument("--shift", action="store_true",
                        help="pan of every frame against the first")
    parser.add_argument("--bands", action="store_true",
                        help="runs standing above the background")
    parser.add_argument("--top", type=int, default=140)
    parser.add_argument("--bottom", type=int, default=540)
    parser.add_argument("--from", dest="first", type=int, default=1030)
    parser.add_argument("--to", dest="last", type=int, default=0)
    parser.add_argument("--over", type=float, default=12.0)
    parser.add_argument("--span", type=int, default=220,
                        help="widest pan --shift will look for")
    args = parser.parse_args()

    profiles = [(path, profile(path, args.top, args.bottom)) for path in args.frames]

    if args.bands or not args.shift:
        for path, cols in profiles:
            last = args.last or len(cols)
            background, found = bands(cols, args.first, last, args.over)
            print(f"{path}  background {background:.1f} over {args.first}..{last}")
            for start, stop, peak in found:
                print(f"  band x{start}..{stop}  {stop - start + 1} cols  peak {peak:.0f}")
            if not found:
                print("  no band")

    if args.shift:
        reference = profiles[0]
        print(f"reference {reference[0]}")
        for path, cols in profiles[1:]:
            print(f"  {path}  shift {shift(reference[1], cols, args.span):+d} columns")


if __name__ == "__main__":
    main()
