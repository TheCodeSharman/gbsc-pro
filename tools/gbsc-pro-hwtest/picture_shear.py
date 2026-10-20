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


def profiles(clip, w, h, bands):
    """Each frame as `bands` column profiles, mean-removed."""
    raw = subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-v", "error", "-i", clip,
         "-vf", f"scale={w}:{h}", "-pix_fmt", "gray", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE)
    edges = [round(i * h / bands) for i in range(bands + 1)]
    size = w * h
    out = []
    while True:
        buf = raw.stdout.read(size)
        if len(buf) < size:
            break
        f = np.frombuffer(buf, np.uint8).reshape(h, w).astype(np.float32)
        out.append(np.array([f[edges[i]:edges[i + 1]].mean(axis=0)
                             for i in range(bands)]))
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
