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


# --- watching across the command, because the command itself blocks ----------

def _s(t, state, rate, cv):
    return (t, {"state": state, "lineRateHz": rate, "cv": cv})


OLD = {"state": "acquired", "lineRateHz": 15625, "cv": 311}


def test_the_interval_is_measured_between_losing_and_regaining_lock():
    # ModeServ blocks until the source has changed mode and repainted, so the
    # clock has to start when the SOURCE moved, not when the reply came back.
    samples = [_s(0.0, "acquired", 15625, 311), _s(1.0, "acquired", 15625, 311),
               _s(2.0, "absent", 0, 0), _s(3.0, "unlocked", 0, 0),
               _s(4.5, "acquired", 31690, 524)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD)
    assert r.moved == 2.0 and r.acquired == 4.5
    assert r.interval == 2.5


def test_a_transition_with_no_loss_of_lock_still_reports_from_the_signature():
    samples = [_s(0.0, "acquired", 15625, 311),
               _s(1.0, "acquired", 31690, 524)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD)
    assert r.moved == 1.0 and r.acquired == 1.0 and r.interval == 0.0


def test_a_source_that_never_comes_back_has_no_interval():
    samples = [_s(0.0, "acquired", 15625, 311), _s(2.0, "absent", 0, 0)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD)
    assert r.moved == 2.0 and r.acquired is None and r.interval is None


def test_a_source_that_never_moved_reports_nothing_rather_than_zero():
    samples = [_s(0.0, "acquired", 15625, 311), _s(2.0, "acquired", 15625, 311)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD)
    assert r.moved is None and r.acquired is None and r.interval is None


def test_samples_before_the_command_are_not_counted_as_the_move():
    samples = [_s(0.0, "unlocked", 0, 0), _s(1.0, "acquired", 15625, 311),
               _s(3.0, "absent", 0, 0), _s(4.0, "acquired", 31690, 524)]
    r = b.analyse(samples, t_cmd=2.0, before=OLD)
    assert r.moved == 3.0 and r.acquired == 4.0


def test_a_mode_re_entered_from_itself_is_timed_from_the_loss_of_lock():
    # The destination signature is the departure signature, so the only
    # evidence of a transition is that lock was lost and regained.
    samples = [_s(0.0, "acquired", 15625, 311), _s(2.0, "absent", 0, 0),
               _s(4.0, "acquired", 15625, 311)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD, same_mode=True)
    assert r.moved == 2.0 and r.acquired == 4.0 and r.interval == 2.0
