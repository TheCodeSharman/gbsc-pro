"""phase_pair answers when it is invoked. No hardware.

A module that defines main() and never calls it exits 0 printing nothing, which
is indistinguishable from a tool nobody has got round to running -- and that is
how this one was recorded as never run while every invocation of it silently
did nothing. ruff passes such a file.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import phase_pair


def run(source, *args):
    return subprocess.run([sys.executable, source, *args],
                          capture_output=True, text=True, timeout=60)


def test_invoking_it_answers_rather_than_exiting_in_silence():
    done = run(os.path.join(HERE, "phase_pair.py"), "--help")
    assert "usage" in done.stdout.lower(), (
        "it printed nothing: %r / %r" % (done.stdout[:200], done.stderr[-400:]))


def test_a_missing_host_is_refused_rather_than_ignored():
    done = run(os.path.join(HERE, "phase_pair.py"))
    assert done.returncode != 0
    assert "--host" in done.stderr


def test_a_bounce_through_the_same_mode_is_refused():
    """A bounce the source never leaves samples one acquisition every trial."""
    same = "MODE X800 Y600 C256 F60"
    done = run(os.path.join(HERE, "phase_pair.py"),
               "--host", "h", "--modeserv", "m", "--mode", same, "--other", same)
    assert done.returncode != 0
    assert "--other must differ" in done.stderr


def test_a_flat_adc_curve_is_refused_as_an_argmax_of_noise():
    """Measured 2026-10-26, PM5544's finest grating off the USB capture.

    One mode, one instrument, one variable -- the path. 1280x720@60 reads
    52.6/17.1 in pass-through and 11.3/11.2 scaled, and 800x600@60 scaled reads
    32.4/29.6 as the mean of eight acquisitions: the scaler resamples the
    grating away before it reaches the capture, so no phase response survives
    it and the field's argmax is noise.
    """
    assert not phase_pair.is_flat(52.6, 17.1)
    assert phase_pair.is_flat(11.3, 11.2)
    assert phase_pair.is_flat(32.4, 29.6)
    assert phase_pair.is_flat(1.0, 0.0), "a dark field is not a peak"


def test_the_threshold_separates_the_pass_through_modes_already_measured():
    """the-adc-sampling-phase-is-a-lottery.md's wedge peak-over-trough set.

    1280x1024@60's 1.63x is a real if modest response and must not be refused;
    640x480@60's 1.05x is the density control, where the grating sits below
    Nyquist and there is nothing to find.
    """
    assert not phase_pair.is_flat(3.01, 1.0)
    assert not phase_pair.is_flat(1.63, 1.0)
    assert phase_pair.is_flat(1.05, 1.0)
