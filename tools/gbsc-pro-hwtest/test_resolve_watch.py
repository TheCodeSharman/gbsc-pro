"""Classifying a console line as the engine re-solving. No hardware.

A phase measurement taken across a re-solve is a measurement of two states:
the arm takes the sync pad away, which re-places the sink's window, and the
re-install re-latches the ADC PLL. Holding the phase stops it being re-chosen
and does not stop either of those, so a run needs a certificate rather than a
hold. docs/known-issues.md
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import resolve_watch


def test_the_arm_and_its_reason_are_reported():
    assert resolve_watch.disturbance(
        "measure: start, TIMINGS change (rate), 750 lines, solved 750") == "arm"


def test_the_sync_pad_leaving_is_a_disturbance():
    assert resolve_watch.disturbance("sync pad: away") == "pad"


def test_a_re_installed_divider_is_a_disturbance():
    assert resolve_watch.disturbance(
        "installing: divider from line rate 45013, doubled 0 -> 1444") == "divider"


def test_the_phase_being_searched_again_is_a_disturbance():
    assert resolve_watch.disturbance(
        "phase scan: .............797d773............") == "phase"


def test_an_ordinary_measurement_is_not_a_disturbance():
    assert resolve_watch.disturbance(
        "measured: 750 lines x 60.01 Hz -> line rate 45013, clock 2038@45013 "
        "htotal 2038") is None


def test_an_unrelated_line_is_not_a_disturbance():
    assert resolve_watch.disturbance("phase: sp 8 held, adc 2 held") is None
    assert resolve_watch.disturbance("") is None


def test_a_run_with_nothing_on_the_console_is_certified():
    watch = resolve_watch.Watch.over([])
    assert watch.quiet()
    assert watch.summary() == "console quiet"


def test_a_run_crossing_an_arm_is_refused():
    watch = resolve_watch.Watch.over([
        "measured: 750 lines x 60.01 Hz -> line rate 45013",
        "measure: start, TIMINGS change (rate), 750 lines, solved 750",
        "sync pad: away",
    ])
    assert not watch.quiet()
    assert "arm" in watch.summary() and "pad" in watch.summary()
