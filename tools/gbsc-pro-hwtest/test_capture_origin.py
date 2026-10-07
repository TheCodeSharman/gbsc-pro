"""Where the capture window may first take video, against a live unit.

The IF line is counted from a hsync edge, and a window may not open on every
unit of it. Two terms are excluded, and CaptureWindow::firstCapture() is their
sum:

  * head blanking of 22 units, applied only where the line doubler IS in
    circuit: the capture path writes past the hsync pulse there, and a window
    opened at the pulse's end captures that as saturated green;
  * the hsync pulse, which is at the head WHATEVER POLARITY THE SOURCE SENDS.
    SyncProcessor::normaliseHsyncPolarity() inverts a high-active one before the
    count is taken, so every source reaches this counter as a low-active pulse on
    the leading edge, and compensating for the polarity here applies it a second
    time.
    docs/investigations/the-capture-floor-followed-a-normalised-polarity.md

THERE IS NO CAPTURE LAG. This suite carried one of 72 units for four modes'
worth of measurement, and the whole of it was SP_HS_LOOP_SEL taking the sync
retiming out of circuit -- engaging the retiming accounts for 77.4 counter
units against the 77.6 the correction applied.
docs/investigations/the-capture-lag-was-the-retiming-bypassed.md

docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md

Read-only: it reads registers and /geometry and presses nothing.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get_json, locked_steadily, read_fields, wait_for

# Tv5725::VideoSourceLine::DoubledHeadBlankingUnits, which only a doubled line
# carries.
DOUBLED_HEAD_BLANKING_UNITS = 22

# Tv5725::HsyncPulse's own window on the duty. NOTHING IS SUBSTITUTED OUTSIDE
# IT: an HsyncPulse has been judged a pulse where it was taken, so a reading
# outside this means the engine has not solved against the source in front of
# it, and asserting a floor against a guess is what this suite must not do.
DUTY_MIN, DUTY_MAX = 0.010, 0.152

# Tv5725::CaptureWindow::FirstCapturableUnit. IF_HB_SP2 at 0 doubles and smears
# the picture, and 1 is clean with every other register identical.
FIRST_CAPTURABLE_UNIT = 1

FIELDS = ["PLLAD_MD", "IF_LD_RAM_BYPS", "STATUS_SYNC_PROC_HLOW_LEN"]


@pytest.fixture
def solved(host):
    """A source the engine has measured and solved a capture window for.

    Waits rather than skips: a skip on an unlocked source reads as a pass.
    """
    assert wait_for(lambda: locked_steadily(host), timeout=90.0, interval=1.0), \
        "no locked source after 90 s; the engine has solved no capture window to check"
    at = read_fields(host, FIELDS)
    at["geometry"] = get_json(host, "/geometry")[1]
    return at


def duty_of(at):
    """The sync low time as a fraction of the line, STRAIGHT FROM THE REGISTER.

    A plausibility reading only: it is what the counter holds now, uncorrected
    for polarity, so it says whether a pulse is arriving and not where the
    engine placed anything. sync_units() is the placed value.
    """
    return at["STATUS_SYNC_PROC_HLOW_LEN"] / at["PLLAD_MD"] if at["PLLAD_MD"] else 0.0


def sync_units(at):
    """The sync interval the engine SOLVED against, in IF units.

    Taken from /geometry rather than derived from duty_of(), which cannot
    reproduce it: SyncProcessor::hsyncPulseSamples() folds the pulse against its
    complement and takes InvertedPulseWidthSamples off a high-active source, so
    the register over the divider is five samples wide. Measured at 320x256@50
    with PLLAD_MD 2200, HSPOL 1: HLOW_LEN 156 derives 79 units where the engine
    used 151 samples and placed 76.

    Re-deriving it here is also the copy this file is not allowed to carry --
    the correction is the firmware's and a second copy of it diverges silently.
    """
    return at["geometry"]["sh"]


def first_capture(at):
    """What the engine SOLVED as its earliest capturable unit.

    Taken from /geometry rather than re-derived from the registers: the duty
    behind it is read once at solve time and drifts by an ADC sample afterwards,
    so a fresh derivation lands one unit out and reports a defect that is not
    there.
    """
    return at["geometry"]["fh"]


def test_the_source_reads_as_a_pulse_at_all(solved):
    """Or every floor below is asserted against a duty the engine refused."""
    duty = duty_of(solved)
    assert DUTY_MIN <= duty <= DUTY_MAX, (
        f"the sync width reads {solved['STATUS_SYNC_PROC_HLOW_LEN']} of "
        f"{solved['PLLAD_MD']}, a duty of {duty:.4f}, outside the "
        f"{DUTY_MIN}..{DUTY_MAX} an HsyncPulse is judged on -- so the engine has "
        f"not solved against the source in front of it")


def test_the_capture_opens_past_what_the_path_writes_over(solved):
    doubled = solved["IF_LD_RAM_BYPS"] == 0
    blanking = DOUBLED_HEAD_BLANKING_UNITS if doubled else 0
    due = max(blanking + sync_units(solved), FIRST_CAPTURABLE_UNIT)

    assert first_capture(solved) == due, (
        f"line doubled {doubled}, sync {sync_units(solved)} units: "
        f"first capture should be {due}, engine reports {first_capture(solved)}")


def test_only_a_doubled_line_is_blanked_at_the_head(solved):
    """Head blanking belongs to the doubled path alone, because only there does
    the capture path write past the pulse.
    """
    doubled = solved["IF_LD_RAM_BYPS"] == 0
    without_sync = first_capture(solved) - sync_units(solved)

    if doubled:
        assert without_sync == DOUBLED_HEAD_BLANKING_UNITS, (
            f"doubled, so the head blanking alone is due: "
            f"{DOUBLED_HEAD_BLANKING_UNITS}, engine reports {without_sync}")
    else:
        # Nothing is written past an undoubled line's pulse, so the floor is the
        # pulse and the one-unit clamp under it.
        assert without_sync <= FIRST_CAPTURABLE_UNIT, (
            f"undoubled, so nothing but the pulse is due, "
            f"engine reports {without_sync} past it")
