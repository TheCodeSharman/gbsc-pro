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
                           card_edges.FLUSH)
    assert out.whole == (1.0, 1.0)


def test_a_border_running_past_the_frame_reports_the_fraction_that_survives():
    """A margin cannot say this: a block sitting wholly on column 0 and one
    with three of its four columns off the frame above it both read 0."""
    out = card_edges.judge(blocks(-3, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH)
    assert out.whole[0] == 0.25


def test_a_border_flush_against_the_frames_edge_is_still_whole():
    """Flush and clipped are what the fraction tells apart, so a block whose
    outer column IS column 0 has all of itself on the frame."""
    out = card_edges.judge(blocks(0, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH)
    assert out.whole == (1.0, 1.0)


def test_a_border_off_the_frame_altogether_survives_none_of_itself():
    """Black at the edge is a border clear of it OR one that missed the frame
    entirely, and only the run says which."""
    out = card_edges.judge(blocks(None, WIDTH - 2 - SOURCE_PX), ACROSS,
                           card_edges.FLUSH)
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
                           card_edges.FLUSH)
    assert out.margins == (0, 0)
    assert out.out_by == 0
    assert out.whole == (0.25, 1.0)
    assert out.why is not None and "0.25" in out.why


def test_three_quarters_of_the_outermost_pixel_is_enough():
    """What the acceptance allows: the border may run past the frame, as long
    as most of the pixel it marks is still on it."""
    out = card_edges.judge(blocks(-1, WIDTH - SOURCE_PX), ACROSS,
                           card_edges.FLUSH)
    assert out.whole == (card_edges.WHOLE_ENOUGH, 1.0)
    assert out.why is None


def test_a_border_clipped_to_a_sliver_inside_the_frames_edge_reports_the_sliver():
    """Black at the frame's edge with the border's remains just inside it. The
    picture's last row cutting through the border leaves exactly this, and
    reading the outermost sample alone calls it a border clear of the edge --
    measured on `vga` at 320x256@50, a bottom row carrying 94 of the 882 a
    whole border carries, reported as the whole pixel."""
    clip = blocks(0, None)
    clip[0, :, WIDTH - 2, 1] = 255
    out = card_edges.judge(clip, ACROSS, card_edges.FLUSH)
    assert out.margins == (0, 1)
    assert out.whole == (1.0, 0.25)


# What the BENCH's border looks like, against the synthetic block above. The
# card's frame is one source pixel and the capture samples it a few times, so
# what arrives is a ramp that never reaches the green the card is drawn at, and
# it spreads wider than the pixel it marks. Measured on `vga` at 640x512@50
# into 1080p, 1.86 emitted columns per source pixel, the border 254 columns
# clear of the frame's edge -- so nothing can have cut it.
BENCH_SMEAR = (22, 22, 44, 44, 57, 57, 6, 6)
BENCH_SOLID = 196
BENCH_SOURCE_PX = 1.86


def smeared(near_at, far_at):
    """A clip carrying the bench's attenuated border at each end and a block of
    the card's own solid green between them."""
    clip = np.zeros((1, HEIGHT, WIDTH, 3), np.uint8)
    for at, smear in ((near_at, BENCH_SMEAR), (far_at, BENCH_SMEAR[::-1])):
        for i, level in enumerate(smear):
            clip[0, :, at + i, 1] = level
    clip[0, :, WIDTH // 2 - 50:WIDTH // 2 + 50, 1] = BENCH_SOLID
    return clip


def test_an_attenuated_border_clear_of_the_frames_edge_is_whole():
    """The border's captured amplitude follows the sampling density and none of
    that is a deficit. Against the card's solid green this reads 0.67, which
    fails a border the capture window opened 254 columns before."""
    out = card_edges.judge(smeared(10, WIDTH - 10 - len(BENCH_SMEAR)), ACROSS,
                           card_edges.FLUSH)
    assert out.whole == (1.0, 1.0)


def test_the_cards_own_green_is_not_the_reference():
    """The card carries solid green blocks between its edges, which outweigh a
    one-pixel border many times over. Where the near border is off the frame
    the outermost run is one of those blocks, and reading it as the opposite
    edge reports a whole far border as nearly all gone."""
    clip = smeared(10, WIDTH - 10 - len(BENCH_SMEAR))
    clip[0, :, 10:10 + len(BENCH_SMEAR), 1] = 0
    out = card_edges.judge(clip, ACROSS, card_edges.FLUSH)
    assert out.whole == (0.0, 1.0)


# --- what the design puts at an edge ---------------------------------------------

def test_an_edge_the_design_clips_is_not_a_deficit():
    """Where two authorities state one resolution the standard answers, so the
    mode file's own border is never captured -- and nothing on the chip can see
    the difference, a border being black active video. The displacement shows
    as black at the far end, which is where it is judged."""
    out = card_edges.judge(across(None, WIDTH - 1 - 18), ACROSS,
                           card_edges.FLUSH, expected=(-18.0, 18.0))
    assert out.margins == (None, 18)
    assert out.out_by == 0
    assert out.why is None


def test_the_design_is_the_goal_rather_than_flush_where_it_puts_black():
    # 18 columns expected at the far edge: 18 is on design, 24 is six out.
    on_design = card_edges.judge(across(None, WIDTH - 1 - 18), ACROSS,
                                 card_edges.FLUSH, expected=(-18.0, 18.0))
    adrift = card_edges.judge(across(None, WIDTH - 1 - 24), ACROSS,
                              card_edges.FLUSH, expected=(-18.0, 18.0))
    assert on_design.why is None
    assert adrift.out_by == 6
    assert "against 18 by design" in adrift.why


def test_a_flush_edge_where_the_design_wants_black_is_still_out_by_it():
    """The offset is not a budget to spend: a picture that reaches the frame's
    edge where the design shows black is as far from the design as one that
    leaves twice the black."""
    out = card_edges.judge(across(None, WIDTH - 1), ACROSS, card_edges.FLUSH,
                           expected=(-18.0, 18.0))
    assert out.out_by == 18
    assert out.why is not None


def test_no_expectation_is_the_goal_itself():
    plain = judged(0, WIDTH - 1)
    stated = card_edges.judge(across(0, WIDTH - 1), ACROSS, card_edges.FLUSH,
                              expected=(0.0, 0.0))
    assert plain == stated
