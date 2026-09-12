"""The summarising half of mode_change_bench, which needs no unit."""
import mode_change_bench as b


def test_a_run_that_never_acquired_is_not_counted_as_fast():
    # None means the poll gave up. Averaging it in as a zero would report the
    # worst mode as the best one.
    s = b.summarise([2.0, None, 4.0])
    assert s.acquired == 2 and s.attempts == 3
    assert s.median == 3.0


def test_every_run_failing_leaves_no_timing_rather_than_zero():
    s = b.summarise([None, None])
    assert s.acquired == 0 and s.median is None


def test_the_ladder_step_is_read_from_the_pass_count():
    # SyncRecovery's positions: nothing before 2, LiftSogFloor at 2.
    assert b.ladder_step(1) is None
    assert b.ladder_step(2) == "LiftSogFloor"
    assert b.ladder_step(150) == "FullReset"


def test_the_ladder_cycles_so_a_long_wait_repeats_the_list():
    assert b.ladder_step(2 + b.CYCLE_LENGTH) == "LiftSogFloor"


def test_a_sampling_line_yields_the_line_count_and_rate():
    got = b.parse_sampling("  8.38  sampling: 311 lines x 50.08 Hz -> line rate 15625")
    assert got == (311, 50.08, 15625)


def test_a_line_rate_of_zero_is_a_rejected_reading_not_a_measurement():
    assert b.parse_sampling("sampling: 311 lines x 50.08 Hz -> line rate 0") is None


def test_a_line_that_is_not_a_sampling_report_parses_to_nothing():
    assert b.parse_sampling("own V sync: yes after 2ms") is None


def test_acquired_on_the_old_signature_is_stale_not_a_result():
    # The engine reports the state it last solved. Reading it before the source
    # move has registered says "acquired" about the mode that just left.
    before = {"state": "acquired", "lineRateHz": 15625, "cv": 311}
    assert not b.solved_new_mode(before, before)


def test_acquired_on_a_new_signature_is_the_result():
    before = {"state": "acquired", "lineRateHz": 15625, "cv": 311}
    after = {"state": "acquired", "lineRateHz": 31690, "cv": 524}
    assert b.solved_new_mode(after, before)


def test_a_new_signature_that_is_not_acquired_yet_is_not_the_result():
    before = {"state": "acquired", "lineRateHz": 15625, "cv": 311}
    after = {"state": "unlocked", "lineRateHz": 31690, "cv": 524}
    assert not b.solved_new_mode(after, before)


def test_a_mode_re_entered_from_itself_has_no_new_signature_to_wait_for():
    # Nothing distinguishes the destination from the origin, so the run cannot
    # be timed and must say so rather than return zero.
    before = {"state": "acquired", "lineRateHz": 15625, "cv": 311}
    assert b.solved_new_mode(before, before, same_mode=True)
