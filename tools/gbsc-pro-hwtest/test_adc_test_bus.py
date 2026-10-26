"""adc_test_bus scores a leg against the control's own spread, not one reading.

Several test-bus selectors carry live analog-derived signals whose transition
count swings by hundreds between two windows on an untouched unit, so a leg
differing from a single control sweep is the ordinary swing -- which is how
three selectors came back as candidates and all three fell to nothing when
re-read. No hardware.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import adc_test_bus


def run(*args):
    return subprocess.run(
        [sys.executable, os.path.join(HERE, "adc_test_bus.py"), *args],
        capture_output=True, text=True, timeout=60)


def test_invoking_it_answers_rather_than_exiting_in_silence():
    done = run("--help")
    assert "usage" in done.stdout.lower(), (
        "printed nothing: %r / %r" % (done.stdout[:200], done.stderr[-400:]))


def test_a_missing_host_is_refused():
    assert run().returncode != 0


def test_a_reading_inside_the_controls_spread_is_not_a_difference():
    envelope = adc_test_bus.Envelope()
    for transitions in (3600, 3700, 3800):
        envelope.add({5: (transitions, 0.37)})

    assert envelope.outside(5, (3750, 0.37)) == 0.0
    assert envelope.outside(5, (3650, 0.38)) <= adc_test_bus.BAR


def test_a_reading_well_outside_it_is():
    envelope = adc_test_bus.Envelope()
    for transitions in (3600, 3700, 3800):
        envelope.add({5: (transitions, 0.37)})

    assert envelope.outside(5, (4200, 0.37)) > adc_test_bus.BAR


def test_a_selector_the_control_never_moved_is_scored_against_a_floor():
    """Otherwise its range has zero width and every reading reads infinitely
    far outside it, which turns a one-transition difference into a finding."""
    envelope = adc_test_bus.Envelope()
    for _ in range(5):
        envelope.add({0: (2, 0.04)})

    assert envelope.outside(0, (4, 0.08)) <= adc_test_bus.BAR
    assert envelope.outside(0, (400, 0.50)) > adc_test_bus.BAR


def test_a_selector_the_control_never_saw_cannot_be_scored():
    assert adc_test_bus.Envelope().outside(31, (999, 0.9)) == 0.0
