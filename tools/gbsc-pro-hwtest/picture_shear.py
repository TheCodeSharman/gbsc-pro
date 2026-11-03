#!/usr/bin/env python3
"""Whether the emitted picture SHEARS -- each line displaced further than the
one above -- off a recorded clip or the dongle.

    python3 tools/gbsc-pro-hwtest/picture_shear.py clip.mp4
    python3 tools/gbsc-pro-hwtest/picture_shear.py clip.mp4 --rows

`picture_stability.py` collapses each frame to a ROW profile, so it reports
whole-line vertical jumps and is largely blind to a horizontal fault. This is
the other axis: each frame is split into horizontal bands, each band gets its
own circular cross-correlation against the clip's median frame, and a
displacement that CHANGES down the frame is the shear.

**ONE DISPLACEMENT PER FRAME CANNOT SEE A SHEAR.** A single column profile
averages a top displaced one way and a bottom displaced the other into
something neither band shows.

**THE CORRELATION WRAPS AT HALF THE WIDTH**, so a band reading +832 may be
-1088. `--rows` resolves per row group against the frame BEFORE the event,
where the ramp is readable directly; the band table is for detecting an event,
not for measuring one.

The reference is the clip's median frame, so no clean capture is needed -- but
a picture displaced in most frames therefore reads still.
"""

import argparse
import subprocess
import sys

import numpy as np

WIDTH, HEIGHT = 1920, 1080


# Enough variation across a band's profile for a correlation peak to mean
# anything. Below it the peak is noise, and a frame of flat colour would score
# as perfectly still -- the same error as reading a row-profile lag of 0 off a
# band of unwritten memory.
CONTRAST_FLOOR = 2.0

# What the clip is scored at: eight bands down the frame, each profile 480
# columns wide, so one unit is four emitted pixels at 1920.
BANDS = 8
PROFILE_WIDTH = 480


def band_profiles(frames, bands, width):
    """Each frame as `bands` column profiles `width` wide.

    Takes what hdmi_capture.frames() answers -- (count, height, width, 3) RGB
    -- or greyscale of the same shape without the last axis.
    """
    frames = np.asarray(frames)
    if frames.ndim == 4:
        frames = frames.astype(np.float32) @ np.array([0.299, 0.587, 0.114],
                                                      np.float32)
    out = []
    for frame in frames.astype(np.float32):
        h, w = frame.shape
        binned = frame.reshape(h, width, w // width).mean(axis=2)
        edges = [round(i * h / bands) for i in range(bands + 1)]
        out.append(np.array([binned[edges[i]:edges[i + 1]].mean(axis=0)
                             for i in range(bands)]))
    return np.array(out)


def measure_clip(clip, bands=BANDS, width=PROFILE_WIDTH):
    """How far the picture in `clip` SHEARS, as three numbers.

    `pairs` is how many ADJACENT bands both carry enough contrast to
    correlate. 0 means the clip says nothing, and the reading has to be
    discarded rather than read as a still picture.

    `displaced` is the share of frames carrying any band away from the clip's
    own median frame. A whole picture moved together displaces every band
    alike, which is a pan and not a shear, so this alone is not the verdict.

    `rough` is the WORST frame's share of adjacent pairs whose displacements
    DIFFER, which is the shear itself. The worst frame rather than the mean,
    because the artefact is not on every frame.
    """
    profiled = band_profiles(clip, bands, width)
    if len(profiled) < 3:
        return {"pairs": 0, "displaced": 0.0, "rough": 0.0}

    middle = np.median(profiled, axis=0)
    live = [b for b in range(bands) if middle[b].std() >= CONTRAST_FLOOR]
    adjacent = [i for i in range(len(live) - 1) if live[i + 1] == live[i] + 1]
    if not adjacent:
        return {"pairs": 0, "displaced": 0.0, "rough": 0.0}

    reference = [np.fft.rfft(middle[b] - middle[b].mean()) for b in live]
    moved = np.array([[displacement(frame[b], reference[i], width)
                       for i, b in enumerate(live)]
                      for frame in profiled])

    steps = np.array([[row[i + 1] != row[i] for i in adjacent] for row in moved])
    return {"pairs": len(adjacent),
            "displaced": float(np.mean((moved != 0).any(axis=1))),
            "rough": float(steps.sum(axis=1).max()) / len(adjacent)}


def profiles(clip, w, h, bands):
    """Each frame of the clip FILE as `bands` column profiles, mean-removed."""
    raw = subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-v", "error", "-i", clip,
         "-vf", f"scale={w}:{h}", "-pix_fmt", "gray", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE)
    size = w * h
    out = []
    while True:
        buf = raw.stdout.read(size)
        if len(buf) < size:
            break
        out.append(band_profiles(
            [np.frombuffer(buf, np.uint8).reshape(h, w)], bands, w)[0])
    raw.stdout.close()
    raw.wait()
    return np.array(out)


def displacement(a, reference, width):
    a = a - a.mean()
    corr = np.fft.irfft(np.fft.rfft(a) * np.conj(reference), n=width)
    k = int(np.argmax(corr))
    return k - width if k > width // 2 else k


def events(flagged, join=4):
    runs = []
    if not len(flagged):
        return runs
    run = [flagged[0]]
    for i in flagged[1:]:
        if i - run[-1] <= join:
            run.append(i)
        else:
            runs.append(run)
            run = [i]
    runs.append(run)
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("clip")
    ap.add_argument("--bands", type=int, default=8)
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--rows", action="store_true",
                    help="resolve the largest event per row group")
    args = ap.parse_args()

    w = args.width
    h = HEIGHT // (WIDTH // w)
    scale = WIDTH // w
    P = profiles(args.clip, w, h, args.bands)
    if len(P) < 10:
        sys.exit("too few frames")
    print(f"{len(P)} frames, {w}x{h}, 1 unit = {scale} emitted px")

    ref = [np.fft.rfft(m - m.mean()) for m in np.median(P, axis=0)]
    disp = np.array([[displacement(P[f][b], ref[b], w) for b in range(args.bands)]
                     for f in range(len(P))])

    flagged = np.flatnonzero((disp != 0).any(axis=1))
    runs = events(flagged)
    print(f"frames with a displaced band: {len(flagged)} of {len(P)} "
          f"({100.0 * len(flagged) / len(P):.2f}%)")
    print(f"events: {len(runs)}")
    for run in runs[:20]:
        print(f"\n  frames {run[0]}..{run[-1]}  ({len(run)} frames)")
        print("    frame   band displacement, emitted px (top -> bottom)")
        for f in range(max(0, run[0] - 1), min(len(P), run[-1] + 2)):
            row = " ".join(f"{v * scale:+6d}" for v in disp[f])
            print(f"    {f:6d}  {row}")

    if args.rows and runs:
        big = max(runs, key=len)
        rows_per_group = 8
        R = profiles(args.clip, 960, HEIGHT, HEIGHT // rows_per_group)
        base = [np.fft.rfft(g - g.mean()) for g in R[max(0, big[0] - 2)]]
        print(f"\nper row group through frames {big[0]}..{big[-1]}, "
              f"against frame {max(0, big[0] - 2)}")
        for f in range(max(0, big[0] - 1), min(len(R), big[-1] + 2)):
            d = [displacement(R[f][g], base[g], 960) * 2
                 for g in range(0, len(base), 4)]
            print(f"  frame {f:5d}: " + " ".join(f"{v:5d}" for v in d))


if __name__ == "__main__":
    main()
