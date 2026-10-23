"""The band-spread instrument, against synthetic frames. No hardware.

It exists because the displacement scores cannot see the fault it was built
for: a row-profile correlation finds lag 0 on whatever still correlates, and a
frame difference measures displacement multiplied by local contrast.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frame_bands


def frame(fill=128.0):
    return np.full((frame_bands.HEIGHT, frame_bands.WIDTH), fill, dtype=np.uint8)


def test_a_still_picture_has_no_moving_band():
    taken = [frame_bands.band_means(frame()) for _ in range(20)]
    spread = frame_bands.band_spread(taken)
    assert spread.max() == 0.0
    assert frame_bands.moving_bands(spread) == []


def test_a_band_rewritten_every_frame_is_the_one_reported():
    """The fault disturbs the TOP of the frame and leaves the rest alone, so the
    instrument has to localise rather than score the whole picture: measured at
    rows 0..319 spreading 2.6..7.5 against 0.02 at the bottom."""
    rows = frame_bands.HEIGHT // frame_bands.BANDS
    taken = []
    for i in range(20):
        f = frame()
        f[0:3 * rows, :] = 100 + (i % 2) * 40        # the top three bands move
        taken.append(frame_bands.band_means(f))

    spread = frame_bands.band_spread(taken)
    assert frame_bands.moving_bands(spread) == [0, 1, 2]


def test_a_uniform_shift_is_not_mistaken_for_one_band_moving():
    """A room light changing moves every band together, which is not the fault
    and must not read as one: absolute brightness is not a measurement."""
    taken = [frame_bands.band_means(frame(100 + (i % 2) * 20)) for i in range(20)]
    spread = frame_bands.band_spread(taken)

    assert spread.min() > 0                       # every band moved
    assert frame_bands.moving_bands(spread) == []  # none stands out


def test_the_frame_border_is_kept_out_of_every_band():
    """The border is where our blanking meets the sink's, and it moves for
    reasons that are not the picture's."""
    f = frame(128.0)
    f[:, :frame_bands.INTERIOR[0]] = 255
    f[:, frame_bands.INTERIOR[1]:] = 0

    assert np.allclose(frame_bands.band_means(f), 128.0)
