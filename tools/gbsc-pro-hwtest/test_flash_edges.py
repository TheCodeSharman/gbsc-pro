"""Reading the card's liveness animation out of a clip.

The two animations flip off one phase and separate on orthogonal channels, but
neither separation is perfect through a subsampled camera, and a landing moves
the feature being read. Both traps are cheap to hit and silent.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flash_edges


def a_profile(spans, width=1600):
    p = np.zeros(width)
    for lo, hi, value in spans:
        p[lo:hi] = value
    return p


def test_the_border_is_read_outside_the_picture_edge():
    """Chroma crosstalk puts a corner block into the border channel. Read the
    border only beyond the picture, or a landing that moves the corner inboard
    reports a border that is switched off."""
    corner = a_profile([(140, 400, 2.0), (1200, 1460, 2.0)])
    border = a_profile([(150, 390, 4.0), (1210, 1450, 4.0)])  # crosstalk only

    left, right = flash_edges.picture_edges(corner)
    assert 139 < left < 142 and 1459 < right < 1462

    peaks = flash_edges.border_peaks(border, left, right)
    assert peaks["left"] < 1.0, f"crosstalk counted as border: {peaks}"
    assert peaks["right"] < 1.0, f"crosstalk counted as border: {peaks}"


def test_a_real_border_outside_the_picture_is_still_found():
    corner = a_profile([(140, 400, 2.0), (1200, 1460, 2.0)])
    border = a_profile([(60, 138, 3.0)])

    left, right = flash_edges.picture_edges(corner)
    peaks = flash_edges.border_peaks(border, left, right)
    assert peaks["left"] > 2.0, f"real border missed: {peaks}"
    assert peaks["right"] < 1.0


def test_an_edge_is_located_between_columns():
    ramp = np.zeros(1600)
    ramp[100:] = 2.0
    ramp[99] = 1.0
    assert abs(flash_edges.half_amplitude(ramp, 20, 400, rising=True) - 99.0) < 0.6


def test_the_panel_edge_ramp_is_not_taken_for_the_picture():
    """A scan from the frame edge stops at the first thing it meets, which on a
    real frame is the panel's own edge rather than the corner block."""
    corner = a_profile([(2, 12, 0.9), (140, 400, 2.0), (1200, 1460, 2.0)])

    left, right = flash_edges.picture_edges(corner)
    assert 139 < left < 142, f"took the panel ramp at {left}"
    assert 1459 < right < 1462


def test_a_clip_that_did_not_catch_the_flip_is_refused():
    """An even split is what the 2 Hz flip gives over a few seconds. A lopsided
    one means the phase locked onto a transient -- a re-lock settling, or the
    sink's own banner -- and the profile that comes back is that transient,
    at tens of levels rather than ones."""
    assert not flash_edges.phase_is_usable(12, 108)
    assert not flash_edges.phase_is_usable(108, 12)
    assert flash_edges.phase_is_usable(53, 67)
    assert flash_edges.phase_is_usable(60, 60)
