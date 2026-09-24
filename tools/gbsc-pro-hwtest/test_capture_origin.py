"""Where the capture window may first take video, against a live unit.

The IF line is counted from a hsync edge, and a window may not open on every
unit of it. Two terms are excluded, and CaptureWindow::firstCapture() is their
sum:

  * head blanking of 22 units, applied only where the line doubler IS in
    circuit: the capture path writes past the hsync pulse there, and a window
    opened at the pulse's end captures that as saturated green;
  * the hsync pulse, which is only at the head of the line while the pulse is
    positive-going. STATUS_SYNC_PROC_HSPOL says which, and where it reads 0 the
    origin is the pulse's trailing edge, the sync interval is already behind it,
    and excluding it again throws away video.

THERE IS NO CAPTURE LAG. This suite carried one of 72 units for four modes'
worth of measurement, and the whole of it was SP_HS_LOOP_SEL taking the sync
retiming out of circuit -- engaging the retiming accounts for 77.4 counter
units against the 77.6 the correction applied.
docs/investigations/the-capture-lag-was-the-retiming-bypassed.md

Every AKF50 640x480 and 1280x480 mode is sync_pol 3, so an inverted pulse is one
`printf 'MODE X640 Y480 C256 F60\\n' | nc <riscpc> 6502` away.

docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md

Read-only: it reads registers and /geometry and presses nothing.
"""

import math
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get_json, locked_steadily, read_fields, wait_for

# Tv5725::VideoSourceLine::DoubledHeadBlankingUnits, which only a doubled line
# carries.
DOUBLED_HEAD_BLANKING_UNITS = 22

# The sync processor's own validity window for the hsync duty, and what the
# retimer is configured for when the reading falls outside it.
DUTY_MIN, DUTY_MAX, FALLBACK_DUTY = 0.041, 0.152, 0.07

FIELDS = ["PLLAD_MD", "IF_HSYNC_RST", "IF_LD_RAM_BYPS",
          "STATUS_SYNC_PROC_HLOW_LEN", "STATUS_SYNC_PROC_HSPOL"]


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


def sync_units(at):
    duty = at["STATUS_SYNC_PROC_HLOW_LEN"] / at["PLLAD_MD"] if at["PLLAD_MD"] else 0.0
    if duty < DUTY_MIN or duty > DUTY_MAX:
        duty = FALLBACK_DUTY
    return math.ceil((at["IF_HSYNC_RST"] + 1) * duty)


def first_capture(at):
    """What the engine SOLVED as its earliest capturable unit.

    Taken from /geometry rather than re-derived from the registers: the duty
    behind it is read once at solve time and drifts by an ADC sample afterwards,
    so a fresh derivation lands one unit out and reports a defect that is not
    there.
    """
    return at["geometry"]["fh"]


def test_the_capture_opens_past_what_the_path_writes_over(solved):
    doubled = solved["IF_LD_RAM_BYPS"] == 0
    blanking = DOUBLED_HEAD_BLANKING_UNITS if doubled else 0
    head_guard = sync_units(solved) if solved["STATUS_SYNC_PROC_HSPOL"] else 0
    due = blanking + head_guard

    # The floor never reaches zero: measured at 640x480@60, whose pulse is
    # behind the origin, IF_HB_SP2 at 0 doubles and smears the picture.
    due = max(due, 1)

    assert first_capture(solved) == due, (
        f"line doubled {doubled}, hsync positive "
        f"{bool(solved['STATUS_SYNC_PROC_HSPOL'])}, sync {sync_units(solved)} units: "
        f"first capture should be {due}, engine reports {first_capture(solved)}")


def test_an_inverted_pulse_does_not_cost_a_sync_width(solved):
    """The head guard belongs to the polarity that puts the pulse at the head.

    On the other one the sync interval is behind the origin, so a guard there is
    video thrown away -- 86 units of it on 640x480@75, 132 on 640x480@60.
    """
    if solved["STATUS_SYNC_PROC_HSPOL"]:
        pytest.skip("hsync is positive-going: the pulse really is at the head")

    assert first_capture(solved) < sync_units(solved), (
        f"hsync is inverted, so the {sync_units(solved)}-unit pulse is behind the "
        f"origin, but the capture still starts at {first_capture(solved)}")


def test_only_a_doubled_line_is_blanked_at_the_head(solved):
    """Head blanking belongs to the doubled path alone, because only there does
    the capture path write past the pulse.
    """
    doubled = solved["IF_LD_RAM_BYPS"] == 0
    head_guard = sync_units(solved) if solved["STATUS_SYNC_PROC_HSPOL"] else 0
    without_sync = first_capture(solved) - head_guard

    if doubled:
        assert without_sync == DOUBLED_HEAD_BLANKING_UNITS, (
            f"doubled, so the head blanking alone is due: "
            f"{DOUBLED_HEAD_BLANKING_UNITS}, engine reports {without_sync}")
    else:
        # Nothing is written past an undoubled line's pulse, so the floor is the
        # pulse and the one-unit clamp under it.
        assert without_sync <= 1, (
            f"undoubled, so nothing but the pulse is due, "
            f"engine reports {without_sync} past it")
