"""Unit tests for the PB_FETCH_NUM sweep. No hardware needed."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sweep_fetch


BENCH = {
    "PB_FETCH_NUM": 200, "PB_CAP_OFFSET": 250,
    "VDS_HSCALE": 612, "IF_HB_SP2": 270, "IF_HB_ST2": 1007,
}


class FakeUnit:
    """A unit whose only moving part is PB_FETCH_NUM.

    Unlike the zoom sweep, nothing here re-solves: the framing is held still on
    purpose, because the whole point is to vary the read rate against a FIXED
    write rate. A fake that moved the capture would be modelling the other
    experiment.
    """

    def __init__(self, fetch=200):
        self.fetch = fetch
        self.applied = []

    def apply(self, value):
        self.fetch = value
        self.applied.append(value)

    def snapshot(self):
        return dict(BENCH, PB_FETCH_NUM=self.fetch)


def sweep(unit, step=-1, recorded=None, first=1):
    return sweep_fetch.FetchSweep(
        unit.apply, unit.snapshot,
        (recorded if recorded is not None else []).append,
        fetch=unit.fetch, step=step, prefix="comb", first=first)


def test_a_step_moves_the_fetch_and_measures_there():
    unit = FakeUnit(fetch=206)
    s = sweep(unit, step=-1)

    s.step()

    assert unit.applied == [205]
    assert s.pending["registers"]["PB_FETCH_NUM"] == 205


def test_the_framing_is_deliberately_not_touched():
    # The comb test varies the READ rate against a fixed write rate. A sweep
    # that moved the framing would vary both and measure nothing -- that is the
    # zoom sweep, and it is a different experiment.
    unit = FakeUnit()
    s = sweep(unit)
    s.step()
    r = s.pending["registers"]
    assert r["IF_HB_ST2"] - r["IF_HB_SP2"] == 737
    assert r["VDS_HSCALE"] == 612


def test_the_fetch_travels_with_the_row():
    unit = FakeUnit(fetch=206)
    s = sweep(unit, step=-1)
    s.step()
    assert s.pending["sweep"] == {"register": "PB_FETCH_NUM", "value": 205}


def test_rows_are_labelled_in_recording_order():
    rows = []
    unit = FakeUnit()
    s = sweep(unit, recorded=rows)
    for _ in range(2):
        s.step()
        s.keep("clean")
    assert [row["label"] for row in rows] == ["comb-01", "comb-02"]


def test_quitting_restores_the_fetch_it_started_from():
    # A sweep that ends anywhere leaves the picture in whatever state the last
    # press happened to be, and PB_FETCH_NUM is the register the whole
    # investigation now rests on.
    unit = FakeUnit(fetch=200)
    s = sweep(unit, step=-1)
    s.step()
    s.step()
    s.restore()
    assert unit.applied[-1] == 200


def test_the_sweep_refuses_to_leave_the_field():
    # PB_FETCH_NUM is 10 bits. Wrapping past 0 would write a huge value into a
    # register that starves the playback FIFO, and the picture goes with it.
    unit = FakeUnit(fetch=2)
    s = sweep(unit, step=-1)
    s.step()
    s.step()
    assert s.step() is None
    assert min(unit.applied) >= 0


def test_the_comb_reads_the_clean_windows_off_the_rows():
    # The whole point: several separate clean windows means the fault is
    # periodic in the fetch, and the engine can compute it. One window means it
    # is not, and a measured constant is the honest answer.
    rows = [{"verdict": v, "sweep": {"register": "PB_FETCH_NUM", "value": n}}
            for n, v in [(210, "glitch"), (209, "clean"), (208, "clean"),
                         (207, "glitch"), (206, "glitch"), (205, "clean"),
                         (204, "clean"), (203, "glitch")]]
    assert sweep_fetch.windows(rows) == [(208, 209), (204, 205)]


def test_a_single_broad_window_is_reported_as_one():
    rows = [{"verdict": v, "sweep": {"register": "PB_FETCH_NUM", "value": n}}
            for n, v in [(210, "glitch"), (209, "clean"), (208, "clean"),
                         (207, "clean"), (206, "glitch")]]
    assert sweep_fetch.windows(rows) == [(207, 209)]


def test_almost_is_not_clean_when_reading_the_windows():
    # Once a fix is being refined the residual is the discriminating category,
    # and a window that is only clean if you squint is not a window.
    rows = [{"verdict": v, "sweep": {"register": "PB_FETCH_NUM", "value": n}}
            for n, v in [(210, "clean"), (209, "almost"), (208, "clean")]]
    assert sweep_fetch.windows(rows) == [(210, 210), (208, 208)]
