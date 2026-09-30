"""transmitted_window's fit of a walk, with no unit attached."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import transmitted_window


def test_a_walk_that_moves_the_edge_gives_where_it_crosses_zero_and_the_slope():
    seen = [(1527, 28), (1507, 55), (1487, 83), (1467, 111), (1447, 139)]
    zero, slope, used, residual = transmitted_window.crossing(seen)
    assert abs(zero - 1546.9) < 0.3
    assert abs(slope - 1.385) < 0.01
    assert used == 5 and residual < 1.0


def test_a_walk_whose_reading_never_moves_is_not_a_crossing():
    # The aperture walked 200 units into black card content: the black at the
    # edge read 278 rows at every step, and the line through that has no zero.
    seen = [(1101, 278), (1081, 278), (1061, 278), (1041, 278), (1021, 278)]
    zero, slope, used, residual = transmitted_window.crossing(seen)
    assert zero is None and slope is None
    assert used == 5


def test_a_black_count_that_starts_flat_is_fitted_from_where_it_climbs():
    # With the source's own black at the edge, the blanking moves through it
    # for the first steps and the count stays at the content's column, which
    # says nothing about the edge; once past the content it climbs a column
    # and a sixth per unit. The zero is the window's start, 172.
    seen = [(180, 75), (200, 75), (220, 75), (240, 78), (260, 101), (280, 124), (300, 147), (320, 170)]
    zero, slope, used, spread = transmitted_window.crossing_past_flat(seen, 1.15)
    assert abs(zero - 172.2) < 0.5
    assert slope == 1.15
    assert used == 4
    assert spread < 0.1


def test_a_step_that_lands_in_a_black_band_of_the_card_does_not_move_the_edge():
    # The count runs on to the next lit column where the blanking's edge falls
    # in one of the card's own black bands, so a step can overshoot by tens of
    # columns. At the raster's own slope each step says where the edge is on
    # its own, and the median is deaf to the overshoots.
    seen = [(320, 242), (340, 269), (360, 297), (380, 366)]
    zero, _slope, used, _spread = transmitted_window.crossing_past_flat(seen, 1.378, flat=0)
    assert abs(zero - 144.5) < 0.5
    assert used == 3


def test_the_far_edge_reads_the_count_from_the_far_side():
    seen = [(1811, 39), (1791, 62), (1771, 85), (1751, 108)]
    zero, _slope, _used, _spread = transmitted_window.crossing_past_flat(seen, 1.15, near=False, flat=0)
    assert abs(zero - 1845.0) < 0.5


def test_a_black_count_with_no_flat_start_is_fitted_as_before():
    seen = [(180, 9), (200, 32), (220, 55), (240, 78)]
    zero, _slope, used, _spread = transmitted_window.crossing_past_flat(seen, 1.15)
    assert abs(zero - 172.2) < 0.5
    assert used == 3


def test_an_edge_needs_three_steps_that_agree_before_it_is_an_edge():
    # A walk that never reaches the content climbs late and unevenly, and its
    # steps disagree on where the edge is. Two agreeing is not a measurement.
    seen = [(417, 256), (537, 256), (557, 273), (577, 327), (597, 327), (617, 345)]
    zero, _slope, used, _spread = transmitted_window.crossing_past_flat(seen, 1.148)
    assert zero is None
    assert used == 0


def test_a_strip_walk_is_read_at_the_rasters_slope_too():
    # Four strip points fit a free slope of 1.00 where the raster's is 1.031,
    # and the two put the edge six units apart. The raster's slope is known.
    seen = [(558, 168), (578, 188), (598, 207), (618, 228)]
    zero, _slope, used, _spread = transmitted_window.crossing_past_flat(seen, 1.031, past_flat=False)
    assert abs(zero - 396.2) < 0.5
    assert used == 4
