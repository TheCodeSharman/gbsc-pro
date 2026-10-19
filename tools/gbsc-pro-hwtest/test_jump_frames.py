"""Which frames jump_frames.py treats as the start of a tear.

No hardware: the rule is about a SEQUENCE of offsets, and a bench clip carries
an unknown number of events, so a sequence written down here is the only way
to know a run of displaced frames is saved once rather than five times.
"""

import jump_frames


def test_a_run_of_displaced_frames_is_one_event():
    assert jump_frames.onsets([0, 0, 90, 90, 90, 90, 0], 25) == [2]


def test_two_runs_separated_by_a_still_frame_are_two_events():
    assert jump_frames.onsets([0, 90, 90, 0, 90, 0], 25) == [1, 4]


def test_an_offset_below_the_threshold_starts_nothing():
    assert jump_frames.onsets([0, 2, 4, 2, 0], 25) == []


def test_a_run_that_grows_past_the_threshold_starts_once():
    assert jump_frames.onsets([0, 2, 90, 120, 0], 25) == [2]


def test_a_frame_with_nothing_to_correlate_does_not_end_a_run():
    assert jump_frames.onsets([0, 90, None, 90, 0], 25) == [1]


def test_a_run_already_under_way_at_the_first_frame_is_an_event():
    assert jump_frames.onsets([90, 90, 0], 25) == [0]
