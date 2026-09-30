"""The dark-run arithmetic behind dark_time.py, no hardware."""

from dark_time import dark_runs, longest


def test_a_dark_span_is_reported_from_its_first_dark_frame_to_the_first_lit_one():
    samples = [(0.0, 80), (0.1, 80), (0.2, 5), (0.3, 5), (0.4, 80), (0.5, 80)]
    assert dark_runs(samples, threshold=36) == [(0.2, 0.4)]


def test_a_flicker_before_the_real_span_is_a_separate_run_and_the_longest_wins():
    samples = [(0.0, 80), (0.1, 5), (0.2, 80), (0.3, 5), (0.4, 5), (0.5, 5), (0.6, 80)]
    runs = dark_runs(samples, threshold=36)
    assert runs == [(0.1, 0.2), (0.3, 0.6)]
    assert longest(runs) == (0.3, 0.6)


def test_a_run_still_dark_at_the_end_closes_at_the_last_sample():
    samples = [(0.0, 80), (0.1, 5), (0.2, 5)]
    assert dark_runs(samples, threshold=36) == [(0.1, 0.2)]


def test_no_dark_frame_means_no_run():
    assert dark_runs([(0.0, 80), (0.1, 81)], threshold=36) == []
    assert longest([]) is None
