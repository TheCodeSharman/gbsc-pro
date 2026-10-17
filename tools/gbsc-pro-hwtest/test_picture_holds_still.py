"""The emitted picture holds still over time, on both sync types.

THE GOAL: the picture stays where the solve put it. A source that acquires and
frames correctly can still be unwatchable, and every other acceptance here
judges a COLLAPSED clip or a single frame -- `card_edges` takes each column's
greenest moment across the frames so the card's flashing ring cannot hide a
border, which erases a transient before anything sees it. Nothing asserted that
the picture holds still until this.

**BOTH LEGS ARE GREEN, AND THE COMPOSITE ONE WAS RED UNTIL THE SEPARATOR AND
THE ADC PLL REFERENCED THE LEADING EDGE OF THE PULSE.** It was never an xfail:
an open defect with nothing driving it out, measured on `vga` at 320x256@50
into 1080p as the picture jumping vertically in 17% to 53% of frames at up to
7 jumps a second, against a separate-sync floor of 0%. The floor is what this
still asserts on both legs, so the roll cannot come back unmeasured.
docs/investigations/the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md

**THE SEPARATE LEG IS THE CONTROL AND THE RUN IS WORTHLESS WITHOUT IT.** The
dongle is itself a sink and can drop or tear frames, so a jump count with no
floor beside it says nothing about the board. Both legs run in the SAME
session, on one source mode, one output and one capture, so the only thing
differing between them is the sync arrangement.

The measurement is `picture_stability`', shared with the tool that reads it
interactively, so there is one statement of what holding still means.

Changes the SOURCE's sync type, so it needs --modeserv and --source, and it
puts the bench mode, the sync type and the output back however the module ends.
Each leg costs about half a minute: a sync-type change with the re-probe that
makes the engine follow it, the engine settling, and eight seconds of capture.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit
import hdmi_capture
import picture_stability

BENCH_MODE = picture_stability.BENCH_MODE

# The output both legs are judged at. A roll is measured in OUTPUT rows, so two
# legs at two resolutions are two different rulers. 1080p because it is what
# the rest of the acceptance suite uses.
OUTPUT = "1080p"

# Long enough to RATE a few-per-second event rather than merely to see one. At
# the 60 frames a second the dongle delivers this is about 480 frames.
SECONDS = 8.0

SYNC_TYPES = ((False, "separate"), (True, "composite"))


@pytest.fixture(scope="module")
def capture_device():
    """The dongle's node, by USB id. Skipped where it is unplugged, a missing
    capture being not a stability fault."""
    try:
        return hdmi_capture.device()
    except SystemExit as why:
        pytest.skip(str(why))


@pytest.fixture(scope="module")
def judged_at(request, host):
    """One output for both legs, and the bench put back however it ends.

    The shape is deliberately left alone. A declared shape narrows the room on
    one axis and the measurement is a VERTICAL displacement, so it cannot bias
    the reading -- and leaving it saves an excursion that would re-acquire the
    link between the legs. The stored framing is left alone for the same
    reason: what is judged is whether the picture moves, not where it sits.
    """
    where = request.config.getoption("--modeserv")
    original = {
        "output": gbs_unit.setting(host, "output"),
        "sync": gbs_unit.mode_serv(where, "SYNC") if where else "",
    }
    assert gbs_unit.choose_output(host, OUTPUT), (
        f"the output never reached {OUTPUT}, so the legs would be measured "
        f"against different rasters")

    yield

    if where:
        gbs_unit.mode_serv(where, "SYNC 1"
                           if " SYNC 1 " in original["sync"] + " " else "SYNC 0")
        gbs_unit.mode_serv(where, f"MODE {BENCH_MODE}")
        gbs_unit.show_card(where)
    if original["output"]:
        gbs_unit.restore_output_mode(host, original["output"])


def clip_once_acquired(host, where, dev, csync):
    """A clip of the emitted frame with the engine on `csync`, and the state it
    was taken in.

    **A VOID CLIP IS RE-TAKEN ONCE.** Composite comes back from the re-probe
    with no picture at all about half the time -- an acquisition fault, not a
    stability one -- and a leg that reports `no picture` says nothing about
    whether the picture holds still. One retry turns that into a reading;
    twice voided is reported as it stands. docs/known-issues.md
    """
    for attempt in range(2):
        sog = picture_stability.on_sync_type(host, where, csync)
        if sog is None:
            return None, 0.0, "never acquired, or never stopped re-solving"
        rows, span = picture_stability.clip_over(dev, SECONDS)
        if rows is None:
            return None, 0.0, "no frames from the capture device"
        judged = picture_stability.judge(rows, span)
        if judged.lost < len(rows):
            return judged, span, f"SP_SOG_MODE {sog}"
        if attempt:
            return judged, span, f"SP_SOG_MODE {sog}, voided twice"
    return None, 0.0, "unreachable"


@pytest.mark.source_mode
@pytest.mark.parametrize("csync,name", SYNC_TYPES,
                         ids=[name for _, name in SYNC_TYPES])
def test_the_picture_holds_still(request, host, source, capture_device,
                                 judged_at, csync, name):
    where = request.config.getoption("--modeserv")
    assert gbs_unit.modeserv_ok(gbs_unit.mode_serv(where, f"MODE {BENCH_MODE}")), \
        f"the source refused {BENCH_MODE!r}"

    judged, _, state = clip_once_acquired(host, where, capture_device, csync)
    if judged is None:
        pytest.fail(f"{name}: {state}")

    # EVERY LEG, not only the one that fails: a floor recorded as a pass and
    # nothing else cannot be compared against a later run, and the rate is the
    # whole point.
    read = f"{picture_stability.reading(judged)}   {state}"
    print(f"\n{name}: {read}")
    assert judged.why is None, f"{judged.why}   [{read}]"
