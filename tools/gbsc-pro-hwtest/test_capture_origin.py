"""Where the capture window may first take video, against a live unit.

The IF line is counted from a hsync edge, and video does not arrive at that
edge. Two terms separate the two:

  * a capture-path lag of ~72 units, measured on four undoubled modes, which is
    not applied where the line doubler is in circuit -- IF_HBIN_SP is that
    FIFO's own line reset there and places the picture itself;
  * the hsync pulse, which is only at the head of the line while the pulse is
    positive-going. STATUS_SYNC_PROC_HSPOL says which, and where it reads 0 the
    origin is the pulse's trailing edge, the sync interval is already behind it,
    and excluding it again throws away video.

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

# Tv5725::VideoSourceLine.
CAPTURE_LAG_UNITS = 72
WRITE_LIMIT_UNITS = 1125

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
    """What the engine reports as its earliest capturable unit.

    /geometry gives the capturable span rather than its ends, and the far end is
    the line's own wrap or the write limit, whichever is tighter.
    """
    units = at["IF_HSYNC_RST"] + 1
    last = min(units - 2, WRITE_LIMIT_UNITS)
    return last - at["geometry"]["ch"]


def test_the_capture_starts_a_lag_after_the_sync_edge(solved):
    doubled = solved["IF_LD_RAM_BYPS"] == 0
    lag = 0 if doubled else CAPTURE_LAG_UNITS
    head_guard = sync_units(solved) if solved["STATUS_SYNC_PROC_HSPOL"] else 0

    assert first_capture(solved) == lag + head_guard, (
        f"line doubled {doubled}, hsync positive "
        f"{bool(solved['STATUS_SYNC_PROC_HSPOL'])}, sync {sync_units(solved)} units: "
        f"first capture should be {lag + head_guard}, engine reports "
        f"{first_capture(solved)}")


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
