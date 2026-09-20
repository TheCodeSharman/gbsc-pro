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


# --- a settle is a HOLD, not one acquired reading -----------------------------

def test_one_acquired_reading_is_not_a_settle():
    # The unit keeps solving for ~3 s after it first reports acquired, and a run
    # started on that reading times the previous leg's churn.
    samples = [_s(0.0, "absent", 0, 0), _s(1.0, "acquired", 31690, 524)]
    assert b.unbroken_since(samples) == 1.0


def test_a_run_of_agreeing_acquired_samples_dates_from_its_first():
    samples = [_s(0.0, "absent", 0, 0), _s(1.0, "acquired", 31690, 524),
               _s(2.0, "acquired", 31690, 524), _s(3.0, "acquired", 31690, 524)]
    assert b.unbroken_since(samples) == 1.0


def test_a_signature_that_moved_restarts_the_hold():
    samples = [_s(0.0, "acquired", 15625, 311), _s(1.0, "acquired", 31690, 524),
               _s(2.0, "acquired", 31690, 524)]
    assert b.unbroken_since(samples) == 1.0


def test_a_source_not_acquired_is_holding_nothing():
    samples = [_s(0.0, "acquired", 31690, 524), _s(1.0, "absent", 0, 0)]
    assert b.unbroken_since(samples) is None
    assert b.unbroken_since([]) is None


def test_a_drop_out_mid_run_restarts_the_hold():
    samples = [_s(0.0, "acquired", 31690, 524), _s(1.0, "absent", 0, 0),
               _s(2.0, "acquired", 31690, 524)]
    assert b.unbroken_since(samples) == 2.0


# --- a transition is an ordered pair ------------------------------------------

def test_a_leg_is_named_by_both_ends_because_the_cost_is_not_symmetric():
    assert b.leg("MODE X640 Y480 C256 F60", "MODE X320 Y256 C256 F50") != \
           b.leg("MODE X320 Y256 C256 F50", "MODE X640 Y480 C256 F60")


def test_every_ordered_pair_is_walked_and_no_mode_transitions_to_itself():
    legs = b.legs(["A", "B", "C"])
    assert set(legs) == {("A", "B"), ("A", "C"), ("B", "A"),
                         ("B", "C"), ("C", "A"), ("C", "B")}


def test_the_walk_departs_from_where_the_previous_leg_landed():
    # Re-settling costs a whole mode change, so a walk that chains arrivals to
    # departures pays for one rather than two.
    legs = b.legs(["A", "B", "C"])
    for (_, landed), (departs, _) in zip(legs, legs[1:]):
        assert landed == departs


# --- reporting the asymmetry, which is the point of walking both ways ---------

def _sum(vals):
    return b.summarise(vals)


def test_opposed_legs_are_reported_as_a_pair_slower_first():
    s = {"640x480@60 -> 320x256@50": _sum([3.0]),
         "320x256@50 -> 640x480@60": _sum([1.0])}
    (slow, _), (fast, _), gap = b.asymmetry(s)[0]
    assert slow == "640x480@60 -> 320x256@50"
    assert fast == "320x256@50 -> 640x480@60"
    assert gap == 2.0


def test_a_leg_whose_reverse_was_not_walked_has_no_pair_to_report():
    assert b.asymmetry({"A@1 -> B@2": _sum([3.0])}) == []


def test_a_pair_is_reported_once_rather_than_from_each_end():
    s = {"640x480@60 -> 320x256@50": _sum([3.0]),
         "320x256@50 -> 640x480@60": _sum([1.0])}
    assert len(b.asymmetry(s)) == 1


def test_a_pair_with_a_direction_that_never_acquired_is_not_a_gap():
    s = {"640x480@60 -> 320x256@50": _sum([None]),
         "320x256@50 -> 640x480@60": _sum([1.0])}
    assert b.asymmetry(s) == []


def test_a_mode_name_is_shortened_to_what_distinguishes_it():
    assert b.short("MODE X640 Y480 C256 F60") == "640x480@60"
    assert b.short("PATTERN PM5544") == "PATTERN PM5544"


# --- the arrival is a hold too, not one acquired reading ----------------------

def test_the_arrival_is_not_timed_to_the_first_acquired_reading():
    # The same rule the DEPARTURE already applies. The engine keeps re-solving
    # for about three seconds after it first answers acquired, and a leg stopped
    # on that reading reports the first of several solves as the whole change.
    samples = [_s(0.0, "acquired", 15625, 311), _s(1.0, "absent", 0, 0),
               _s(2.0, "acquired", 15625, 306), _s(3.0, "acquired", 15625, 308),
               _s(4.0, "acquired", 15625, 308), _s(5.0, "acquired", 15625, 308),
               _s(6.0, "acquired", 15625, 308)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD, dwell=3.0)
    assert r.moved == 1.0
    assert r.acquired == 2.0
    assert r.settled == 3.0
    assert r.interval == 1.0
    assert r.settled_interval == 2.0


def test_a_raster_not_held_for_the_dwell_is_not_a_settle():
    samples = [_s(0.0, "acquired", 15625, 311), _s(1.0, "absent", 0, 0),
               _s(2.0, "acquired", 15625, 308), _s(3.0, "acquired", 15625, 308)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD, dwell=3.0)
    assert r.acquired == 2.0
    assert r.settled is None and r.settled_interval is None


def test_a_drop_out_after_the_arrival_restarts_the_hold():
    samples = [_s(0.0, "acquired", 15625, 311), _s(1.0, "absent", 0, 0),
               _s(2.0, "acquired", 15625, 308), _s(3.0, "absent", 0, 0),
               _s(4.0, "acquired", 15625, 308), _s(5.0, "acquired", 15625, 308),
               _s(6.0, "acquired", 15625, 308), _s(7.0, "acquired", 15625, 308)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD, dwell=3.0)
    assert r.acquired == 2.0
    assert r.settled == 4.0


def test_the_departure_raster_returning_is_not_an_arrival():
    # A leg that ends where it started has not changed mode, whatever the state
    # field says on the way through.
    samples = [_s(0.0, "acquired", 15625, 311), _s(1.0, "absent", 0, 0),
               _s(2.0, "acquired", 15625, 311), _s(3.0, "acquired", 15625, 311),
               _s(4.0, "acquired", 15625, 311), _s(5.0, "acquired", 15625, 311)]
    r = b.analyse(samples, t_cmd=0.5, before=OLD, dwell=3.0)
    assert r.acquired is None and r.settled is None
