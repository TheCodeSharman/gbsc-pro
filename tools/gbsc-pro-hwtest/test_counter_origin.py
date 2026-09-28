"""counter_origin's feature finding, against synthetic frames. No hardware.

The card's frame is what the origin is read from, and the emitted frame carries
a green artefact at its own first column that the picture does not put there.
Taken as the near edge it reads as a source whose video starts 65 px early, and
nothing about the number says it came from the wrong run.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import counter_origin

WIDTH, HEIGHT = 400, 40


def frame(columns):
    clip = np.zeros((1, HEIGHT, WIDTH, 3), np.uint8)
    for at in columns:
        clip[0, :, at, 1] = 255
    return clip


def test_the_cards_two_edges_are_the_near_run():
    near, far = counter_origin.near_run(frame([50, 250]), 1)
    assert round(near) == 50
    assert round(far) == 250


def test_the_left_edge_artefact_is_not_the_near_edge():
    near, far = counter_origin.near_run(frame([0, 50, 250]), 1)
    assert round(near) == 50
    assert round(far) == 250


def test_an_artefact_on_the_far_boundary_is_not_the_far_edge():
    near, far = counter_origin.near_run(frame([50, 250, WIDTH - 1]), 1)
    assert round(near) == 50
    assert round(far) == 250


def test_a_picture_with_only_boundary_runs_is_refused():
    assert counter_origin.near_run(frame([0, WIDTH - 1]), 1) == (None, None)


def clipped(leading, width, low, high):
    """A feature `width` counter units wide, clipped from the left by the
    window's start, as the (register, amplitude) walk the creep collects."""
    return [(v, max(0.0, leading + width - max(v, leading)))
            for v in range(low, high + 1)]


def test_the_crossing_is_the_features_leading_edge_not_its_centre():
    """A half-amplitude point sits half a feature past where it starts, and the
    feature is one SOURCE pixel -- so the error is half a source pixel however
    many counter units that is, and it differs between two modes at one line
    rate. Measured on the twins it is 0.68 units against 0.35."""
    for width in (1.363, 0.681, 2.0, 4.3):
        for leading in (293.0, 293.25, 293.5, 293.75):
            found = counter_origin.leading_edge(clipped(leading, width, 280, 305),
                                                width)
            assert abs(found - leading) <= 0.35, (width, leading, found)


def test_a_walk_that_brackets_the_feature_reports_its_own_ramp():
    """The ramp is the feature's width, so a ramp that is not says the walk
    measured something other than the feature leaving the capture."""
    walk = clipped(293.0, 2.15, 280, 305)
    shape = counter_origin.walk_shape(walk)
    assert shape["held"] and shape["emptied"]
    assert abs(shape["ramp"] - 2.15) < 1.0


def test_a_walk_that_stops_before_the_feature_leaves_says_so():
    assert not counter_origin.walk_shape(clipped(304.5, 2.15, 280, 305))["emptied"]


def test_a_walk_that_starts_after_the_feature_is_going_says_so():
    """Its own maximum is at step one, so a high first sample is not enough."""
    assert not counter_origin.walk_shape(clipped(279.0, 2.15, 280, 305))["held"]
