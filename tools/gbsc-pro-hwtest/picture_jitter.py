#!/usr/bin/env python3
"""How much the emitted picture moves between frames, on both axes.

    python3 tools/gbsc-pro-hwtest/picture_jitter.py
    python3 tools/gbsc-pro-hwtest/picture_jitter.py --frames 120 --bands 4

Each frame's column profile is cross-correlated against the first frame's, in
bands down the picture so a shear cannot average away, and the row profile the
same way across it. On an untouched unit the result repeats to a few
thousandths of a dongle column, so tenths of a column are signal.

A shimmer the eye reads as flicker is a fraction of an output pixel, so the
number has to come from the emitted frame rather than from a photograph: what
moves is which source samples reach the screen, and that shows up as edge
intensities shifting, not as the picture panning.

Two things this exists to avoid. **A one-axis instrument is blind to the other
axis**, and reporting a horizontal figure while the picture wobbles vertically
reads as a healthy unit. And **a displacement is not the only movement** -- the
picture's own extent can change between frames, which no correlation reports,
so each frame's lit edges are printed beside the displacement.

`docs/investigations/the-sampling-phase-cannot-reach-the-shimmer.md` is what it
has measured. `docs/bench-output-capture.md` is the dongle itself.
"""

import argparse
import sys

import numpy as np

from hdmi_capture import BLACK, device, frames, luma

BANDS = 4
BAND_SPAN = 100
MAXLAG = 3


def band_starts(lo, hi, bands=BANDS, span=BAND_SPAN):
    """Evenly spread band origins, the last one ending on `hi`."""
    if bands < 2:
        return [lo]
    step = (hi - lo - span) / (bands - 1)
    return [int(lo + round(i * step)) for i in range(bands)]


def displacement(reference, current, maxlag=MAXLAG):
    """How far `current` sits to the right of `reference`, and whether the
    correlation peak was bracketed.

    An unbracketed peak means the movement ran past the search rather than that
    it was small, and the two are indistinguishable in the value alone.
    """
    reference = reference - reference.mean()
    current = current - current.mean()
    length = len(reference)
    lags = np.arange(-maxlag, maxlag + 1)
    scores = np.empty(len(lags))
    for index, lag in enumerate(lags):
        if lag < 0:
            a, b = reference[-lag:], current[:length + lag]
        elif lag > 0:
            a, b = reference[:length - lag], current[lag:]
        else:
            a, b = reference, current
        scores[index] = (a * b).sum() / len(a)

    peak = int(np.argmax(scores))
    if peak == 0 or peak == len(scores) - 1:
        return float(lags[peak]), False
    before, at, after = scores[peak - 1], scores[peak], scores[peak + 1]
    curvature = before - 2 * at + after
    offset = 0.5 * (before - after) / curvature if curvature else 0.0
    return float(lags[peak] + offset), True


def shifted(profile, delta):
    """`profile` moved `delta` columns right, by a phase ramp rather than by
    interpolation -- resampling would smooth the profile and so change the very
    peak shape the gain is being measured from."""
    # Mirrored first, so the sequence the transform sees joins to itself. A
    # profile whose ends differ wraps as a step, and the ringing off that step
    # changes the peak shape the gain is being read from -- measured as a gain
    # three times the true one on step-edge content.
    mirrored = np.concatenate([profile, profile[::-1]])
    spectrum = np.fft.rfft(mirrored)
    ramp = np.exp(-2j * np.pi * np.fft.rfftfreq(len(mirrored)) * delta)
    return np.fft.irfft(spectrum * ramp, len(mirrored))[:len(profile)]


def gain_for(profile, probe=0.25):
    """What this profile's own content makes the estimator report per column.

    A parabolic fit over three correlation samples is biased by the shape of the
    peak, and that shape is the content's spectrum: measured 1.24 on a sum of
    sinusoids and 0.435 on step edges. Left uncorrected the reading is in
    arbitrary units that change with the picture, so two framings of the same
    source are not comparable -- which is the whole use.
    """
    measured, bracketed = displacement(profile, shifted(profile, probe))
    if not bracketed or measured == 0.0:
        return 1.0
    return measured / probe


def lit_box(grey, threshold=BLACK):
    """(left, right, top, bottom) of everything above `threshold`."""
    rows = (grey > threshold).any(axis=1)
    columns = (grey > threshold).any(axis=0)
    if not rows.any():
        return None
    return (int(np.argmax(columns)), grey.shape[1] - int(np.argmax(columns[::-1])),
            int(np.argmax(rows)), grey.shape[0] - int(np.argmax(rows[::-1])))


def axis_moves(grey, profiles):
    """Per frame, per band, how far that band moved from the first frame's, in
    columns -- each band divided by the gain its own content gives."""
    count, bands = len(grey), len(profiles[0])
    moves = np.empty((count, bands))
    unbracketed = 0
    gains = [gain_for(profiles[0][band]) for band in range(bands)]
    for frame in range(count):
        for band in range(bands):
            raw, ok = displacement(profiles[0][band], profiles[frame][band])
            moves[frame, band] = raw / gains[band]
            unbracketed += not ok
    return moves, unbracketed


def measure(grey, bands=BANDS, span=BAND_SPAN):
    box = lit_box(grey[0])
    if box is None:
        return None
    left, right, top, bottom = box
    rows, columns = band_starts(top, bottom, bands, span), band_starts(left, right, bands, span)

    horizontal = [[frame[t:t + span, left:right].mean(axis=0) for t in rows]
                  for frame in grey]
    vertical = [[frame[top:bottom, c:c + span].mean(axis=1) for c in columns]
                for frame in grey]

    h_moves, h_missed = axis_moves(grey, horizontal)
    v_moves, v_missed = axis_moves(grey, vertical)
    edges = np.array([lit_box(frame) or (0, 0, 0, 0) for frame in grey])
    return dict(box=box, horizontal=h_moves, vertical=v_moves, edges=edges,
                unbracketed=h_missed + v_missed)


def describe(moves):
    bands = moves.shape[1]
    return dict(sd=float(moves.mean(axis=1).std()),
                p2p=float(max(moves[:, b].max() - moves[:, b].min()
                              for b in range(bands))),
                bsd=float(np.mean([moves[:, b].std() for b in range(bands)])))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--bands", type=int, default=BANDS)
    parser.add_argument("--span", type=int, default=BAND_SPAN)
    parser.add_argument("--device", default=None, help="override the USB id search")
    parser.add_argument("--warmup", type=int, default=12,
                        help="frames to discard; the default suits a link that "
                             "has not just changed")
    args = parser.parse_args()

    grey = luma(frames(args.frames, args.device or device(), warmup=args.warmup))
    result = measure(grey, args.bands, args.span)
    if result is None:
        print("ALL BLACK -- ask the board before the connector, see hdmi_capture.py")
        return 1

    left, right, top, bottom = result["box"]
    print(f"picture {right - left}x{bottom - top} at {left},{top}   "
          f"{len(grey)} frames, {args.bands} bands")
    for name, moves in (("HORIZONTAL", result["horizontal"]),
                        ("VERTICAL", result["vertical"])):
        stats = describe(moves)
        print(f"  {name:11} sd {stats['sd']:.4f}  p2p {stats['p2p']:.3f}"
              f"  bsd {stats['bsd']:.4f}")
    for index, name in enumerate(("left", "right", "top", "bottom")):
        column = result["edges"][:, index]
        print(f"  edge {name:6} {column.min():4}..{column.max():4}  "
              f"sd {column.std():6.2f}   moved in "
              f"{int((np.diff(column) != 0).sum())} of {len(grey) - 1}")
    if result["unbracketed"]:
        print(f"  {result['unbracketed']} readings ran past the search and are "
              f"floors, not measurements")
    return 0


if __name__ == "__main__":
    sys.exit(main())
