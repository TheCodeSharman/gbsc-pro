"""Panning right is what walks the capture stop into the part of the line the
capture path does not write, and the bound on the pan is the fix.

A mark is unreadable unless it says where the capture stop was, so every one
records the stop and what it had left before the clamp.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import creep_pan


def a_state(hsync_rst=1125, capture=(118, 1008)):
    return {"IF_HSYNC_RST": hsync_rst,
            "IF_HB_SP2": capture[0], "IF_HB_ST2": capture[1]}


def test_a_mark_records_how_far_the_stop_is_from_the_clamp():
    where = creep_pan.clamp_state(a_state())

    assert where["line_units"] == 1126
    assert where["last_capture"] == 1124
    assert where["stop"] == 1008
    assert where["clearance"] == 116


def test_the_line_wrap_is_the_only_bound_however_long_the_line():
    # A write limit was carried here as a second bound and there is none: the
    # capture reaches the end of the line at every divider.
    where = creep_pan.clamp_state(a_state(hsync_rst=1300))

    assert where["last_capture"] == 1299


def test_a_step_waits_for_the_engine_to_re_solve_rather_than_sleeping():
    """The pan is applied from loop(), not from the network callback, so the
    window read straight back is still the one from before the press."""
    reads = iter([1008, 1008, 1032, 1032])

    assert creep_pan.settled(lambda: next(reads), was=1008,
                             pause=lambda _: None) == 1032


def test_a_clamped_step_reports_the_unchanged_window_rather_than_hanging():
    """Against the clamp a press moves nothing, and that is the reading."""
    assert creep_pan.settled(lambda: 1124, was=1124, tries=3,
                             pause=lambda _: None) == 1124
