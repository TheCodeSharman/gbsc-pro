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


def frames(count, dev, width=WIDTH, height=HEIGHT):
    """`count` frames after the warm-up, as (count, height, width, 3) RGB."""
    raw = subprocess.run(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "v4l2",
         "-input_format", "yuyv422", "-video_size", f"{width}x{height}", "-i", dev,
         "-vf", f"select=gte(n\\,{WARMUP})", "-frames:v", str(count),
         "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
        capture_output=True).stdout
    got = len(raw) // (width * height * 3)
    if got == 0:
        sys.exit("no frames captured")
    return np.frombuffer(raw[:got * width * height * 3], np.uint8).reshape(got, height, width, 3)


def luma(rgb):
    return rgb.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)


def borders(frame, threshold=BLACK):
    """Rows and columns at each edge carrying nothing above `threshold`."""
    height, width = frame.shape
    lit_rows = (frame > threshold).any(axis=1)
    lit_cols = (frame > threshold).any(axis=0)
    if not lit_rows.any():
        return dict(left=width, right=0, top=height, bottom=0, width=0, height=0)
    top, bottom = int(np.argmax(lit_rows)), int(np.argmax(lit_rows[::-1]))
    left, right = int(np.argmax(lit_cols)), int(np.argmax(lit_cols[::-1]))
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
        subprocess.run(["ffmpeg", "-hide_banner", "-v", "error", "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-video_size", f"{width}x{height}",
                        "-i", "-", "-frames:v", "1", "-update", "1", "-y", args.png],
                       input=rgb[0].tobytes(), check=True)
        print(f"  wrote {args.png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
