"""card_edges' verdict, against synthetic frames. No hardware.

A verdict needs a magnitude. A green column one pixel off the frame and one a
hundred pixels off are the same fault at two sizes, and a framing judged
without the size cannot say which cases are nearly right -- nor tolerate the
pixel or two the solve lands apart from one run to the next.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_edges

WIDTH, HEIGHT = 400, 40
ACROSS = 1


def across(near, far):
    """A clip whose green frame columns sit at `near` and `far`. A column off
    the emitted frame is given as None, which is what the capture sees of it."""
    clip = np.zeros((1, HEIGHT, WIDTH, 3), np.uint8)
    for at in (near, far):
        if at is not None:
            clip[0, :, at, 1] = 255
    return clip


def judged(near, far, allowance=card_edges.FLUSH):
    return card_edges.judge(across(near, far), ACROSS, allowance)


def test_a_flush_frame_is_out_by_nothing():
    out = judged(0, WIDTH - 1)
    assert out.margins == (0, 0)
    assert out.out_by == 0
    assert out.why is None


def test_the_magnitude_is_the_worst_edges_distance_from_flush():
    assert judged(9, WIDTH - 1).out_by == 9
    assert judged(0, WIDTH - 1 - 23).out_by == 23


def test_a_margin_inside_the_allowance_is_still_measured():
    """The magnitude is the distance from the goal, not the amount by which an
    acceptance threshold was missed, so raising the allowance cannot hide a
    drift that is still growing."""
    out = judged(1, WIDTH - 1)
    assert out.why is None
    assert out.out_by == 1


def test_an_edge_off_the_frame_is_sized_by_the_edge_still_on_it():
    """The found edge's margin is what sizes the shift: the two ends move
    together, so a picture misplaced by sixty pixels reads sixty at the end
    still on the frame."""
    out = judged(None, WIDTH - 1 - 60)
    assert out.margins == (None, 60)
    assert out.out_by == 60
    assert "60" in out.why


def test_an_edge_off_the_frame_beside_a_flush_one_is_inside_the_allowance():
    """Gone at one end and flush at the other is the rounding the solve lands
    either side of, which is the nondeterminism the allowance covers -- and a
    width wrong by more than it puts BOTH columns off, which bounds nothing and
    fails whatever the allowance."""
    out = judged(None, WIDTH - 1)
    assert out.margins == (None, 0)
    assert out.out_by == card_edges.OFF_FRAME_LEAST
    assert out.why is None


def test_neither_edge_bounds_nothing():
    """Both columns off the frame needs a width wrong by more than the
    allowance at both ends, and no feature in the frame says by how much."""
    out = judged(None, None)
    assert out.out_by is None
    assert out.why is not None


def test_the_far_edge_carries_the_width_parity():
    assert judged(0, WIDTH - 1 - card_edges.FLUSH - card_edges.PARITY).why is None
    assert judged(card_edges.FLUSH + card_edges.PARITY, WIDTH - 1).why is not None


def test_the_span_is_what_the_two_margins_leave():
    out = judged(4, WIDTH - 1 - 7)
    assert out.shown == 11
