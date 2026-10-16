"""The arithmetic picture_stability.py scores a clip with.

No hardware: a synthetic clip rolled by a known number of rows at known frames
is the only way to know the judge reports the movement it was given rather
than a plausible number. The bench cannot supply that -- the fault it exists
for is intermittent, so a bench clip carries an unknown amount of it.
"""

import numpy as np

import picture_stability as stability


def card(rows=1080, lit=140.0):
    """One frame's row profile, with features at several scales down it so a
    roll has something to correlate against."""
    down = np.arange(rows)
    return (lit + 40.0 * np.sin(down * 0.05) + 25.0 * np.sin(down * 0.31)
            + 30.0 * np.sin(down * 0.011)).astype(np.float32)


def held(frames=60):
    """A clip whose picture never moves."""
    return np.tile(card(), (frames, 1))


def rolled(rows_mean, frame, by):
    """One frame's picture moved `by` rows DOWN the frame, the rows it leaves
    behind going black the way a roll's do."""
    out = rows_mean.copy()
    out[frame] = np.roll(out[frame], by)
    if by > 0:
        out[frame, :by] = 0.0
    elif by < 0:
        out[frame, by:] = 0.0
    return out


def test_a_still_picture_is_not_displaced_in_any_frame():
    assert stability.judge(held(), seconds=2.0).displaced == 0


def test_a_rolled_frame_reports_the_rows_it_moved_by():
    moved = stability.displacements(rolled(held(), frame=9, by=24))
    assert moved[9] == 24


def test_a_roll_up_the_frame_is_reported_with_its_sign():
    moved = stability.displacements(rolled(held(), frame=9, by=-18))
    assert moved[9] == -18


def test_only_the_frames_that_moved_are_counted():
    rows = rolled(rolled(held(), frame=9, by=4), frame=20, by=-6)
    assert stability.judge(rows, seconds=2.0).displaced == 2


def test_the_worst_displacement_is_the_largest_either_way():
    rows = rolled(rolled(held(), frame=9, by=4), frame=20, by=-31)
    assert stability.judge(rows, seconds=2.0).worst == -31


def test_a_frame_carrying_no_contrast_is_the_dongle_and_not_a_roll():
    """A frame the capture dropped or re-sent black has no feature to
    correlate, so a lag read off it is noise. Counted as a displacement it
    makes the dongle's own losses read as an unsteady board."""
    rows = held()
    rows[7] = 0.0
    judged = stability.judge(rows, seconds=2.0)
    assert (judged.displaced, judged.lost) == (0, 1)


def test_a_run_of_frames_at_one_offset_is_one_event():
    """A roll persists for several frames, so counting frames counts how long
    it lasted and counting changes counts how often it happens."""
    rows = held()
    for frame in (10, 11, 12, 13):
        rows = rolled(rows, frame=frame, by=6)
    assert stability.judge(rows, seconds=2.0).events == 1


def test_the_event_rate_is_against_the_measured_span():
    """The capture carries no timestamps, so the span is timed around the read
    and the rate cannot come from the frame count."""
    rows = held()
    for frame in (10, 20, 30, 40):
        rows = rolled(rows, frame=frame, by=6)
    assert stability.judge(rows, seconds=2.0).per_second == 2.0


def test_a_picture_that_holds_still_passes():
    assert stability.judge(held(), seconds=2.0).why is None


def test_a_picture_that_moves_too_often_fails_and_names_the_worst():
    rows = held()
    for frame in range(0, 60, 3):
        rows = rolled(rows, frame=frame, by=12)
    judged = stability.judge(rows, seconds=2.0)
    assert "12 rows" in judged.why and "20 of 60" in judged.why


def test_a_clip_reduces_to_one_mean_per_row():
    """The seam between the capture and the arithmetic: a roll moves every
    column alike, so a row's mean carries it at a thousandth of the data."""
    clip = np.zeros((2, 4, 8, 3), np.uint8)
    clip[:, 2, :, :] = 100
    assert stability.row_luma(clip)[0].tolist() == [0.0, 0.0, 100.0, 0.0]


def test_a_picture_that_blanks_part_way_is_not_holding_still():
    """A frame with no picture in it is a frame the picture was ABSENT for,
    which is unsteady by any reading -- so it is counted with the displaced
    ones rather than merely subtracted from the denominator. Left out, a run
    that blanked for a tenth of its frames passed as steady."""
    rows = held()
    for frame in range(0, 60, 10):
        rows[frame] = 0.0
    assert stability.judge(rows, seconds=2.0).why is not None


def test_a_clip_with_no_picture_in_it_does_not_read_as_a_steady_one():
    """A BLACK CAPTURE IS A STATE TO DIAGNOSE, NOT A PASS. Every frame flat
    means nothing to correlate, so no frame is displaced and the share is 0 of
    0 -- which reads as the steadiest picture there has ever been. Measured on
    the bench: a composite leg came back 482 of 482 frames lost and the judge
    called it ok. docs/bench-output-capture.md
    """
    judged = stability.judge(np.zeros((60, 1080), np.float32), seconds=2.0)
    assert judged.why is not None and "no picture" in judged.why


def test_a_single_lost_frame_does_not_void_the_reading():
    """The dongle sheds the odd frame, and failing a whole run on one of them
    would leave nothing measurable. One in sixty is inside the same allowance
    a displaced frame gets, so no second threshold is needed for it."""
    rows = held()
    rows[7] = 0.0
    assert stability.judge(rows, seconds=2.0).why is None
