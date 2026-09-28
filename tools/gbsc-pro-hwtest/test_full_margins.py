"""full_margins' edge finding, against synthetic frames. No hardware.

A mode whose picture overruns the emitted frame carries only the card's near
green edge, and the far one is off the raster. The pair is what the ruler is
measured from, so the whole reading used to be refused -- which loses exactly
the modes the capture origin is furthest out on.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import full_margins

WIDTH, HEIGHT = 400, 40
EXPECTED = 200.0


def frame(columns):
    clip = np.zeros((1, HEIGHT, WIDTH, 3), np.uint8)
    for at in columns:
        clip[0, :, at, 1] = 255
    return clip


def test_a_pair_gives_both_edges():
    edges, runs, off = full_margins.green_edges(frame([50, 250]), 1, EXPECTED)
    assert len(runs) == 2
    assert edges[0] == 50.0 and edges[1] == 250.0
    assert off == 0.0


def test_one_edge_gives_the_near_one_and_no_far_one():
    edges, runs, off = full_margins.green_edges(frame([50]), 1, EXPECTED)
    assert len(runs) == 1
    assert edges[0] == 50.0
    assert edges[1] is None
    assert off is None


def test_no_green_at_all_is_still_refused():
    edges, runs, off = full_margins.green_edges(frame([]), 1, EXPECTED)
    assert edges is None
    assert not runs


def test_the_nearest_edge_is_taken_not_the_widest_run():
    edges, _runs, _off = full_margins.green_edges(frame([50, 120]), 1, 1000.0)
    assert edges[0] == 50.0
