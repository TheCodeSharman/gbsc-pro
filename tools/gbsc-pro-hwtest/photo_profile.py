#!/usr/bin/env python3
"""Read a bench photograph as a one-dimensional profile.

Where the picture is the only instrument, what is wanted from a photograph is
usually one number: which column an edge sits in. This reduces a band of the
frame to a per-column mean and finds the first sustained rise out of black,
which on the plain capture card is the source's first active pixel.

ffmpeg does the reduction, so there is no imaging dependency to install.

    photo_profile.py shot.jpg                     # the edge column
    photo_profile.py shot.jpg --dump              # the whole profile
    photo_profile.py shot.jpg --band 0.55,0.60    # a different slice

Pick a band that crosses the card's vertical bands and nothing else bright.
Three bands agreeing is the check that the feature found is the right one.
"""
import argparse
import subprocess
import sys


def size(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", path],
        capture_output=True, text=True, check=True).stdout.strip()
    w, h = out.split("x")
    return int(w), int(h)


def columns(path, band, xrange=None):
    w, h = size(path)
    f0, f1 = band
    y0, y1 = int(h * f0), int(h * f1)
    x0, x1 = xrange if xrange else (0, w)
    vf = (f"crop={x1 - x0}:{y1 - y0}:{x0}:{y0},"
          f"scale={x1 - x0}:1:flags=area,format=rgb24")
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-vf", vf,
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True).stdout
    return x0, [tuple(raw[i * 3:i * 3 + 3]) for i in range(x1 - x0)]


def first_edge(grey, run=12):
    """The half-maximum crossing of the first rise that stays up for `run`."""
    half = (min(grey) + max(grey)) / 2
    for i in range(1, len(grey)):
        if grey[i] >= half and all(v >= half for v in grey[i:i + run]):
            a, b = grey[i - 1], grey[i]
            return (i - 1) + (half - a) / (b - a) if b != a else float(i)
    return None


def main():
    a = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("path")
    a.add_argument("--band", default="0.23,0.27",
                   help="vertical slice as two fractions of frame height")
    a.add_argument("--xrange", default=None, help="X0,X1 to profile")
    a.add_argument("--dump", action="store_true",
                   help="print the profile rather than the edge")
    a.add_argument("--run", type=int, default=12,
                   help="columns an edge must stay up for")
    args = a.parse_args()

    band = tuple(float(v) for v in args.band.split(","))
    xr = tuple(int(v) for v in args.xrange.split(",")) if args.xrange else None
    x0, px = columns(args.path, band, xr)
    grey = [(r + g + b) / 3 for r, g, b in px]

    if args.dump:
        for i, ((r, g, b), m) in enumerate(zip(px, grey)):
            print(f"{x0 + i}\t{m:6.1f}\t{r}\t{g}\t{b}")
        return

    e = first_edge(grey, args.run)
    if e is None:
        print("none", file=sys.stderr)
        raise SystemExit(1)
    print(f"{x0 + e:.2f}")


if __name__ == "__main__":
    main()
