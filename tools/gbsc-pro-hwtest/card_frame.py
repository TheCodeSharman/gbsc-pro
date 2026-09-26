#!/usr/bin/env python3
"""Where PATTERN CARD's one-pixel green frame lands in the emitted frame.

    python3 tools/gbsc-pro-hwtest/card_frame.py --frames 3

The frame marks the source's outermost pixels, so its position is where the
source's own picture sits once the capture window and the scale have placed it.

Taken as the OUTERMOST green above a threshold, never the strongest, and
requiring a run most of the picture's height: the card carries green colour
blocks in its middle and argmax finds one of those instead.

**IT CANNOT SEE A DOUBLED FRAME.** A second copy landing inside the first one's
bounding box leaves these numbers unchanged. card_copies.py is what counts
copies. docs/investigations/the-separator-moves-the-counters-origin.md
"""

import argparse

import numpy as np

import hdmi_capture as cap


def greenness(rgb):
    r, g, b = (rgb[..., i].astype(np.int16) for i in range(3))
    return (g > 60) & (g - r > 40) & (g - b > 40)


def edges(mask, floor):
    cols = mask.sum(axis=0)
    rows = mask.sum(axis=1)
    lit_c = np.flatnonzero(cols >= floor)
    lit_r = np.flatnonzero(rows >= floor)
    if not lit_c.size or not lit_r.size:
        return None
    return dict(left=int(lit_c[0]), right=int(lit_c[-1]),
                top=int(lit_r[0]), bottom=int(lit_r[-1]))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--frames", type=int, default=3)
    p.add_argument("--floor", type=int, default=300,
                   help="pixels of green a row/column needs to count as the frame")
    p.add_argument("--png", default=None)
    args = p.parse_args()

    rgb = cap.frames(args.frames, cap.device())
    for i in range(rgb.shape[0]):
        e = edges(greenness(rgb[i]), args.floor)
        print(f"  frame {i}: " + ("no green frame found" if e is None else
              f"green left {e['left']:4} right {e['right']:4} "
              f"top {e['top']:4} bottom {e['bottom']:4}  "
              f"span {e['right'] - e['left'] + 1}x{e['bottom'] - e['top'] + 1}"))
    if args.png:
        import subprocess
        subprocess.run(["ffmpeg", "-hide_banner", "-v", "error", "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-video_size", "1920x1080", "-i", "-",
                        "-frames:v", "1", "-update", "1", "-y", args.png],
                       input=rgb[0].tobytes(), check=True)
        print(f"  wrote {args.png}")


if __name__ == "__main__":
    main()
