#!/usr/bin/env python3
"""Read the picture's edges and the source's border off a clip of the test card.

    python3 tools/gbsc-pro-hwtest/flash_edges.py shot.mp4 [more.mp4 ...]
    tv-snap -q -C 4 -o /tmp/landing.mp4     # what to feed it

For the pass-through path, where the encoder places the picture and the scaler's
registers do not say where it landed. The card's own liveness animation carries
both boundaries, so a landing is measured with NOTHING on the scaler touched --
which matters, because a write provokes a re-lock and a re-lock is the thing
being measured.
../../docs/investigations/the-encoder-tunes-the-left-edge-in-pass-through.md

PatLib flips the screen border cyan/magenta and four corner blocks yellow/white
off one phase. `R-G` swings on cyan/magenta and is zero on yellow/white; `B`
does the reverse. So one clip carries the border and the picture's own edge
separately.

THE CORNER BLOCKS ARE THE ACCEPTANCE TEST. They are `CW% DIV 3` by `CH% DIV 3`
at all four corners, identical by construction, so a picture running off either
end of the panel returns that corner narrower. Equal widths mean nothing is
clipped, and it needs no blanking change and no column-to-sample mapping.

THE SOURCE'S BORDER HAS TO BE ON (`BORDER ON` to ModeServ). That flash is the
only signal strong enough to lock the phase to; with it off the corner blocks
alone read at about one grey level through the bench camera and the phase locks
onto noise, which returns a profile spread across the whole frame rather than
two blocks.

Phase-locked averaging is what makes a three grey level flip readable: splitting
the frames by phase and averaging each half takes the noise floor to about 0.05
levels. A frame difference cannot find it, and a threshold on one photograph
finds the bezel instead.
"""
import argparse
import subprocess

import numpy as np

TopBand = (0.05, 0.25)
MidBand = (0.20, 0.80)

# How far beyond the picture's edge the border is read. Chroma subsampling
# leaves the corner block in the border channel, so a window that reaches
# inboard counts that crosstalk and reports a border that is switched off.
BorderMargin = 6
BorderReach = 200

# The flip is even over any whole number of periods, so a lopsided split means
# the phase locked onto a transient instead -- a re-lock settling, or the sink's
# own banner. The profile then describes that transient, and reads at tens of
# levels where the flip reads at ones.
MinPhaseShare = 0.35


def frames(path):
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height", "-of", "csv=p=0:s=x", path],
        capture_output=True, text=True, check=True).stdout.strip()
    width, height = (int(v) for v in probe.split("x"))
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-vf", "format=rgb24",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True).stdout
    count = len(raw) // (width * height * 3)
    return np.frombuffer(raw[:count * width * height * 3],
                         dtype=np.uint8).reshape(count, height, width, 3).astype(float)


def phase(volume, band):
    """Which half of the flip each frame belongs to, from whichever column
    flickers hardest -- so nothing has to be known about where the animation is
    before it is found."""
    height = volume.shape[1]
    rows = volume[:, int(height * band[0]):int(height * band[1]), :].mean(axis=1)
    series = rows - rows.mean(axis=0)
    return series[:, int(np.argmax(series.std(axis=0)))] > 0


def phase_is_usable(hot, cold):
    total = hot + cold
    return total > 0 and min(hot, cold) / total >= MinPhaseShare


def locked(volume, hot, band):
    """The per-column difference between the two phases."""
    height = volume.shape[1]
    rows = volume[:, int(height * band[0]):int(height * band[1]), :].mean(axis=1)
    return rows[hot].mean(axis=0) - rows[~hot].mean(axis=0)


def half_amplitude(profile, lo, hi, rising, floor=0.35, run=8):
    """The OUTER edge of the outermost sustained block in [lo,hi), to better
    than a column.

    Outermost and sustained, rather than the first crossing or the tallest
    column: the frame also holds the panel's own edge ramp outboard of the
    picture and other flashing content inboard of it, and either will be taken
    by a scan that stops at the first thing it meets or anchors on a peak.
    """
    window = profile[lo:hi]
    if window.size == 0 or window.max() < floor:
        return None
    half = window.max() / 2.0
    above = window >= half

    blocks = []
    start = None
    for i, on in enumerate(above):
        if on and start is None:
            start = i
        elif not on and start is not None:
            if i - start >= run:
                blocks.append((start, i - 1))
            start = None
    if start is not None and window.size - start >= run:
        blocks.append((start, window.size - 1))
    if not blocks:
        return None

    if rising:
        edge = blocks[0][0]
        if edge == 0:
            return lo
        low, high = window[edge - 1], window[edge]
        return lo + edge - 1 + (half - low) / (high - low)

    edge = blocks[-1][1]
    if edge >= window.size - 1:
        return lo + window.size - 1
    low, high = window[edge + 1], window[edge]
    return lo + edge + (high - half) / (high - low)


def picture_edges(corner, split=None):
    """The outer edge of each corner block, which is the picture's own edge."""
    split = split if split is not None else corner.size // 2
    return (half_amplitude(corner, 0, split, rising=True),
            half_amplitude(corner, split, corner.size, rising=False))


def border_peaks(border, left, right, margin=BorderMargin, reach=BorderReach):
    """The border flash, read only BEYOND the picture on each side."""
    peaks = {}
    if left is None:
        peaks["left"] = 0.0
    else:
        stop = int(left) - margin
        peaks["left"] = float(np.max(np.abs(border[max(0, stop - reach):max(0, stop)]))) \
            if stop > 0 else 0.0
    if right is None:
        peaks["right"] = 0.0
    else:
        start = int(right) + margin + 1
        peaks["right"] = float(np.max(np.abs(border[start:start + reach]))) \
            if start < border.size else 0.0
    return peaks


def read(path):
    volume = frames(path)
    corner_channel = volume[:, :, :, 2]
    border_channel = volume[:, :, :, 0] - volume[:, :, :, 1]

    hot = phase(corner_channel, TopBand)
    corner = locked(corner_channel, hot, TopBand)
    border = locked(border_channel, hot, MidBand)

    usable = phase_is_usable(int(hot.sum()), int((~hot).sum()))
    left, right = picture_edges(corner) if usable else (None, None)
    return {"path": path, "frames": len(volume), "usable": usable,
            "phase": (int(hot.sum()), int((~hot).sum())),
            "noise": float(np.std(corner[700:900])),
            "left": left, "right": right,
            "width": None if left is None or right is None else right - left,
            "border": border_peaks(border, left, right)}


def show(result):
    def column(value):
        return "  --  " if value is None else f"{value:6.1f}"
    print(f"{result['path'].split('/')[-1]}   {result['frames']} frames  "
          f"phase {result['phase'][0]}/{result['phase'][1]}  "
          f"noise {result['noise']:.2f}"
          + ("" if result["usable"] else "   <- LOPSIDED PHASE, the flip was not caught"))
    if not result["usable"]:
        return
    print(f"  picture  left {column(result['left'])}   right {column(result['right'])}"
          f"   width {column(result['width'])}")
    for side in ("left", "right"):
        peak = result["border"][side]
        print(f"  border {side:5s} {peak:5.2f}"
              + ("   <- on panel" if peak > 1.0 else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("clips", nargs="+")
    for path in parser.parse_args().clips:
        show(read(path))


if __name__ == "__main__":
    main()
