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
