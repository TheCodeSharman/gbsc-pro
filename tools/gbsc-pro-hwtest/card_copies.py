#!/usr/bin/env python3
"""How many times the emitted frame plays PATTERN CARD out.

    python3 tools/gbsc-pro-hwtest/card_copies.py --frames 2

The card carries ONE red block, low and centred. Counting the red blobs counts
the copies: one is a correct playout, two is the frame played out twice.

The green frame's extent cannot answer this -- a second copy that lands inside
the first one's bounding box leaves it unchanged, and every reading taken that
way reported a doubled frame as clean.
"""

import argparse

import numpy as np

import hdmi_capture as cap


def redness(rgb):
    r, g, b = (rgb[..., i].astype(np.int16) for i in range(3))
    return (r > 70) & (r - g > 50) & (r - b > 50)


def bands(mask, floor):
    """Runs of rows carrying at least `floor` marked pixels."""
    lit = mask.sum(axis=1) >= floor
    runs, start = [], None
    for y, on in enumerate(lit):
        if on and start is None:
            start = y
        elif not on and start is not None:
            runs.append((start, y - 1))
            start = None
    if start is not None:
        runs.append((start, len(lit) - 1))

    # A band a few rows tall is sensor noise at an edge, not a copy of the
    # block: the card's own is 28 rows at this framing.
    return [(a, b) for a, b in runs if b - a >= 5]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--frames", type=int, default=2)
    p.add_argument("--floor", type=int, default=8)
    p.add_argument("--png", default=None)
    args = p.parse_args()

    rgb = cap.frames(args.frames, cap.device())
    for i in range(rgb.shape[0]):
        runs = bands(redness(rgb[i]), args.floor)
        verdict = "CLEAN" if len(runs) == 1 else ("DOUBLED" if len(runs) == 2
                                                  else f"{len(runs)} copies")
        print(f"  frame {i}: {verdict}  red bands at "
              + ", ".join(f"{a}..{b}" for a, b in runs))
    if args.png:
        import subprocess
        subprocess.run(["ffmpeg", "-hide_banner", "-v", "error", "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-video_size", "1920x1080", "-i", "-",
                        "-frames:v", "1", "-update", "1", "-y", args.png],
                       input=rgb[0].tobytes(), check=True)
        print(f"  wrote {args.png}")


if __name__ == "__main__":
    main()
