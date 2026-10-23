#!/usr/bin/env python3
"""Which rows of the emitted frame MOVE, measured as temporal spread per band.

    python3 tools/gbsc-pro-hwtest/frame_bands.py --frames 160

**A DISPLACEMENT SCORE CANNOT SEE THIS FAULT AND A BAND SPREAD CAN.**
`picture_stability.judge` correlates row profiles and reports a lag, so it
finds lag 0 on whatever part of the frame still correlates and scores a frame
with a hundred disturbed rows as perfectly still -- measured at 0 of 120 frames
displaced on a picture that was visibly jumping. A frame difference is no
better: it measures displacement MULTIPLIED BY LOCAL CONTRAST, so it reads zero
across flat colour.

What survives is the temporal spread of each row band's own mean. A band that
is being rewritten differently every frame moves, whatever its content, and a
band that is steady reads the sensor floor. Measured on an unlocked sampling
clock: rows 0..319 at sd 2.6..7.5 against a median of 0.45 over the frame and
0.02 at the bottom, and 1.78 at the top once recovered.

Capture at FULL resolution. Downscaling averages emitted rows together and
cancels the thing being measured. ../../docs/bench-output-capture.md
"""

import argparse
import subprocess
import sys

import numpy as np

import hdmi_capture

WIDTH, HEIGHT = 1920, 1080

# The dongle emits black frames while its receiver settles.
WARMUP = 40

# Row bands over the frame. 27 of 40 rows each: fine enough to localise a
# disturbance to a tenth of the frame, coarse enough that one band's mean is
# steady to a hundredth of a grey level on a still picture.
BANDS = 27

# The picture's interior, keeping the frame's own edges out of every band's
# mean -- the border is where our blanking meets the sink's and it moves for
# reasons that are not the picture's.
INTERIOR = (240, 1680)

# How much more than the frame's median a band must spread to be called moving.
# The two states measured are a factor of ten apart, so nothing sits near this.
MOVING_MULTIPLE = 5.0


def band_means(frame, bands=BANDS, interior=INTERIOR):
    """Each band's mean luma over the picture's interior."""
    inside = frame[:, interior[0]:interior[1]]
    rows = inside.shape[0] // bands
    return inside[:rows * bands].reshape(bands, rows, -1).mean(axis=(1, 2))


def band_spread(profiles):
    """Each band's temporal spread across the frames measured."""
    return np.asarray(profiles).std(axis=0)


def moving_bands(spread, multiple=MOVING_MULTIPLE):
    """Which bands spread more than `multiple` times the frame's median."""
    spread = np.asarray(spread)
    floor = float(np.median(spread))
    return [i for i, s in enumerate(spread) if s > multiple * floor]


def capture(frames, warmup=WARMUP):
    """Band means per frame, straight off the dongle at full resolution."""
    reader = subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-loglevel", "quiet", "-f", "v4l2",
         "-input_format", "mjpeg", "-video_size", f"{WIDTH}x{HEIGHT}",
         "-i", hdmi_capture.device(), "-pix_fmt", "gray", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE)
    try:
        taken = []
        for i in range(warmup + frames):
            raw = reader.stdout.read(WIDTH * HEIGHT)
            if len(raw) < WIDTH * HEIGHT:
                break
            if i >= warmup:
                taken.append(band_means(
                    np.frombuffer(raw, np.uint8).reshape(HEIGHT, WIDTH)))
        return taken
    finally:
        reader.kill()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=160)
    parser.add_argument("--quiet", action="store_true",
                        help="the verdict only, no per-band table")
    args = parser.parse_args()

    taken = capture(args.frames)
    if not taken:
        sys.exit("no frames -- see docs/bench-output-capture.md before the connector")

    spread = band_spread(taken)
    moving = moving_bands(spread)
    rows = HEIGHT // BANDS

    if not args.quiet:
        print(f"{'band':>4}  {'rows':<11} {'mean luma':>9}  {'spread':>8}")
        for i, s in enumerate(spread):
            flag = "  <<< MOVING" if i in moving else ""
            print(f"{i:4}  {i * rows:4}-{i * rows + rows - 1:<6}"
                  f"{np.asarray(taken)[:, i].mean():9.1f}  {s:8.3f}{flag}")

    print(f"\n{len(taken)} frames, median band spread {np.median(spread):.3f}, "
          f"worst {spread.max():.3f}")
    if moving:
        print("moving: rows " + ", ".join(
            f"{i * rows}-{i * rows + rows - 1}" for i in moving))
    else:
        print("no band moves more than "
              f"{MOVING_MULTIPLE:g}x the median: the frame is still")
    return 1 if moving else 0


if __name__ == "__main__":
    sys.exit(main())
