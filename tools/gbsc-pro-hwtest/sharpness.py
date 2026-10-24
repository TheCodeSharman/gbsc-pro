"""How sharp the emitted picture is, over ordinary content rather than a wedge.

The finest grating answers whether the ADC sampled at the limit, and it is
adversarial by construction -- sustained periodic content at Nyquist, which
beats rather than merely softens. **A reading there is not a picture-quality
claim**, and the question it cannot answer is whether anything a viewer would
see moves with it.

This is the measure that can: the mean and 99th-percentile luma gradient over
the whole lit picture with the wedge band cut out, so what is left is grid
lines, castellations, colour-bar edges and the centre circle -- the 1 px
features that occur naturally.

**THE VERTICAL GRADIENT IS THE CONTROL.** The sampling phase is a horizontal
phenomenon: where in the ADC clock a sample of the line is taken. Vertical
sharpness is whole lines and cannot move with it, so a swing that appears in
both axes is something else -- brightness, a re-lock, the link -- and is not
the phase.

Peak and mean answer different questions and both are reported. The mean is
dominated by flat card, so a change to it is a change over most of the
picture; the 99th percentile is the sharpest edges in the frame and is where a
softening of 1 px features would land first.
"""

import numpy as np


def measure(greys, lit, exclude_rows=None):
    """Luma gradient statistics over a clip, in grey levels per pixel.

    `greys` is (frames, height, width), `lit` the picture's own box as
    (left, right, top, bottom) inclusive, and `exclude_rows` an absolute
    (top, bottom) row range to drop -- the wedge's band.

    **SCORED PER FRAME AND THEN AVERAGED.** A picture that shimmers has sharp
    frames and a blurred average, so a measure taken on the mean frame reports
    the movement as softness and the two cannot be told apart.
    """
    left, right, top, bottom = lit
    rows = np.ones(bottom - top + 1, bool)
    if exclude_rows is not None:
        band_top, band_bottom = exclude_rows
        rows[max(0, band_top - top):max(0, band_bottom - top + 1)] = False

    block = greys[:, top:bottom + 1, left:right + 1][:, rows, :].astype(np.float32)
    if block.size == 0 or block.shape[2] < 2 or block.shape[1] < 2:
        raise AssertionError(
            f"nothing left to measure in lit box {lit} after excluding "
            f"{exclude_rows}")

    out = {}
    for axis, name in ((2, "dx"), (1, "dy")):
        gradient = np.abs(np.diff(block, axis=axis))
        per_frame = gradient.reshape(gradient.shape[0], -1)
        out[f"mean_{name}"] = float(per_frame.mean(axis=1).mean())
        out[f"p99_{name}"] = float(np.percentile(per_frame, 99, axis=1).mean())
    out["luma"] = float(block.mean())
    return out
