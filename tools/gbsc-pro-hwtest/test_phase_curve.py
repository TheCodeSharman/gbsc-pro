"""phase_curve answers when invoked, and refuses a leg it cannot measure.

A leg naming a register that rescales the picture reads as a measurement of
that register and is a measurement of the wedge box sliding off the grating --
which is how a peak of 28.0 against 52.8 got recorded as an oversampling
result. No hardware.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import phase_curve


def run(*args):
    return subprocess.run([sys.executable, os.path.join(HERE, "phase_curve.py"), *args],
                          capture_output=True, text=True, timeout=60)


def test_invoking_it_answers_rather_than_exiting_in_silence():
    done = run("--help")
    assert "usage" in done.stdout.lower(), (
        "printed nothing: %r / %r" % (done.stdout[:200], done.stderr[-400:]))


def test_a_missing_host_is_refused():
    done = run()
    assert done.returncode != 0
    assert "--host" in done.stderr


def test_the_divider_is_not_offered_as_a_leg():
    """It rescales the picture, so a held box lands on different content."""
    assert "PLLAD_MD" in phase_curve.RESCALES
    assert "SP_RT_HS_SP" in phase_curve.RESCALES


def test_a_phase_is_never_offered_as_a_leg_register():
    """The sweep owns the phase; a leg setting it would fight the sweep."""
    assert "PA_ADC_S" not in phase_curve.RESCALES, (
        "PA_ADC_S is swept rather than refused -- it belongs to sweep(), not legs")
