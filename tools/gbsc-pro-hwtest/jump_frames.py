#!/usr/bin/env python3
"""Save the FRAME a large vertical jump lands on, not just its share.

    python3 tools/gbsc-pro-hwtest/jump_frames.py --out /tmp/jump
    python3 tools/gbsc-pro-hwtest/jump_frames.py --seconds 600 --rows 50

`picture_stability.py` reports how often the picture moved and how far, as a
correlation lag per frame. **A LAG IS ALL IT CAN REPORT**, and on this board
the frames it scores are not displaced pictures at all: they carry the tail of
one capture, a band of black, and the head of the next. A share cannot tell
those apart from a picture that moved, and a frame can.

So this waits for a jump of at least `--rows` and writes out the frame it
lands on together with the frame immediately BEFORE it, which is still. It
keeps the full 1080 rows -- the jump is vertical -- and narrows the capture
horizontally, because the structure is legible at a quarter of the width and
the full frame at 60 Hz is more than the pipe carries without dropping.

**ONE SAVE PER EVENT, NOT PER FRAME.** A jump holds for four to six frames, so
saving every displaced frame spends the whole budget on one event.

**THE REFERENCE IS A SLIDING MEDIAN, NOT THE FIRST FRAME.** The engine
re-places the picture when it re-solves, and a fixed reference reads everything
after such a move as displaced for ever. Every run prints the same frames
judged against the median of the WHOLE run beside it, because a run reading
zero is otherwise indistinguishable from a tool that stopped detecting.

docs/investigations/the-large-composite-jump-is-a-torn-frame.md
"""

import argparse
import collections
import subprocess
import sys
import time

import numpy as np

import hdmi_capture
import picture_stability

# Rows a jump has to reach to be saved. The line-or-two roll is 2 to 8 rows and
# is closed; what is open is the 100..250-row jump.
LARGE_ROWS = 50

# Frames the sliding reference holds. At 60 a second this is ten seconds, and
# the jumps come at one per twenty, so no run of them can move the median.
REFERENCE_FRAMES = 600

# Frames between recomputes of that median, which costs more than a profile.
REFERENCE_EVERY = 60

WIDTH = 480
HEIGHT = 1080


def stream(dev, width, height):
    return subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "v4l2",
         "-input_format", "yuyv422", "-video_size", "1920x1080", "-i", dev,
         "-vf", f"scale={width}:{height}:flags=area", "-pix_fmt", "gray",
         "-fps_mode", "passthrough", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def read_frame(proc, width, height):
    want, buf = width * height, b""
    while len(buf) < want:
        chunk = proc.stdout.read(want - len(buf))
        if not chunk:
            return None
        buf += chunk
    return np.frombuffer(buf, np.uint8).reshape(height, width)


def is_onset(offset, previous, rows):
    """Whether this frame BEGINS a tear, `previous` being the offset before it.

    A frame with nothing to correlate carries no offset and so cannot end a
    run: it leaves `previous` where it was.
    """
    if offset is None:
        return False
    return abs(offset) >= rows and abs(previous) < rows


def onsets(offsets, rows):
    """The indices `is_onset` fires on, over a whole sequence."""
    found, previous = [], 0
    for index, offset in enumerate(offsets):
        if is_onset(offset, previous, rows):
            found.append(index)
        if offset is not None:
            previous = offset
    return found


def against_the_whole_run(profiles):
    """The same frames judged the way `picture_stability` judges a clip."""
    whole = picture_stability.reference(profiles)
    seen = collections.Counter(
        o for o in (picture_stability.displacement(p, whole) for p in profiles) if o)
    return dict(sorted(seen.items()))


def save(path, frame):
    hdmi_capture.write_png(path, np.repeat(frame[:, :, None], 3, axis=2))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default="/tmp/jump",
                        help="prefix for the PNGs written")
    parser.add_argument("--seconds", type=float, default=300.0)
    parser.add_argument("--rows", type=int, default=LARGE_ROWS)
    parser.add_argument("--keep", type=int, default=6,
                        help="how many events to save before stopping")
    parser.add_argument("--width", type=int, default=WIDTH)
    args = parser.parse_args()

    dev = hdmi_capture.device()
    proc = stream(dev, args.width, HEIGHT)
    held = collections.deque(maxlen=REFERENCE_FRAMES)
    reference, saved, seen, dropped = None, 0, 0, 0
    started, previous, was = None, None, 0
    offsets = collections.Counter()
    kept = []
    print(f"device {dev}  {args.width}x{HEIGHT}  jump >= {args.rows} rows",
          flush=True)
    try:
        while True:
            frame = read_frame(proc, args.width, HEIGHT)
            if frame is None:
                break
            at = time.monotonic()
            if started is None:
                started = at
            if at - started >= args.seconds or saved >= args.keep:
                break
            profile = frame.astype(np.float32).mean(axis=1)
            held.append(profile)
            seen += 1
            if len(held) < REFERENCE_EVERY:
                continue
            if reference is None or seen % REFERENCE_EVERY == 0:
                reference = picture_stability.reference(held)
            offset = picture_stability.displacement(profile, reference)
            if offset is None:
                dropped += 1
                continue
            if is_onset(offset, was, args.rows):
                saved += 1
                stem = f"{args.out}-{saved:02d}"
                if previous is not None:
                    save(f"{stem}-before.png", previous)
                save(f"{stem}-offset{offset:+d}.png", frame)
                print(f"{at - started:7.1f}  offset {offset:+5d} rows -> "
                      f"{stem}-offset{offset:+d}.png", flush=True)
            kept.append(profile)
            if offset:
                offsets[offset] += 1
            was, previous = offset, frame
    finally:
        proc.kill()
    print(f"{seen} frames, {dropped} with nothing to correlate, "
          f"{saved} jumps saved", flush=True)
    print(f"every offset seen: {dict(sorted(offsets.items()))}", flush=True)

    print(f"whole-clip median: {against_the_whole_run(kept)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
