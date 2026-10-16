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


def test_an_edge_off_the_frame_is_sized_by_the_allowance_and_fails_the_pixel():
    """Gone at one end and flush at the other is the rounding the solve lands
    either side of, which the MARGIN's allowance covers -- and a width wrong by
    more than it puts BOTH columns off, which bounds nothing. The pixel is a
    separate question and it is lost: a border no part of which reached the
    frame survives none of itself, however small the shift that cost it."""
    out = judged(None, WIDTH - 1)
    assert out.margins == (None, 0)
    assert out.out_by == card_edges.OFF_FRAME_LEAST
    assert out.whole == (0.0, 1.0)
    assert "0.00" in out.why


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


# One source pixel is several output pixels under magnification, so the card's
# border is a BLOCK and the acceptance is how much of that block survives.
SOURCE_PX = 4


def blocks(near_at, far_at, width=SOURCE_PX):
    """A clip whose green border blocks START at `near_at` and `far_at`, either
    of which may lie partly off the frame."""
    clip = np.zeros((1, HEIGHT, WIDTH, 3), np.uint8)
    for at in (near_at, far_at):
        if at is None:
            continue
        lo, hi = max(0, at), min(WIDTH, at + width)
        if lo < hi:
            clip[0, :, lo:hi, 1] = 255
    return clip


def test_a_border_clear_of_the_frames_edge_is_whole():
    out = card_edges.judge(blocks(2, WIDTH - 2 - SOURCE_PX), ACROSS,
                           card_edges.FLUSH, SOURCE_PX)
    assert out.whole == (1.0, 1.0)


def test_a_border_running_past_the_frame_reports_the_fraction_that_survives():
    """A margin cannot say this: a block sitting wholly on column 0 and one
    with three of its four columns off the frame above it both read 0."""
    out = card_edges.judge(blocks(-3, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH, SOURCE_PX)
    assert out.whole[0] == 0.25


def test_a_border_flush_against_the_frames_edge_is_still_whole():
    """Flush and clipped are what the fraction tells apart, so a block whose
    outer column IS column 0 has all of itself on the frame."""
    out = card_edges.judge(blocks(0, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH, SOURCE_PX)
    assert out.whole == (1.0, 1.0)


def test_a_border_off_the_frame_altogether_survives_none_of_itself():
    """Black at the edge is a border clear of it OR one that missed the frame
    entirely, and only the run says which."""
    out = card_edges.judge(blocks(None, WIDTH - 2 - SOURCE_PX), ACROSS,
                           card_edges.FLUSH, SOURCE_PX)
    assert out.whole == (0.0, 1.0)


def test_the_source_pixels_width_is_the_frame_over_the_modes_active_count():
    """At a default framing the emitted frame IS the source's active area, so
    the magnification needs no register read."""
    clip = blocks(0, WIDTH - SOURCE_PX)
    assert card_edges.source_pixel_px("X100 Y40 C256 F50", clip, ACROSS) == 4.0
    assert card_edges.source_pixel_px("X100 Y40 C256 F50", clip, 0) == 1.0


def test_a_border_mostly_off_the_frame_fails_though_its_margin_is_flush():
    """The fault the margin cannot see: flush at both ends, and the outermost
    source pixel nearly all gone."""
    out = card_edges.judge(blocks(1 - SOURCE_PX, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH, SOURCE_PX)
    assert out.margins == (0, 0)
    assert out.out_by == 0
    assert out.whole == (0.25, 1.0)
    assert out.why is not None and "0.25" in out.why


def test_three_quarters_of_the_outermost_pixel_is_enough():
    """What the acceptance allows: the border may run past the frame, as long
    as most of the pixel it marks is still on it."""
    out = card_edges.judge(blocks(-1, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH, SOURCE_PX)
    assert out.whole == (card_edges.WHOLE_ENOUGH, 1.0)
    assert out.why is None
