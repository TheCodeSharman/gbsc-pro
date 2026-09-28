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
