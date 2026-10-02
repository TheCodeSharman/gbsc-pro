#!/usr/bin/env python3
"""Capture the board's HDMI output off the USB dongle and measure the picture.

    python3 tools/gbsc-pro-hwtest/hdmi_capture.py
    python3 tools/gbsc-pro-hwtest/hdmi_capture.py --frames 4 --png /tmp/out.png

Reports the black margin at each edge, which is where the emitted picture
starts and stops. The television is a second view of the same thing; this one
is repeatable to the byte, so two states are comparable without controlling the
room.

An all-black capture is a board state until the board has been ruled out. The
connector is the last resort, not the first: a seated one stays seated.

The device node renumbers whenever the dongle is replugged, so it is found by
USB id rather than named. `docs/bench-output-capture.md` is what the readings
mean and which traps they carry.
"""

import argparse
import glob
import subprocess
import sys

import numpy as np

USB_ID = "345f:2131"
WIDTH, HEIGHT = 1920, 1080

# The dongle emits black frames while its receiver settles, and a one-frame grab
# returns one of them.
WARMUP = 40

# Limited-range black is 16, so a picture has to clear it by enough that sensor
# noise on a black border does not read as content.
BLACK = 24


def device():
    """The dongle's capture node, by USB id."""
    for path in sorted(glob.glob("/dev/video*")):
        info = subprocess.run(["udevadm", "info", "--query=property", "--name", path],
                              capture_output=True, text=True).stdout
        if USB_ID.split(":")[0].upper() in info.upper() and "ID_V4L_CAPABILITIES=:capture:" in info:
            return path
    sys.exit(f"no capture device with USB id {USB_ID} -- is the dongle plugged in?")


def write_png(path, frame):
    """One RGB frame to a PNG. ffmpeg rather than a codec library, because the
    dev shell has no PIL and the raw pipe is already how frames arrive."""
    subprocess.run(["ffmpeg", "-hide_banner", "-v", "error", "-f", "rawvideo",
                    "-pix_fmt", "rgb24",
                    "-video_size", f"{frame.shape[1]}x{frame.shape[0]}",
                    "-i", "-", "-frames:v", "1", "-update", "1", "-y", str(path)],
                   input=frame.tobytes(), check=True)


def frames(count, dev, width=WIDTH, height=HEIGHT, warmup=WARMUP):
    """`count` frames after the warm-up, as (count, height, width, 3) RGB.

    A shorter `warmup` is for a walk that takes a reading per register step and
    never touches the link: the default is sized for a source or an output that
    has just changed, and paying it per step is most of the walk.
    """
    raw = subprocess.run(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "v4l2",
         "-input_format", "yuyv422", "-video_size", f"{width}x{height}", "-i", dev,
         "-vf", f"select=gte(n\\,{warmup})", "-frames:v", str(count),
         "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
        capture_output=True).stdout
    got = len(raw) // (width * height * 3)
    if got == 0:
        sys.exit("no frames captured")
    return np.frombuffer(raw[:got * width * height * 3], np.uint8).reshape(got, height, width, 3)


def luma(rgb):
    return rgb.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)


# A line or column is content when this fraction of it clears `BLACK`. One hot
# pixel is sensor noise; a tenth of the frame is picture.
LIT_FRACTION = 0.01

# And the picture starts where that SUSTAINS for this many lines or columns. The
# dongle puts a narrow bright column at the very edge of some frames, so taking
# the first lit one reported a 313 px bar as a 3 px one -- a break measured as a
# fill, which is the direction that costs a diagnosis.
RUN = 8


def _edge(lit, run=RUN):
    """First index where `run` consecutive entries are lit, else len(lit)."""
    if run <= 1:
        return int(np.argmax(lit)) if lit.any() else len(lit)
    held = np.convolve(lit.astype(np.int32), np.ones(run, np.int32), "valid")
    found = np.flatnonzero(held == run)
    return int(found[0]) if found.size else len(lit)


def borders(frame, threshold=BLACK):
    """Rows and columns at each edge carrying nothing above `threshold`."""
    height, width = frame.shape
    above = frame > threshold
    lit_rows = above.mean(axis=1) > LIT_FRACTION
    lit_cols = above.mean(axis=0) > LIT_FRACTION
    if not lit_rows.any() or not lit_cols.any():
        return dict(left=width, right=0, top=height, bottom=0, width=0, height=0)
    top, bottom = _edge(lit_rows), _edge(lit_rows[::-1])
    left, right = _edge(lit_cols), _edge(lit_cols[::-1])
    if left + right >= width or top + bottom >= height:
        return dict(left=width, right=0, top=height, bottom=0, width=0, height=0)
    return dict(left=left, right=right, top=top, bottom=bottom,
                width=width - left - right, height=height - top - bottom)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--device", default=None, help="override the USB id search")
    parser.add_argument("--png", default=None, help="save the first frame here")
    parser.add_argument("--size", default=f"{WIDTH}x{HEIGHT}")
    args = parser.parse_args()

    width, height = (int(n) for n in args.size.split("x"))
    dev = args.device or device()
    rgb = frames(args.frames, dev, width, height)
    grey = luma(rgb)

    print(f"{dev}  {width}x{height}  {rgb.shape[0]} frame(s)  mean luma {grey.mean():.2f}")
    if grey.max() <= BLACK:
        print("  ALL BLACK -- nothing reached the dongle. ASK THE BOARD FIRST: a block\n"
              "  left held in reset emits nothing while every configuration register\n"
              "  reads correct, so a clean dump is no evidence. s0_46 (SFTRST_*_RSTZ\n"
              "  all 1), s0_45, s0_49, then the console's frame time lock 'out' rate.\n"
              "  An unseated HDMI input does this too and is the LAST thing to check.")
        return 1

    for i in range(rgb.shape[0]):
        edge = borders(grey[i])
        print(f"  frame {i}: black margins  left {edge['left']:4}  right {edge['right']:4}"
              f"  top {edge['top']:4}  bottom {edge['bottom']:4}"
              f"   picture {edge['width']}x{edge['height']}")

    if args.png:
        write_png(args.png, rgb[0])
        print(f"  wrote {args.png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
