#!/usr/bin/env python3
"""Whether the emitted frame is still the card, and at what horizontal scale.

    python3 tools/gbsc-pro-hwtest/picture_match.py --save ref.npy
    python3 tools/gbsc-pro-hwtest/picture_match.py --ref ref.npy

**A DUPLICATED FRAME IS THE TRAP THIS EXISTS FOR.** A counter that divides the
real line by an exact factor repeats the picture across the line and holds
perfectly still, so every measure that asks whether the frame MOVES calls it
clean: frame-to-frame displacement is zero, row-to-row slip is zero, and the
correlation between neighbouring rows is as high as a working picture's,
because the repeated content is real content. Three separate readings of the
scaling-down block were taken on duplicated frames and recorded as the block
working.

What separates them is asking whether the frame is the REFERENCE, allowing for
scale. A correct picture matches its reference at some scale and a duplicated
one matches nothing, because its column profile carries two copies where the
reference has one. Measured on `vga` into 1080p: a solved frame against a
reference of the same state scores 0.99, and every broken state measured --
duplicated, sheared, or shredded to noise -- scores at or below 0.85.

**THE SCALE IS PART OF THE ANSWER, NOT A NUISANCE.** A frame that matches at
scale 1.0 is the same framing; one that matches at 1.16 is the same picture
magnified, which is what a capture window narrower than the line looks like.
Reporting the correlation alone cannot tell those apart, and they are opposite
diagnoses.

**IT IS A COMPARISON, SO BOTH FRAMES MUST BE THE SAME SOURCE MODE AND PATTERN.**
The reference is captured from a state known to be right, with the animation
off, and a new one is needed after any source mode change.
`docs/bench-output-capture.md` is the capture; `docs/scaling-down-path.md` is
what this was built to judge.
"""

import argparse
import sys

import numpy as np

import hdmi_capture as hc


# Columns at each edge left out of `detail`, which a single cliff would
# otherwise dominate.
EDGE = 8


def profile(frame):
    """The frame's column profile, mean removed.

    Column rather than row because the horizontal axis is the one every
    scaling-down question moves, and collapsing the rows keeps a feature that
    spans only part of the frame's height.
    """
    p = frame.mean(axis=0)
    return p - p.mean()


def _best_shift(a, b):
    """Correlation at the best horizontal shift, normalised to [-1, 1]."""
    n = max(a.size, b.size) * 2
    cc = np.fft.irfft(np.fft.rfft(a, n=n) * np.conj(np.fft.rfft(b, n=n)), n=n)
    norm = np.sqrt((a * a).sum() * (b * b).sum())
    if norm < 1e-6:
        return 0.0, 0
    k = int(np.argmax(cc))
    return float(cc[k] / norm), k if k <= n // 2 else k - n


def match(frame, ref, scales=None):
    """Best (correlation, scale, shift) of `frame` against `ref`.

    `scale` above one means `frame` shows the reference's content magnified.
    """
    if scales is None:
        scales = np.arange(0.55, 1.85, 0.005)
    pf, pr = profile(frame), profile(ref)
    x = np.arange(pr.size)
    best = (-1.0, 1.0, 0)
    for scale in scales:
        # The reference resampled onto the FRAME's own columns, so the two are
        # the same length whatever the scale. Resampling to the scaled length
        # instead leaves a longer array whose extra energy penalises the very
        # magnification being searched for.
        resampled = np.interp(np.arange(pf.size) / scale, x, pr,
                              left=0.0, right=0.0)
        corr, shift = _best_shift(pf, resampled)
        if corr > best[0]:
            best = (corr, float(scale), shift)
    return best


def detail(frame):
    """RMS of the horizontal second difference -- content near Nyquist.

    A ratio that filters without moving the picture shows here and nowhere in
    `match`, which is scale-invariant by construction.

    The outermost columns are left out: the dongle puts a bright column at the
    very edge of some frames, and one such cliff outweighs the whole line.
    """
    inner = frame[:, EDGE:-EDGE]
    d = inner[:, 2:] - 2 * inner[:, 1:-1] + inner[:, :-2]
    return float(np.sqrt((d * d).mean()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", help="reference .npy to judge against")
    ap.add_argument("--save", help="write this state's luma here as a reference")
    ap.add_argument("--png")
    ap.add_argument("--frames", type=int, default=2)
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    rgb = hc.frames(args.frames, hc.device())
    frame = hc.luma(rgb)[0]
    if args.png:
        hc.write_png(args.png, rgb[0])
    if args.save:
        np.save(args.save, frame)

    edges = hc.borders(frame)
    print(f"label   {args.label}")
    print(f"borders left {edges['left']} right {edges['right']} "
          f"top {edges['top']} bottom {edges['bottom']}")
    print(f"detail  {detail(frame):.2f}")
    if args.ref:
        corr, scale, shift = match(frame, np.load(args.ref))
        print(f"match   corr {corr:.3f} scale {scale:.3f} shift {shift}")
    elif not args.save:
        sys.exit("nothing to do: pass --ref to judge or --save to record one")


if __name__ == "__main__":
    main()
