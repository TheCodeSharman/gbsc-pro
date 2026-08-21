"""Unit tests for the keypress-driven zoom sweep. No hardware needed."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import characterise
import sweep_zoom


BENCH = {
    "VDS_HSCALE": 694, "VDS_VSCALE": 483,
    "IF_HB_SP2": 262, "IF_HB_ST2": 1105,
    "VDS_DIS_HB_SP": 95, "VDS_DIS_HB_ST": 1348,
}


class FakeUnit:
    """A unit whose capture widens by one input unit for every unit of zoom out.

    That is what the engine does -- capture width = default width - zoomH -- so
    a fake that ignored the zoom could not tell a sweep that steps from one that
    stands still, which is the property most of these tests are about.
    """

    def __init__(self, zoom=0, clamp=None):
        self.origin = zoom
        self.zoom = zoom
        self.clamp = clamp          # the widest capture this line can hold
        self.applied = []

    def apply(self, value):
        self.zoom = value
        self.applied.append(value)
        return self.snapshot(), self.zoom

    def snapshot(self):
        width = 843 + (self.origin - self.zoom)
        if self.clamp is not None:
            width = min(width, self.clamp)
        return dict(BENCH, IF_HB_ST2=BENCH["IF_HB_SP2"] + width)


def sweep(unit, step=-1, prefix="dense", first=1, recorded=None):
    return sweep_zoom.Sweep(unit.apply, unit.snapshot, (recorded if recorded
                                                        is not None else []).append,
                            zoom=unit.zoom, step=step, prefix=prefix, first=first)


def test_enter_means_clear_and_t_means_torn():
    # One key per step, and only the defined ones. Anything else must not be
    # guessed at as a verdict.
    assert sweep_zoom.verdict_for("") == "clean"
    assert sweep_zoom.verdict_for("\r") == "clean"
    assert sweep_zoom.verdict_for("t") == "glitch"
    assert sweep_zoom.verdict_for("T") == "glitch"


def test_a_records_a_residual_that_is_neither_torn_nor_clean():
    # "Almost counts as clear" is right while the question is where the band edges
    # of a gross fault are. Once the question is whether a fix is COMPLETE the
    # residual is the discriminating category, and scoring it clear puts 66/66
    # clean in the log.
    assert sweep_zoom.verdict_for("a") == "almost"


def test_a_key_that_is_not_a_verdict_is_not_scored_as_one():
    assert sweep_zoom.verdict_for("z") is None


def test_a_step_moves_the_zoom_by_one_unit_and_measures_there():
    unit = FakeUnit(zoom=-12)
    s = sweep(unit, step=-1)

    s.step()

    assert unit.applied == [-13]
    assert s.pending["registers"]["IF_HB_ST2"] == BENCH["IF_HB_SP2"] + 844


def test_the_row_holds_every_register_characterise_records():
    # One schema, defined once. A sweep row that recorded fewer fields than a
    # hand-made observation would be a second, quietly poorer, format in the
    # same index file.
    unit = FakeUnit()
    s = sweep(unit)
    s.step()
    for name in characterise.FIELDS:
        assert name in s.pending["registers"], f"{name} not recorded"


def test_the_zoom_travels_with_the_row():
    # The swept variable is the framing, not HSCALE -- which does not move on
    # every press. A table without it cannot say what the step was.
    unit = FakeUnit(zoom=-12)
    s = sweep(unit, step=-1)
    s.step()
    assert s.pending["framing"] == {"axis": "h", "zoom": -13}


def test_judging_a_step_records_it():
    rows = []
    unit = FakeUnit()
    s = sweep(unit, recorded=rows)
    s.step()
    s.keep("glitch")
    assert [row["verdict"] for row in rows] == ["glitch"]


def test_a_step_nobody_judged_is_not_recorded():
    # Stepping past something without judging it is how the aliased dataset
    # happened. Better a short table than one with invented rows in it.
    rows = []
    unit = FakeUnit()
    s = sweep(unit, recorded=rows)
    s.step()
    s.step()
    s.keep("clean")
    assert len(rows) == 1


def test_rows_are_labelled_in_recording_order():
    rows = []
    unit = FakeUnit()
    s = sweep(unit, recorded=rows)
    for _ in range(2):
        s.step()
        s.keep("clean")
    assert [row["label"] for row in rows] == ["dense-01", "dense-02"]


def test_labelling_continues_past_what_the_index_already_holds():
    # dense-01 is already recorded from the last session. A second dense-01
    # would make two different scales the same row.
    existing = [{"label": "dense-01"}, {"label": "onset-5"}]
    assert sweep_zoom.next_number(existing, "dense") == 2


def test_an_empty_index_starts_at_one():
    assert sweep_zoom.next_number([], "dense") == 1


def test_a_note_travels_with_the_row_it_was_typed_for():
    rows = []
    unit = FakeUnit()
    s = sweep(unit, recorded=rows)
    s.step()
    s.keep("glitch", note="~5 scattered lines")
    assert rows[0]["note"] == "~5 scattered lines"


def test_stepping_back_returns_to_the_previous_zoom_and_measures_again():
    # end-1 came out register-identical to onset-1, which is only a finding
    # because both ends were MEASURED. Remembering the earlier snapshot instead
    # would have made that true by construction.
    unit = FakeUnit(zoom=-12)
    s = sweep(unit, step=-1)
    s.step()
    s.step()
    s.back()
    assert unit.applied == [-13, -14, -13]
    assert s.pending["registers"]["IF_HB_ST2"] == BENCH["IF_HB_SP2"] + 844


def test_a_step_the_unit_refused_is_reported_as_one():
    # At the clamp limit the framing stops moving and every further press
    # produces an identical window. Recorded silently, that is a run of rows
    # saying the fault is scale-independent.
    unit = FakeUnit(zoom=0, clamp=844)
    s = sweep(unit, step=-1)
    s.step()
    assert s.moved is True
    s.step()
    assert s.moved is False


def keys(*presses):
    """A key source that plays a script and then quits."""
    queue = list(presses) + ["q"]
    return lambda prompt: queue.pop(0)


def test_a_press_judges_the_step_and_walks_on_to_the_next():
    rows = []
    unit = FakeUnit(zoom=0)
    s = sweep(unit, step=-1, recorded=rows)

    sweep_zoom.run(s, key_source=keys("t", ""), out=lambda *a: None)

    assert [row["verdict"] for row in rows] == ["glitch", "clean"]
    # Three steps applied: two judged, and the one waiting when q arrived.
    assert unit.applied == [-1, -2, -3]


def test_an_unknown_key_asks_again_rather_than_moving_on():
    rows = []
    unit = FakeUnit(zoom=0)
    s = sweep(unit, step=-1, recorded=rows)

    sweep_zoom.run(s, key_source=keys("k", "t"), out=lambda *a: None)

    assert [row["verdict"] for row in rows] == ["glitch"]
    assert unit.applied == [-1, -2]


def test_a_note_typed_before_the_verdict_lands_on_that_row_only():
    rows = []
    unit = FakeUnit(zoom=0)
    s = sweep(unit, step=-1, recorded=rows)

    sweep_zoom.run(s, key_source=keys("n", "t", ""),
                   note_source=lambda prompt: "~5 scattered lines",
                   out=lambda *a: None)

    assert [row["note"] for row in rows] == ["~5 scattered lines", ""]


def test_a_jittering_status_measurement_is_not_the_geometry_moving():
    # Segment 0 carries live measurements -- VTOTAL, HLOW_LEN -- and they blip.
    # Settling on the whole snapshot would wait the full timeout at every step
    # and then call a clamped framing "moved".
    unit = FakeUnit(zoom=0, clamp=843)
    jitter = iter(range(100))
    plain = unit.snapshot
    unit.snapshot = lambda: dict(plain(), STATUS_SYNC_PROC_VTOTAL=next(jitter))

    s = sweep(unit, step=-1)
    s.step()

    assert s.moved is False


def test_the_settle_wait_ends_when_the_geometry_stops_moving():
    # Two agreeing reads, not a fixed sleep: the firmware is slower some days
    # than any constant anyone would pick, and faster on all the others.
    states = iter([{"VDS_HSCALE": 694}, {"VDS_HSCALE": 695}, {"VDS_HSCALE": 695},
                   {"VDS_HSCALE": 696}])
    slept = []
    settled = sweep_zoom.wait_until_settled(
        lambda: next(states), timeout=8.0, interval=0.4,
        clock=lambda: 0.0, sleep=slept.append)

    assert settled == {"VDS_HSCALE": 695}
    assert slept == [0.4]


def test_a_geometry_that_never_settles_is_returned_anyway():
    # A unit still hunting is a finding about the unit, not a reason to hang at
    # the bench with no reading.
    ticks = iter([0.0, 1.0, 2.0, 9.0])
    moving = iter(range(100))
    settled = sweep_zoom.wait_until_settled(
        lambda: {"VDS_HSCALE": next(moving)}, timeout=8.0, interval=0.0,
        clock=lambda: next(ticks), sleep=lambda seconds: None)

    assert settled is not None


def test_the_line_on_screen_describes_the_axis_being_swept():
    # The vertical zoom is the next experiment -- does the null move when only
    # the vertical changes -- and a run of it reporting HSCALE and a horizontal
    # capture would be reporting the axis that was held still.
    row = characterise.observe(dict(BENCH, IF_VB_SP=48, IF_VB_ST=576).get,
                               "x", "clean")
    row["framing"] = {"axis": "v", "zoom": 4}
    line = sweep_zoom.describe(row)
    assert "VSCALE 483" in line
    assert "48..576" in line


def test_quitting_restores_the_framing_it_started_from():
    # A sweep leaves the picture somewhere arbitrary and the next run inherits
    # it. The framing reset is one URL, but only if something sends it.
    unit = FakeUnit(zoom=-12)
    s = sweep(unit, step=-1)
    s.step()
    s.step()
    s.restore()
    assert unit.applied[-1] == -12


def test_the_summary_keeps_the_rows_where_the_verdict_CHANGED():
    # A 342 press run printed as its last forty rows is forty identical lines
    # saying clean, which is what a sweep that left the fault region at press
    # eleven looks like from the tail. The boundaries are the finding.
    rows = [{"label": "a", "verdict": "glitch"}, {"label": "b", "verdict": "glitch"},
            {"label": "c", "verdict": "clean"}, {"label": "d", "verdict": "clean"},
            {"label": "e", "verdict": "glitch"}]
    assert [r["label"] for r in sweep_zoom.transitions(rows)] == ["a", "b", "c", "d", "e"]


def test_a_long_unbroken_run_collapses_to_its_two_ends():
    rows = ([{"label": f"c{n}", "verdict": "clean"} for n in range(50)])
    kept = [r["label"] for r in sweep_zoom.transitions(rows)]
    assert kept == ["c0", "c49"]


def test_a_sweep_can_start_somewhere_other_than_where_the_unit_is():
    # 342 presses to get back to the interesting side is not a sweep, it is a
    # wrist injury. The first run went the wrong way and cleared at HSCALE 704;
    # everything still unknown is on the other side of where it began.
    unit = FakeUnit(zoom=-176)
    s = sweep_zoom.Sweep(unit.apply, unit.snapshot, [].append,
                         zoom=220, step=1, restore_to=unit.zoom)

    s.step()

    assert unit.applied == [221]


def test_restoring_goes_back_to_the_unit_not_to_the_start_point():
    # --start is where the SWEEP begins, not where the picture belongs. Putting
    # it back to the start point would leave the unit somewhere it has never
    # been.
    unit = FakeUnit(zoom=-176)
    s = sweep_zoom.Sweep(unit.apply, unit.snapshot, [].append,
                         zoom=220, step=1, restore_to=-176)
    s.step()
    s.restore()
    assert unit.applied[-1] == -176
