"""The pads, against a live unit.

The firmware is the implementation. These tests assert the properties its
geometry has to hold -- the windows hug the picture, the picture fills the
raster, nothing is inherited -- against the capture and the raster the unit
actually holds after a press.

**NOTHING HERE RE-IMPLEMENTS THE SOLVE.** A second implementation in Python is
not a specification: it goes stale against the firmware, and every disagreement
then reads as a firmware fault when it is a maintenance failure in the copy.
What these assert are RELATIONSHIPS the firmware's own output must satisfy --
one register against another, and against the measured
`produced = capture x 1024 / scale`.

Needs --source, and needs a source the unit is SCALING. In RGBHV bypass the VDS
is out of the video path and there is no geometry to solve, so these skip rather
than fail -- see docs/rgbhv-bypass-trap.md.

NOT FROZEN, deliberately. Freezing tests the port in isolation and passes while
the unit is unusable in normal operation: applyBestHTotal moves VDS_HSYNC_RST
and slides the blanking windows by diffHTotal/2, which the picture does not
follow, so unwritten memory shows at the right-hand edge. That was on the bench
on 2026-08-08 with the whole suite green. The pads have to hold up against the
firmware that actually runs beside them.

Instead of freezing, each press settles: the geometry is polled until two
consecutive reads agree, so the comparison is against a state that has stopped
moving rather than one caught mid-write.
"""

import collections
import math
import os
import re
import subprocess
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe
from gbs_unit import (GEOMETRY_GATED, RESET_COMMAND, field_from, framing_matches,
                      framing_of, fs_read, get, get_json, locked_steadily,
                      read_field, read_reg, read_segment, recover_lock,
                      reset_framing, resolve, wait_for, write_reg)

GEOMETRY_FIELDS = [
    ("IF_HB_SP2", 1, 0x1A, 0, 11), ("IF_HB_ST2", 1, 0x18, 0, 11),
    ("IF_VB_SP", 1, 0x1E, 0, 11), ("IF_VB_ST", 1, 0x1C, 0, 11),
    ("IF_HSYNC_RST", 1, 0x0E, 0, 11),
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12), ("VDS_VSYNC_RST", 3, 0x02, 4, 11),
    ("VDS_HSCALE", 3, 0x16, 0, 10), ("VDS_VSCALE", 3, 0x17, 4, 10),
    ("VDS_HSCALE_BYPS", 3, 0x00, 4, 1), ("VDS_VSCALE_BYPS", 3, 0x00, 5, 1),
    ("VDS_HB_SP", 3, 0x05, 4, 12), ("VDS_HB_ST", 3, 0x04, 0, 12),
    ("VDS_VB_SP", 3, 0x08, 4, 11), ("VDS_VB_ST", 3, 0x07, 0, 11),
    ("VDS_DIS_HB_SP", 3, 0x11, 4, 12), ("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
    ("VDS_DIS_VB_SP", 3, 0x14, 4, 11), ("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
    ("VTOTAL", 0, 0x1B, 0, 11),
]

# One press asks for a number of OUTPUT PIXELS, not input units, and not a
# proportion. Must match Geometry::ControlSteps in src/tv5725/ControlSteps.h.
#
# **NOT INPUT UNITS, AND NOT A PROPORTION.** An input unit is worth
# `magnification` pixels on screen, so a fixed unit step gets coarser the further
# you zoom in, and the two axes move by different amounts for the same press
# whenever their magnifications differ -- x1.315 across and x2.107 down on this
# bench, 60% for one keypress. One unit of zoom is one pixel of the output
# screen.
PAN_STEP_PX = 8
ZOOM_STEP_PX = 8

# InputLine::WriteLimitUnits. Past it the capture path writes blanking rather
# than video, so it bounds the tail of the capture window and, through
# SourceMeasurement::recommendedDivider(), the divider itself. docs/capture-limits.md
WRITE_LIMIT_UNITS = 1125


# The smallest change of capture POSITION each axis's hardware acts on. Mirrors
# Tv5725::Axis::captureGranularity. Horizontally 2: the low bit of IF_HB_SP2 does
# nothing, measured 2026-08-17 by toggling the capture start and watching the
# picture -- 162 <-> 163 does not move it, 162 <-> 164 does.
HORIZONTAL_GRANULE = 2
VERTICAL_GRANULE = 1


def units_for(pixels, scale_reg, granularity=HORIZONTAL_GRANULE):
    """Output pixels -> input units: what Tv5725::Axis::stepUnits computes.

    magnification = 1024 / VDS_?SCALE, and ONE GRANULE IS THE FLOOR -- a request
    that rounds below it would be a press the hardware ignores. Rounded ONCE, in
    output pixels: rounding to units first and to granules after biases every
    request upwards. Reading the scale off the chip rather than assuming one
    keeps this honest at any magnification.
    """
    if scale_reg == 0:              # a dropped read, not a setting
        wanted = abs(pixels)
    else:
        wanted = abs(pixels) * scale_reg / 1024
    granules = max(1, round(wanted / granularity))
    units = granules * granularity
    return -units if pixels < 0 else units

# Scale::Unity, and the two axes' constructor arguments from
# src/tv5725/Axis.cpp. These are BOUNDS the firmware's output must respect --
# nothing here recomputes where the picture lands, which is the part that goes
# stale.
SCALE_UNITY = 1024

OutputAxis = collections.namedtuple(
    "OutputAxis",
    "name capture_sp capture_st window_sp window_st display_sp display_st "
    "scale raster_rst window_sp_min")

AXIS_HORIZONTAL = OutputAxis(
    "horizontal", "IF_HB_SP2", "IF_HB_ST2", "VDS_HB_SP", "VDS_HB_ST",
    "VDS_DIS_HB_SP", "VDS_DIS_HB_ST", "VDS_HSCALE", "VDS_HSYNC_RST", 8)
AXIS_VERTICAL = OutputAxis(
    "vertical", "IF_VB_SP", "IF_VB_ST", "VDS_VB_SP", "VDS_VB_ST",
    "VDS_DIS_VB_SP", "VDS_DIS_VB_ST", "VDS_VSCALE", "VDS_VSYNC_RST", 0)
OUTPUT_AXES = (AXIS_HORIZONTAL, AXIS_VERTICAL)


def framing(host):
    """The engine's framing: where the capture window starts and how far it
    runs, per axis, in input units. /geometry also reports the capturable region
    those are taken against and what it measured of the source, neither of which
    is framing."""
    status, parsed = get_json(host, "/geometry")
    if status == 404:
        pytest.skip(GEOMETRY_GATED)
    return framing_of(parsed) if status == 200 else None


def moved_off(value, poison):
    """True once a field holds something other than the poison written into it.

    None is a read that did not arrive, not a value: register reads defer to
    loop() and time out for as long as loop() is busy, which is precisely while
    a preset load is running. A bare `!= poison` is satisfied by that timeout and
    reports the poison overwritten by the very thing that was too busy to answer.
    """
    return value is not None and value != poison


def probe_for(host):
    """bench_probe's read-modify-write over this unit's HTTP. There is no
    /setfield route -- only whole registers -- and every geometry field shares
    its register with something else, so a write that drops a neighbour lands as
    a fault somewhere unrelated."""
    return bench_probe.Probe(
        lambda segment, register: read_reg(host, segment, register),
        lambda segment, register, value: write_reg(host, segment, register, value))


def settled_geometry(host, timeout=8.0):
    """The geometry once it has stopped moving.

    Unfrozen, a read can land between the engine's write and the sync watcher's
    answer to it. Two consecutive agreeing reads means the firmware has finished
    having its say -- and if it never settles, that is itself the finding.
    """
    deadline = time.monotonic() + timeout
    previous = read_geometry(host)
    while time.monotonic() < deadline:
        time.sleep(0.4)
        current = read_geometry(host)
        if current is not None and current == previous:
            return current
        previous = current
    return previous


def restore_the_preset_the_suite_expects(host):
    """Wait out the preset load a teardown just fired.

    **A teardown that triggers a preset load and returns has not finished.**
    doPostPresetLoadSteps() runs for seconds afterwards, and the next test starts
    immediately -- so its setup lands in the middle of a load that is still
    writing. Measured 2026-08-14: test_the_subsystems_own_the_fifo_watermarks
    failed a full-suite run at its own poison step, reading back 24 and 61 for
    PB_MAST_FLAG_REG and PB_GENERAL_FLAG_REG, which are precisely the values
    FrameBuffer::apply() writes. The poison had been overwritten before the test
    got to check it had landed. In isolation the same poison survives 20 s
    untouched, which is why nothing caught this until the whole suite ran.

    The geometry is the right thing to wait on rather than the registers the next
    test cares about, and the ordering is what makes it sound: the subsystems run
    early in doPostPresetLoadSteps() and geometry.solveFromScratch() is the last
    thing in it. Settled geometry therefore implies FrameBuffer::apply() and
    InputFormatter::apply() are long done.

    Generously timed because this is a whole preset load, not a pad press.
    """
    settled_geometry(host, timeout=25.0)


def read_geometry(host, attempts=3):
    """Every geometry field, from three whole-segment reads.

    Field at a time was ~40 HTTP round trips per call and dropped reads often
    enough to skip tests that had nothing wrong with them. /getregs fetches a
    segment in one request, so this is three -- and a dropped one is retried
    rather than reported as "could not read the geometry".
    """
    for _ in range(attempts):
        segments = {}
        for number in (0, 1, 3):
            segments[number] = read_segment(host, number)
            if segments[number] is None:
                break
        else:
            return {name: field_from(segments[segment], register, offset, width)
                    for name, segment, register, offset, width in GEOMETRY_FIELDS}
    return None




@pytest.fixture
def probe(host):
    return probe_for(host)


# How long to give the source to come back before deciding the pads cannot be
# tested. Covers a detection pass, which is two 6000 ms getVideoMode() spins.
LOCK_WAIT_S = 40.0


@pytest.fixture
def scaling(host, source):
    """The geometry under test, or a skip saying why there is none.

    Bypass is the expected reason: an RGBHV source over 535 lines is trapped
    there, VDS_?SYNC_RST reads 0, and the VDS is out of the video path
    entirely. That is not a failure of the pads -- there is nothing for them to
    do. The bench 800x600 (VTOTAL 627) hits it every boot.
    """
    # **A PAD PRESS MADE WHILE THE SOURCE IS OUT IS ACCEPTED AND MOVES NOTHING.**
    # An unseeded framing is seeded from the placement the line can realise, and
    # ActiveImage::clampToLine() seeds nothing when the line it is handed measures
    # zero -- so the press applies to a framing of 0+0, which clamps back to 0+0.
    # The firmware says so and is the only thing that does: the console prints
    # `ADJ horizontalZoom +8px -> framing h 0+0/1044 ... IF_VB 2..0` against a
    # working press's `h 54+969/1044 ... IF_VB 38..620`, with the same registers
    # and the same /geometry reading either way.
    #
    # So a longer press budget does not help, and reading the framing back does
    # not either. docs/investigations/a-press-into-an-unmeasured-source-is-swallowed.md
    if not wait_for(lambda: locked_steadily(host), timeout=LOCK_WAIT_S):
        pytest.skip(
            f"the sync processor is not counting the source after {LOCK_WAIT_S}s, "
            "so a pad press would be accepted and move nothing -- the state a "
            "preceding detection pass leaves behind, not a fault in the pads")

    state = read_geometry(host)
    if state is None:
        pytest.skip("could not read the geometry registers")
    if state["VDS_HSYNC_RST"] < 64 or state["VDS_VSYNC_RST"] < 64:
        pytest.skip(
            f"the unit is in bypass (VDS_HSYNC_RST {state['VDS_HSYNC_RST']}, "
            f"VDS_VSYNC_RST {state['VDS_VSYNC_RST']}): no geometry to solve. "
            "Put the source in a mode under 535 lines -- the bench 320x256@50 "
            "at VTOTAL 311 scales. See docs/rgbhv-bypass-trap.md")
    if state["IF_HB_ST2"] <= state["IF_HB_SP2"]:
        pytest.skip("the horizontal capture window is empty; nothing to solve")
    return state


@pytest.fixture
def framed(host, probe, scaling):
    """The geometry at a KNOWN framing, reached the way a user reaches it.

    Each test states the state it needs instead of saving and restoring
    whatever it found, so nothing depends on what ran before it. The engine's
    state is the framing; the registers are an output of it, which is why
    writing registers back restores nothing -- the next press recomputes from a
    framing nobody reset. Getting that wrong let eleven zoom steps accumulate
    across a suite while every test believed it had made one.

    Through the reset control rather than an absolute set, because a route the
    user does not have is a door this suite must not use either: arranged that
    way, a test stops exercising the system anyone else can reach.
    """
    reset_the_framing(host)
    return settled_geometry(host)


# What a reset lands on, learned by doing one. It is the placement the solve
# computes for the source in force, so it is not a constant that can be written
# down -- and holding it is what lets every later reset be CHECKED. A source
# mode change during a run invalidates it, and fails loudly rather than
# silently, which is the right way round.
_default_framing = None


def reset_the_framing(host):
    """Put the framing back to default, and CHECK IT LANDED.

    A 200 from /sc only means the command was queued into a global that loop()
    has yet to read, so it is no evidence the reset ran -- and a reset that
    silently does nothing leaves every test running from whatever framing the
    one before it left, which is the accumulation this fixture exists to stop.
    """
    global _default_framing
    landed = reset_framing(host, expect=_default_framing)
    assert landed is not None, (
        f"/sc?{RESET_COMMAND} left the framing at {framing(host)} rather than "
        f"{_default_framing or 'the default for this source'}: "
        "the reset control did not run.")
    _default_framing = landed


# What a press moves first, so there is something to wait on.
WATCH = {"z": ("IF_HB_ST2", 1, 0x18, 0, 11),
         "h": ("IF_HB_ST2", 1, 0x18, 0, 11),
         "+": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         "-": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         # The OSD and IR 'move' keys, which reach the same handler.
         "6": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         "7": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         # The vertical zoom pads, which move the vertical capture instead.
         "4": ("IF_VB_ST", 1, 0x1C, 0, 11),
         "5": ("IF_VB_ST", 1, 0x1C, 0, 11)}


# The smallest capture move the hardware acts on, per axis. Horizontally the low
# bit of IF_HB_SP2 does nothing, so a one-unit move leaves the picture where it
# was; Axis::stepUnits() never returns less than one of these.
GRANULE = {"horizontal": 2, "vertical": 1}


def press(host, probe, command, pixels=None, timeout=6.0):
    """Issue a pad press and wait for it to land.

    /sc only sets `serialCommand`; web_service() consumes it from loop() on its
    next tick, so a 200 means queued, not done. And only ONE command fits in
    that global -- a second press before the first is consumed is a press thrown
    away, which would make a repeated-press test quietly measure fewer presses
    than it made.

    `pixels` is the press magnitude in OUTPUT pixels; None asks for the pad's own
    step.
    """
    spec = WATCH[command]
    before = probe.read_field(spec)
    path = f"/sc?{command}" if pixels is None else f"/sc?{command}={pixels}"
    status, _ = get(host, path)
    assert status == 200, f"{path} returned {status}"
    landed = wait_for(lambda: probe.read_field(spec) != before, timeout=timeout)
    assert landed, (f"{path} did not move {spec[0]} from {before} "
                    f"within {timeout}s -- was the press absorbed?")


# How many presses it can take to walk a control from wherever a test found it
# to its stop. Not a property of the control: the zoom floor is raster / 4 and
# the raster is COMPUTED, so the travel moves when the output does. High enough
# that saturating is what ends the loop, and asserted at each call so a limit
# which has fallen behind fails loudly instead of testing a half-zoomed picture.
ZOOM_PRESSES_TO_A_STOP = 120
PAN_PRESSES_TO_A_STOP = 200


def press_until_saturated(host, probe, command, limit=24):
    """Press until the control stops moving, and report how many landed.

    press() asserts every press moves something, which is right for a test about
    ONE press and wrong for a test that walks a control to its end -- a zoom has
    a finite range and reaching it is correct behaviour, not a fault.

    That range is not a constant, which is why this counts rather than assuming.
    AxisHorizontal magnifies at most Scale::Unity / Scale::Min, so the smallest
    capture that fills the raster is ceil(raster / that). The raster is COMPUTED,
    so a wider one leaves less zoom travel and no press count can be hardcoded.
    """
    spec = WATCH[command]
    landed = 0
    for _ in range(limit):
        before = probe.read_field(spec)
        status, _ = get(host, f"/sc?{command}")
        assert status == 200, f"/sc?{command} returned {status}"
        if not wait_for(lambda: probe.read_field(spec) != before, timeout=6.0):
            break
        landed += 1
    return landed


def windows_following_the_capture(state):
    """What the output windows get wrong, given the capture the unit holds.

    Read back AFTER the press rather than taken from what was asked for, so a
    firmware that ignored the press cannot pass by agreeing with itself. That a
    press moves the capture at all is asserted separately.

    The picture's WIDTH is checkable exactly: Scale::produced is
    `capture x 1024 / scale`, measured on the bench with no loss term at either
    end, and Axis::solve truncates it. Its POSITION is not, because the front
    porch clamping the far edge comes from the solved raster and no register
    carries it -- so the far edge is BOUNDED rather than predicted, in the
    direction that matters: a window longer than the picture shows scratch,
    where one shorter is invisible.
    """
    wrong = []
    for axis in OUTPUT_AXES:
        capture = state[axis.capture_st] - state[axis.capture_sp]
        scale = state[axis.scale]
        window_sp, window_st = state[axis.window_sp], state[axis.window_st]
        display_sp, display_st = state[axis.display_sp], state[axis.display_st]
        raster = state[axis.raster_rst] + 1

        if capture <= 0:
            wrong.append(f"{axis.name}: the capture window is empty "
                         f"({axis.capture_st} {state[axis.capture_st]}, "
                         f"{axis.capture_sp} {state[axis.capture_sp]})")
            continue
        if not 0 < scale <= SCALE_UNITY:
            wrong.append(f"{axis.name}: {axis.scale} is {scale}, which is no "
                         f"magnification at all")
            continue

        produced = capture * SCALE_UNITY // scale

        # Axis::solve: the memory window IS the display window, allocating
        # nothing spare. Memory past the picture is memory the playback stage
        # still walks, and taking the whole raster shows as artefacts down the
        # LEFT edge.
        if window_st != display_st:
            wrong.append(f"{axis.name}: {axis.window_st} {window_st} != "
                         f"{axis.display_st} {display_st}, so the memory window "
                         f"no longer hugs the display window")

        # Axis::solve() allocates nothing spare, so equality is the design and
        # only a window WIDER than the picture opens onto unwritten memory.
        if display_st - display_sp > produced:
            wrong.append(f"{axis.name}: the display window is "
                         f"{display_st - display_sp} wide but a capture of "
                         f"{capture} at scale {scale} produces only {produced}, "
                         f"so it opens past the picture onto unwritten memory")

        if display_st <= display_sp:
            wrong.append(f"{axis.name}: the display window is empty "
                         f"({axis.display_sp} {display_sp}, "
                         f"{axis.display_st} {display_st})")

        # Axis::windowStopMin -- below it the near edge corrupts.
        if window_sp < axis.window_sp_min:
            wrong.append(f"{axis.name}: {axis.window_sp} {window_sp} is below "
                         f"the write floor {axis.window_sp_min}")

        # Axis::farBound -- ST registers wrap rather than clamp, and a wrapped
        # VDS_VB_ST rolls the frame.
        if display_st > raster - 2:
            wrong.append(f"{axis.name}: {axis.display_st} {display_st} is past "
                         f"the raster's last usable position {raster - 2}")
    return wrong


@pytest.mark.zoom
def test_the_reset_control_returns_the_framing_to_default(host, probe, scaling):
    """/sc?<reset> puts the framing back to default and re-solves from the source.

    The framing is the engine's state and the registers are an output of it, so
    nothing else can put a test back at a known starting point: writing the
    registers back restores nothing, because the next press recomputes from a
    framing nobody reset.

    **The character is the whole risk here.** web_service() parks '@' in
    serialCommand to mean "nothing pending" and guards its switch on
    serialCommand != '@', so a case labelled '@' is unreachable -- the route
    answers 200, loop() consumes nothing, and the framing stays where the last
    press left it. That is invisible to every other test in this file: they
    would go on passing against an inherited framing, which is exactly the
    accumulation the fixture was written to prevent.
    """
    default = reset_framing(host)
    assert default is not None, "the framing did not settle after a reset"

    press(host, probe, "z")
    moved = framing(host)
    assert moved != default, (
        f"a zoom press left the framing at {moved}, so this cannot show a reset "
        f"returning it to {default}")

    assert reset_framing(host, expect=default) is not None, (
        f"the framing stayed at {framing(host)} rather than returning to "
        f"{default}: the reset control did not run")


@pytest.mark.pan
def test_a_press_moves_by_the_pixels_it_is_given(host, probe, framed):
    """A press carries its own magnitude, and one output pixel is the finest move.

    Without it the smallest step reachable from outside is the pad's own, which
    is magnification-scaled -- so an instrument creeping a boundary one unit at a
    time cannot do it through the controls, and reaches for a setter the user
    does not have.

    Axis::stepUnits() floors at one capture granule, so a press of one pixel is
    the smallest move the hardware acts on whatever the scale happens to be.
    """
    before = framing(host)["oh"]
    press(host, probe, "+", pixels=1)
    fine = framing(host)["oh"] - before
    assert fine == GRANULE["horizontal"], (
        f"a one-pixel pan moved {fine} units, not one granule "
        f"({GRANULE['horizontal']}): the magnitude did not reach the control")

    at = framing(host)["oh"]
    press(host, probe, "+")
    default = framing(host)["oh"] - at
    assert default > fine, (
        f"the pad's own step moved {default} units against the fine press's "
        f"{fine}, so a press with no magnitude is no longer the coarse one")


def test_a_zoom_press_leaves_the_windows_following_the_capture(host, probe, framed):
    """End to end: press the pad, read the registers back, and check every
    output window still follows the capture the unit holds."""
    press(host, probe, "z")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    wrong = windows_following_the_capture(after)
    assert not wrong, "after a zoom press:\n  " + "\n  ".join(wrong)


@pytest.mark.pan
def test_a_pan_press_leaves_the_windows_following_the_capture(host, probe, framed):
    press(host, probe, "+")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    wrong = windows_following_the_capture(after)
    assert not wrong, "after a pan press:\n  " + "\n  ".join(wrong)


@pytest.mark.zoom
def test_a_zoom_press_actually_moves_the_capture(host, probe, framed):
    """The measured fault this replaces: 100 invocations of /sc?z moved HSCALE
    by 16 counts, because the internal guards absorbed ~92% of them. One press,
    one step -- no press may be absorbed."""
    before = framed
    press(host, probe, "z")
    after = settled_geometry(host)

    step = units_for(ZOOM_STEP_PX, before["VDS_HSCALE"])
    was = before["IF_HB_ST2"] - before["IF_HB_SP2"]
    now = after["IF_HB_ST2"] - after["IF_HB_SP2"]
    assert now < was, f"the capture did not narrow: {was} -> {now}"
    assert was - now == step, (
        f"one press is {ZOOM_STEP_PX} output pixels, which at HSCALE "
        f"{before['VDS_HSCALE']} is {step} input units, but the capture "
        f"narrowed by {was - now}")


@pytest.mark.pan
def test_a_pan_press_moves_the_capture_without_resizing_it(host, probe, framed):
    """A pan that changed the width would resize the picture as a side effect."""
    before = framed
    press(host, probe, "+")
    after = settled_geometry(host)

    step = units_for(PAN_STEP_PX, before["VDS_HSCALE"])
    assert after["IF_HB_SP2"] - before["IF_HB_SP2"] == step, (
        f"one press is {PAN_STEP_PX} output pixels, which at HSCALE "
        f"{before['VDS_HSCALE']} is {step} input units")
    assert after["IF_HB_ST2"] - after["IF_HB_SP2"] \
        == before["IF_HB_ST2"] - before["IF_HB_SP2"]


def test_every_horizontal_pan_press_moves_the_picture(host, probe, framed):
    """A press must move the capture by something the hardware acts on.

    The low bit of IF_HB_SP2 does nothing, so a press worth an odd number of
    units leaves the picture where it was. That reads as a dead remote: at the
    OSD's one-pixel step it took two presses to see anything move, while the
    register changed on every one and nothing in a dump looked wrong.

    Three presses rather than one, because a single even step could be even by
    luck at this magnification.
    """
    before = framed
    starts = [before["IF_HB_SP2"]]
    for _ in range(3):
        press(host, probe, "+")
        starts.append(settled_geometry(host)["IF_HB_SP2"])

    steps = [b - a for a, b in zip(starts, starts[1:])]
    assert all(s != 0 for s in steps), (
        f"a press did not move the capture at all: IF_HB_SP2 went {starts}")
    assert all(s % HORIZONTAL_GRANULE == 0 for s in steps), (
        f"IF_HB_SP2 went {starts}, stepping {steps}. A step that is not a "
        f"multiple of {HORIZONTAL_GRANULE} is a press the hardware ignores")


# The playback burst, which is not geometry and is owned by the engine anyway.
#
# Segment 4 is not in GEOMETRY_FIELDS and deliberately stays out of it: these
# are read a field at a time so a failure names the register it is about.
PB_FETCH_NUM = ("PB_FETCH_NUM", 4, 0x39, 0, 10)
PB_CAP_OFFSET = ("PB_CAP_OFFSET", 4, 0x37, 0, 10)
VDS_HSYNC_RST = ("VDS_HSYNC_RST", 3, 0x01, 0, 12)
IF_HSYNC_RST = ("IF_HSYNC_RST", 1, 0x0E, 0, 11)

# What a preset load leaves behind, and what every torn picture of 2026-08-09 was
# photographed in. Seeded as the hostile state, never expected as an answer.
UPSTREAM_FETCH = 256
UPSTREAM_OFFSET = UPSTREAM_FETCH + 4


# Mirrors Tv5725::Memory. The fetch has to cover the source pixels each output
# line needs, over the playback request budget; the offset deliberately does not
# follow it, being clean anywhere across 190..256 when it was measured.
REQUESTS_PER_LINE = 4
FETCH_FLOOR = 150
FETCH_MAX = 512


def burst_units(units):
    """Tv5725::Memory::fetchFor -- requests rounded UP, then floored and capped.

    Rounded up because a line one pixel short of its source still fails to
    finish, and it repeats: the start of the picture reappearing at the right.
    """
    return min(FETCH_MAX, max(FETCH_FLOOR, -(-units // REQUESTS_PER_LINE)))


def expected_burst(line_units, capture):
    """The pair Tv5725::Memory computes: fetch from the CAPTURE, stride from the
    whole IF LINE.

    **NEITHER IS GATED ON THE OUTPUT RASTER, AND THIS MIRRORED A GATE ON BOTH.**
    Memory::fetchFor takes the raster and ignores it -- the gate was dropped
    because a preset load onto a 1435 px raster switched the rule off, leaving
    PB_FETCH_NUM at upstream's 256 against a capture of 1185 that needed 297.

    The stride was a raster-gated constant here long after the firmware stopped
    using one. Sizing it from the line rather than the framing is what keeps it
    still through a zoom: the fetch follows the capture, the capture GROWS as the
    picture zooms out, and a stride sized for one framing is passed by the fetch
    partway through the range -- after which lines overlap in the buffer and each
    overwrites its predecessor's tail.
    """
    return burst_units(capture), burst_units(line_units)


@pytest.mark.zoom
def test_a_zoom_press_leaves_the_playback_burst_at_the_measured_value(
        host, probe, framed):
    """PB_FETCH_NUM matches the capture after a zoom, because a zoom is when it
    changes.

    A zoom changes the capture width, which changes how many source pixels
    playback must read per output line -- so the burst has to be resized at
    exactly the moment the picture is being re-solved around it. Written short,
    the line does not finish and repeats; that is the recorded failure at 236
    against a capture of 1009, which needed 253.

    The class of fault this asserts against is not "is the formula right", which
    only the bench can say, but "does the firmware own the register at all" --
    left unwritten it keeps an upstream preset table's value for a different
    output mode.
    """
    press(host, probe, "z")
    after = settled_geometry(host)

    line_units = probe.read_field(IF_HSYNC_RST) + 1
    capture = after["IF_HB_ST2"] - after["IF_HB_SP2"]
    fetch, offset = expected_burst(line_units, capture)
    assert probe.read_field(PB_FETCH_NUM) == fetch, (
        f"the playback burst does not cover a capture of {capture} on a "
        f"{line_px} px output line -- the line cannot finish and repeats")
    assert probe.read_field(PB_CAP_OFFSET) == offset


@pytest.mark.zoom
def test_a_zoom_press_repairs_a_playback_burst_a_preset_left_behind(
        host, probe, framed):
    """The test above can pass on a register nobody wrote. This one cannot.

    256/260 is not an arbitrary hostile value: it is exactly what a preset load
    puts there, and exactly the state every torn picture of 2026-08-09 was
    photographed in. Writing it back is reproducing the fault, and one pad press
    has to clear it -- the same rule as every other register here, compute it,
    never inherit it.

    Deranging tears the picture for as long as it takes the press to land, which
    is why this is behind --source with the rest of them.
    """
    probe.write_field(PB_FETCH_NUM, UPSTREAM_FETCH)
    probe.write_field(PB_CAP_OFFSET, UPSTREAM_OFFSET)
    assert probe.read_field(PB_FETCH_NUM) == UPSTREAM_FETCH, (
        "could not seed the fault, so this proves nothing")

    line_units = probe.read_field(IF_HSYNC_RST) + 1

    # The press MOVES the capture, so what to expect can only be computed from
    # the framing the press lands on -- not from the one it started in.
    press(host, probe, "z")
    after = settled_geometry(host)
    capture = after["IF_HB_ST2"] - after["IF_HB_SP2"]
    fetch, offset = expected_burst(line_units, capture)
    if fetch == UPSTREAM_FETCH:
        pytest.skip(f"a capture of {capture} wants upstream's own value, so "
                    f"there is no repair to observe")

    landed = wait_for(lambda: probe.read_field(PB_FETCH_NUM) == fetch,
                      timeout=8.0)
    assert landed, (
        f"a zoom press left PB_FETCH_NUM at "
        f"{probe.read_field(PB_FETCH_NUM)} -- the engine does not own it, and "
        f"the picture tears with whatever the last preset load happened to put "
        f"there")
    assert probe.read_field(PB_CAP_OFFSET) == offset


@pytest.mark.zoom
def test_zooming_out_and_back_does_not_walk_the_capture_away(host, probe, framed):
    """Out and back is not EXACTLY reversible, and cannot be: a press asks for
    output pixels and converts at the magnification in force, which differs on
    the way out and on the way back, so the rounding does not cancel.

    What has to hold is that the error is BOUNDED rather than accumulating -- an
    irreversible control is how a hand-aligned picture is lost. It is bounded by
    a mechanism rather than by luck: a narrower capture means a larger
    magnification means FEWER input units per press, so the conversion pushes
    back against its own drift.

    Measured 2026-08-09, five 4-out/4-in cycles from rest, widths 1009 -> 1008,
    1002, 1002, 1002, 1008: it wanders inside about one press and comes back. Two
    cycles here rather than one, because a single cycle cannot tell a bounded
    wobble from the first step of a walk.
    """
    before = framed
    step = units_for(ZOOM_STEP_PX, before["VDS_HSCALE"])
    width = lambda g: g["IF_HB_ST2"] - g["IF_HB_SP2"]

    def out_and_back():
        for _ in range(4):
            press(host, probe, "h")
        for _ in range(4):
            press(host, probe, "z")
        return settled_geometry(host)

    once = out_and_back()
    twice = out_and_back()

    # Two press-steps: the measured band is about one, and a control that walks
    # would be past this by the second cycle rather than sitting inside it.
    band = 2 * step
    assert abs(width(once) - width(before)) <= band, (
        f"one out-and-back moved the capture width "
        f"{width(before)} -> {width(once)}, more than {band} units")
    assert abs(width(twice) - width(before)) <= band, (
        f"the error accumulates: width {width(before)} -> {width(once)} "
        f"-> {width(twice)} over two out-and-back cycles")


@pytest.mark.zoom
def test_the_memory_window_never_lands_below_its_floor(host, probe, framed):
    """VDS_HB_SP under 8 corrupts the display. Checked after zooming hard in,
    which is where the picture is biggest and the near edge tightest."""
    for _ in range(6):
        press(host, probe, "z")
    after = settled_geometry(host)

    assert after["VDS_HB_SP"] >= AXIS_HORIZONTAL.window_sp_min


# --- the capture window may never take the hsync pulse ------------------------

# The pulse is at the HEAD, its width read from the source on every solve, and
# the TAIL IS DELIBERATELY UNBOUNDED -- both asserted below rather than left to
# chance. docs/scaler-geometry-model.md "The two green regions in an IF line".

HLOW_LEN = ("HLOW_LEN", 0, 0x19, 0, 12)
PLLAD_MD = ("PLLAD_MD", 5, 0x12, 0, 12)

# The sync processor's own validity window for the duty, from
# gbs-control.ino:4858. HLOW_LEN is a segment 0 live measurement and rails.
DUTY_MIN, DUTY_MAX = 0.041, 0.152


def sync_units(probe, units):
    """What the hsync pulse takes off the head of a `units` long IF line, or a
    skip: with the duty unmeasurable the firmware falls back to a constant, and
    a test that cannot measure it either has nothing to check against."""
    hlow = probe.read_field(HLOW_LEN)
    adc_line = probe.read_field(PLLAD_MD)
    if not adc_line:
        pytest.skip("PLLAD_MD reads 0; the ADC PLL divider is not set")
    duty = hlow / adc_line
    if not DUTY_MIN < duty < DUTY_MAX:
        pytest.skip(f"hsync duty {duty:.4f} (HLOW_LEN {hlow} of {adc_line}) is "
                    "outside the sync processor's validity window, so the "
                    "firmware is using its fallback and there is nothing to "
                    "compare against")
    return math.ceil(units * duty)


@pytest.mark.zoom
def test_a_zoomed_out_capture_never_takes_the_hsync_pulse(host, probe, framed):
    """Without a floor, zoom-out widens the capture through the blanking and
    into the sync, which arrives as green. Asking for far more zoom-out than the
    line can give is the press that reaches it."""
    units = framed["IF_HSYNC_RST"] + 1
    guard = sync_units(probe, units)

    landed = press_until_saturated(host, probe, "h", limit=ZOOM_PRESSES_TO_A_STOP)
    assert landed < ZOOM_PRESSES_TO_A_STOP, (
        f"zoom-out was still moving after {landed} presses, so this never "
        "reached the stop it is about")
    after = settled_geometry(host)

    assert after["IF_HB_SP2"] >= guard, (
        f"the capture starts at {after['IF_HB_SP2']}, inside the {guard} unit "
        f"hsync pulse at the head of a {units} unit line")


@pytest.mark.zoom
def test_a_zoomed_out_capture_takes_the_tail_down_to_whichever_bound_is_lower(
        host, probe, framed):
    """The other half of the same decision. Zoom-out must reach the end of what
    can be captured -- that is the reach which recovers active video the 0.76
    default active fraction crops -- and stop there.

    Two bounds meet at the tail and the lower one wins. The wrap is units - 2,
    not units - 1: a stop ON the reset value stops the input formatter producing
    pixels and freezes the picture, so InputLine stands off by one more. The
    write limit is the other: past it the capture path writes blanking instead
    of video, which destroys picture rather than showing it, and SourceMeasurement caps
    the divider so an ordinary line stays inside it.
    See InputLine::lastCapture() and docs/capture-limits.md."""
    units = framed["IF_HSYNC_RST"] + 1
    reach = min(units - 2, WRITE_LIMIT_UNITS)

    landed = press_until_saturated(host, probe, "h", limit=ZOOM_PRESSES_TO_A_STOP)
    assert landed < ZOOM_PRESSES_TO_A_STOP, (
        f"zoom-out was still moving after {landed} presses, so this never "
        "reached the stop it is about")
    after = settled_geometry(host)

    assert after["IF_HB_ST2"] == reach, (
        f"the capture stops at {after['IF_HB_ST2']} of a {units} unit line, "
        f"not the {reach} that the wrap and the write limit between them allow")


@pytest.mark.pan
def test_panning_to_the_left_stop_never_takes_the_hsync_pulse(host, probe, framed):
    """The same bound, reached the other way. capture() clamps the window and
    clampToLine() clamps the framing, and they are separate arithmetic -- a pan
    that stops one unit short of where a zoom stops is the dead zone of
    2026-08-09 wearing a different hat."""
    units = framed["IF_HSYNC_RST"] + 1
    guard = sync_units(probe, units)

    landed = press_until_saturated(host, probe, "-", limit=PAN_PRESSES_TO_A_STOP)
    assert landed < PAN_PRESSES_TO_A_STOP, (
        f"the pan was still moving after {landed} presses, so this never "
        "reached the stop it is about")
    left = settled_geometry(host)
    assert left["IF_HB_SP2"] >= guard, (
        f"panned to the left stop the capture starts at {left['IF_HB_SP2']}, "
        f"inside the {guard} unit hsync pulse")


# --- the playback burst -------------------------------------------------------

# The runtime half of the scheme in src/tv5725/Memory.h: PB_FETCH_NUM must cover
# the source pixels each output line needs, over the playback request budget.
# Short of that the line does not finish reading and repeats, visibly, as the
# start of the picture reappearing at the right.
#
# WHAT THIS CANNOT DO IS LOOK AT THE PICTURE. Whether a framing tears is a
# judgement from the screen. So this asserts the property the firmware
# is supposed to hold, not that the result is clean, and a green run here is not
# evidence the tearing is gone.


@pytest.mark.zoom
def test_the_playback_burst_covers_the_capture_at_every_zoom_position(host, probe,
                                                                     framed):
    """The floor, checked at every position the pad reaches.

    Walks rather than samples. The fault alternates on roughly one input unit
    and every pattern ever found across a coarser grid was later refuted as an
    aliasing artefact -- sweep_zoom.py's docstring records four of them.

    The two recorded failures are both floor violations: 236 written where
    capture 1009 needed 253 wrapped the picture, and 200 at capture 931 shredded
    it. Both would fail here.
    """
    offenders = []
    for _ in range(12):
        press(host, probe, "z")
        after = settled_geometry(host)
        capture = after["IF_HB_ST2"] - after["IF_HB_SP2"]
        needed = burst_units(capture)
        got = probe.read_field(PB_FETCH_NUM)
        if got < needed:
            offenders.append((capture, got, needed))

    assert not offenders, (
        "the playback burst is short of the source pixels the line needs at "
        f"{offenders} -- (capture, PB_FETCH_NUM, required)")


@pytest.mark.zoom
def test_the_playback_burst_tracks_the_zoom_rather_than_sitting_still(host, probe,
                                                                     framed):
    """It has to MOVE, which is the whole change.

    A constant was defensible while the floor was unknown and is not now: held
    still, the fetch is either short of the capture at the wide end or far above
    it at the deep end, and the beat between capture writes and playback reads
    walks with the zoom. Tracking the capture takes HSCALE out of that ratio.

    Walks the control to its end rather than pressing a fixed 24 times. The zoom
    range is a function of the raster, and the engine now COMPUTES the raster --
    at 1915 the horizontal saturates after 16 presses, where the preset table's
    1436 had room for about 61. Running out of travel is correct behaviour; a
    fetch that never moved on the way there is not.
    """
    seen = set()
    landed = 0
    while True:
        moved = press_until_saturated(host, probe, "z", limit=1)
        if not moved:
            break
        landed += 1
        settled_geometry(host)
        seen.add(probe.read_field(PB_FETCH_NUM))
        if landed >= 24:
            break

    assert landed >= 4, (
        f"only {landed} zoom press(es) moved the capture before it saturated, "
        "which is too little travel to say anything about the burst. The raster "
        "may have been computed far wider than the source can fill")
    assert len(seen) > 1, (
        f"PB_FETCH_NUM stayed at {seen} across {landed} zoom presses -- it is "
        "either not being written or the capture is not reaching "
        "Memory::fetchFor")


@pytest.mark.zoom
def test_the_memory_window_never_uncovers_the_display_window(host, probe, framed):
    """VDS_HB_ST below VDS_DIS_HB_ST shows unwritten memory at the right of the
    screen -- a DIFFERENT fault from the beat, and one that photographs like it.

    The trim is bounded by the display window precisely so the two can never be
    traded against each other, and the unit was left in exactly that state on
    2026-08-09 with every register reading what was asked for.
    """
    for _ in range(8):
        press(host, probe, "z")
        after = settled_geometry(host)
        assert after["VDS_HB_ST"] >= after["VDS_DIS_HB_ST"], (
            f"memory window ends at {after['VDS_HB_ST']}, display window at "
            f"{after['VDS_DIS_HB_ST']} -- the right-hand edge is unwritten "
            "memory")



# The SDRAM memory map, owned by Tv5725::FrameBuffer. Word addresses: 2^21
# words of 32 bits = the 8 MB part the schematic fits.
MEMORY_MAP = [
    ("RFF_WFF_STA_ADDR_A", 4, 0x51, 0, 21, 0x000000),
    ("WFF_SAFE_GUARD_A", 4, 0x44, 0, 21, 0x052000),
    ("WFF_SAFE_GUARD_B", 4, 0x47, 0, 21, 0x052000),
    ("WFF_SAFE_GUARD", 4, 0x42, 3, 1, 1),
    ("PB_CAP_BUF_STA_ADDR_A", 4, 0x31, 0, 21, 0x060000),
    ("PB_CAP_BUF_STA_ADDR_B", 4, 0x34, 0, 21, 0x060000),
    ("CAP_SAFE_GUARD_A", 4, 0x24, 0, 21, 0x1FFFFF),
    ("CAP_SAFE_GUARD_B", 4, 0x27, 0, 21, 0x1FFFFF),
    ("CAP_SAFE_GUARD_EN", 4, 0x21, 5, 1, 1),
]


# The SDRAM bus, owned by Tv5725::MemoryBus. The clock the bus runs at and the
# nanosecond trims that compensate this board's traces -- none of them a property
# of the output mode, all four measured:
#   - The three delays were swept to both ends of their range at 129.6MHz with
#     the picture unchanged at every step, under the heaviest playback load the
#     unit has. A positive control -- MEM_RD_LAT_PIP forced to 0 -- visibly
#     corrupted the picture, so the path was live throughout and the clean sweep
#     is evidence rather than an artefact.
#   - 162MHz is the fastest of the eight PLL_MS codes that stays in spec for the
#     EM638325TS-6: tCK3 6ns against 6.17 actual, and tRCD/tRP 18ns against the
#     18.5ns three clocks give. 185MHz and 216MHz break all three.
#     docs/EM638325-Industrial_Rev-3.2.pdf
#
# **PLL_MS IS DELIBERATELY NOT HERE, AND IT IS THE ONE FIELD THAT FAILS.**
# MemoryBus::init() writes SdramTimings::fastestInSpec(), which is 3 -- 162MHz.
# Three sketch functions write 2, which is not a frequency at all but the pad
# FEEDBACK clock: setResetParameters() and both bypass switches. Coming back from
# bypass the bring-up writes 3 and one of those writes 2 after it, so the frame
# buffer runs on FBCLK while the scaler is using it. Measured 2 of 3 runs.
# Asserting it here would make this test intermittently red for a defect that
# belongs to the bypass work, so it is recorded instead.
# docs/investigations/pll-ms-has-four-writers.md
MEMORY_BUS = [
    ("MEM_FK_RD_DLY", 4, 0x04, 0, 3, 2),
    ("MEM_DATA_DLY_REG", 4, 0x18, 0, 3, 0),
    ("MEM_CLK_DLY_REG", 4, 0x1B, 4, 3, 4),
    ("MEM_ACT_CYCLE", 4, 0x05, 0, 2, 1),
    ("MEM_PCHG_CYCLE", 4, 0x05, 4, 2, 1),
]


# The FIFO request watermarks and the line-double reset position, owned by
# Tv5725::FrameBuffer and Tv5725::InputFormatter.
WATERMARKS = [
    ("PB_MAST_FLAG_REG", 4, 0x2C, 0, 6, 24),
    ("PB_GENERAL_FLAG_REG", 4, 0x2D, 0, 6, 61),
    ("RFF_MASTER_FLAG", 4, 0x4E, 0, 6, 36),
    ("RFF_GENERAL_FLAG", 4, 0x4F, 0, 6, 60),
    ("WFF_LINE_FLIP", 4, 0x4A, 4, 1, 1),
    ("IF_LD_ST", 1, 0x0C, 1, 4, 5),
    ("WFF_FF_HALF_REQ", 4, 0x42, 1, 1, 0),
]


BROUGHT_UP = [
    ("the memory map, Tv5725::FrameBuffer", MEMORY_MAP),
    ("the bus timing, Tv5725::MemoryBus", MEMORY_BUS),
    ("the FIFO watermarks, Tv5725::FrameBuffer and Tv5725::InputFormatter",
     WATERMARKS),
]


# Single-byte poisons, chosen so no value is one the firmware writes.
#
# The three memory-map entries are whole BYTES because those fields are 21 bits
# across three registers, and changing one byte is enough to make the field
# differ. The rest sit inside one byte each and are poisoned as fields.
MAP_BYTE_POISON = {0x21: 0x43, 0x33: 0x0A, 0x26: 0x15}
FIELD_POISON = {
    (4, 0x04, 0, 3): 5,
    (4, 0x18, 0, 3): 3, (4, 0x1B, 4, 3): 6,
    (4, 0x2C, 0, 6): 7, (4, 0x2D, 0, 6): 9,
    (4, 0x4E, 0, 6): 11, (4, 0x4F, 0, 6): 13,
    (4, 0x4A, 4, 1): 0, (4, 0x42, 1, 1): 1,
    (1, 0x0C, 1, 4): 9,
}

# PB_CAP_BUF_STA_ADDR_A's high byte, which the poison sets to 0x0A and only a
# bring-up sets back to 0x06. The witness that BringUp::init() ran at all, as
# opposed to the assertions below, which are about what it wrote.
BRING_UP_WITNESS = (4, 0x33, 0x06)

# HSOUT/VSOUT. Dropping them and bringing them back makes the encoder re-acquire.
PAD_SYNC_OUT_ENZ = (0, 0x49, 2, 1)


def _poison_within_a_byte(host, spec, value):
    """A field that sits inside one register, read-modify-written so the
    neighbours sharing that byte survive."""
    segment, register, offset, width = spec
    raw = read_reg(host, segment, register)
    assert raw is not None, f"could not read s{segment}_{register:02x} to poison it"
    mask = ((1 << width) - 1) << offset
    write_reg(host, segment, register, (raw & ~mask) | (value << offset))


def _make_the_encoder_reacquire(host):
    """Drop HSOUT/VSOUT and bring them back.

    **A BYPASS EXCURSION LEAVES THIS BENCH WITHOUT A PICTURE WITHOUT IT.** The
    MS9288A samples the analog output and does not always notice the timing under
    it moved, so it carries on transmitting the mode it locked to before and the
    television reports no signal while every register reads correct. The firmware
    has this as useHdmiSyncFix, armed only where the input classification swaps
    inside the SD 50/60 families -- returning from bypass is not one of those, so
    the recovery has to be made here. docs/investigations/encoder-stale-timing.md
    """
    segment, register, offset, _ = PAD_SYNC_OUT_ENZ
    for level in (1, 0):
        raw = read_reg(host, segment, register)
        if raw is None:
            return
        write_reg(host, segment, register,
                  (raw & ~(1 << offset)) | (level << offset))
        time.sleep(2)


def test_arming_the_chip_brings_the_subsystems_back(host, source):
    """A chip that has been ARMED gets its subsystem registers back.

    **THE TRIGGER IS AN ARM, NOT A PRESET LOAD, AND THAT IS THE WHOLE POINT.**
    The bring-up runs when the chip is armed -- at boot, and from either bypass
    switch, which reconfigure it away from the scaling setup. An ordinary mode
    change does not repeat it. Measured: poisoned and then given a preset load,
    PB_CAP_BUF_STA_ADDR_A's high byte stays at the poison; poisoned and then
    given a bypass excursion, all three groups come back.

    **POISONED FIRST, AND THAT IS THE REST OF THE POINT.** The TV5725 keeps its
    registers across an ESP reboot and a reflash, so reading the right value
    proves nothing on its own -- it may be what was already there. With the preset
    tables gone the poison is the only reference there is, and every field below
    discriminates because of it.

    It is also a two-owner guard, which no host test can be. FrameBuffer::apply()
    wrote nine registers and eight landed, because a second uncommented
    CAP_SAFE_GUARD_EN write further down the same function won by running later.
    Only reading the chip after a real arm catches that -- and it caught one:
    PLL_MS, which is left out of MEMORY_BUS above with the reason.
    """
    for register, value in MAP_BYTE_POISON.items():
        write_reg(host, 4, register, value)
    for spec, value in FIELD_POISON.items():
        _poison_within_a_byte(host, spec, value)

    landed = {f"s4_{r:02x}": read_reg(host, 4, r) for r in MAP_BYTE_POISON}
    assert landed == {f"s4_{r:02x}": v for r, v in MAP_BYTE_POISON.items()}, (
        f"could not poison the memory map, got {landed} -- without this the test "
        "cannot tell a fresh write from a leftover")
    unpoisoned = {spec: read_field(host, *spec) for spec, value in FIELD_POISON.items()
                  if read_field(host, *spec) != value}
    assert not unpoisoned, (
        f"these fields did not take the poison: {unpoisoned} -- without it the "
        "test cannot tell a fresh write from a leftover")

    try:
        get(host, "/sc?k")          # RGBHV bypass, which arms the bring-up
        time.sleep(6)
        get(host, "/sc?~")          # and back, where the armed bring-up runs

        segment, register, want = BRING_UP_WITNESS
        assert wait_for(lambda: read_reg(host, segment, register) == want,
                        timeout=30.0), (
            f"s{segment}_{register:02x} never came back to 0x{want:02X} after the "
            "chip was armed: BringUp::init() did not run, or something overwrote "
            "what it wrote")

        wrong = []
        for label, group in BROUGHT_UP:
            for name, seg, reg, lo, width, want in group:
                got = read_field(host, seg, reg, lo, width)
                if got != want:
                    wrong.append(f"{label}: {name} (s{seg}_{reg:02x}) = 0x{got:X}, "
                                 f"want 0x{want:X}")
        assert not wrong, (
            "the bring-up did not leave these registers to their owners:\n  "
            + "\n  ".join(wrong))
    finally:
        recover_lock(host)
        _make_the_encoder_reacquire(host)
        reset_the_framing(host)



ENGINE_OUTPUTS = [
    ("IF_HB_SP2", 1, 0x1A, 0, 11),
    ("IF_HB_ST2", 1, 0x18, 0, 11),
    ("IF_VB_SP", 1, 0x1E, 0, 11),
    ("IF_VB_ST", 1, 0x1C, 0, 11),
    ("IF_LINE_ST", 1, 0x20, 0, 12),
    ("IF_LINE_SP", 1, 0x22, 0, 12),
    ("VDS_VSCALE", 3, 0x17, 4, 10),
    ("VDS_HSCALE", 3, 0x16, 0, 10),
    ("VDS_VSCALE_BYPS", 3, 0x00, 5, 1),
    ("VDS_HSCALE_BYPS", 3, 0x00, 4, 1),
    ("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
    ("VDS_DIS_VB_SP", 3, 0x14, 4, 11),
    ("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
    ("VDS_DIS_HB_SP", 3, 0x11, 4, 12),
    ("VDS_HB_ST", 3, 0x04, 0, 12),
    ("VDS_HB_SP", 3, 0x05, 4, 12),
    ("VDS_VB_ST", 3, 0x07, 0, 11),
    ("VDS_VB_SP", 3, 0x08, 4, 11),
    ("PB_CAP_OFFSET", 4, 0x37, 0, 10),
    ("PB_FETCH_NUM", 4, 0x39, 0, 10),
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12),
    ("VDS_VSYNC_RST", 3, 0x02, 4, 11),
    ("VDS_HS_ST", 3, 0x0A, 0, 12),
    ("VDS_HS_SP", 3, 0x0B, 4, 12),
    ("VDS_VS_ST", 3, 0x0D, 0, 11),
    ("VDS_VS_SP", 3, 0x0E, 4, 11),
]


# One engine output, poisoned so that a re-solve arriving at the same answer is
# still distinguishable from a re-solve that never ran. VDS_DIS_HB_ST is the far
# edge of the display window: a low value hides a strip of picture for as long as
# the poison stands and cannot take the lock, unlike the sync and sampling
# registers. Bottom-aligned, which _write_field() requires.
ENGINE_WITNESS = ("VDS_DIS_HB_ST", 3, 0x10, 0, 12)
POISON_OFFSET = 40


def _engine_outputs(host):
    return {name: read_field(host, seg, reg, lo, width)
            for name, seg, reg, lo, width in ENGINE_OUTPUTS}


# The file the framing table lives in, and how long the firmware waits for the
# framing to stop moving before it writes one.
FRAMING_FILE = "/framing.txt"
FRAMING_SAVE_QUIET_S = 15


def _framing_file_settled(host, interval=3.0):
    """The file's contents, once two reads apart agree. Wrapped in a tuple so
    an absent file -- a perfectly good settled state -- is not read as "not
    settled yet"."""
    first = fs_read(host, FRAMING_FILE)
    time.sleep(interval)
    return (first,) if fs_read(host, FRAMING_FILE) == first else None


@pytest.mark.zoom
def test_a_tuning_reaches_flash_only_after_it_settles(host, probe, source,
                                                      preset_save):
    """A pad press must not write flash, and a tuning must not be lost either.

    Walking a picture into place with the remote is tens of presses; one write
    each would spend the flash's life on a single adjustment. So the in-memory
    table follows every press and the FILE waits for the framing to hold still.
    docs/framing-presets.md
    """
    try:
        reset_the_framing(host)

        # Whatever ran before may have left a write pending, and it would land
        # inside the window below and read as a press having cost one. Wait for
        # the file to hold still first: the state this needs, set rather than
        # inherited.
        before = wait_for(lambda: _framing_file_settled(host),
                          timeout=FRAMING_SAVE_QUIET_S + 25.0, interval=2.0)
        assert before is not None, (
            f"{FRAMING_FILE} never stopped changing, so nothing here can tell a "
            "press that cost a write from one that did not")
        before = before[0]

        for _ in range(4):
            press(host, probe, "5", pixels=40)
        time.sleep(4)
        tuned = framing(host)

        # Still nothing written: the press alone must not have cost a write.
        assert fs_read(host, FRAMING_FILE) == before, (
            "the file changed within seconds of a press, so every press of a "
            "hold-to-repeat costs a flash write")

        settled = wait_for(
            lambda: (fs_read(host, FRAMING_FILE) or "") != (before or "")
                    or None,
            timeout=FRAMING_SAVE_QUIET_S + 20.0, interval=2.0)
        assert settled, (
            f"nothing was written to {FRAMING_FILE} within "
            f"{FRAMING_SAVE_QUIET_S + 20} s of the framing settling, so the "
            "tuning is lost at the next power cut")

        stored = fs_read(host, FRAMING_FILE)
        assert f"{read_field(host, 0, 0x1B, 0, 11)}@" in stored, (
            f"the source counted is not the one the record is keyed on:\n{stored}")
        assert tuned == framing(host), (
            "the framing moved while it was being written")
    finally:
        reset_the_framing(host)


# The output preset commands, and the raster each puts on the chip. /uc? queues
# one character for loop() to pick up.
OUTPUT_480P = "/uc?h"
OUTPUT_1080P = "/uc?s"


@pytest.mark.zoom
def test_changing_the_output_keeps_the_framing(host, probe, source, preset_load):
    """A user who has framed a source and then picks a different output
    resolution must not have to frame it again.

    applyPresets() is the one caller of Geometry::modeChanged(), and it runs for
    a source mode change and for this. Only the first invalidates a framing: the
    proportions are taken against the capturable region, which is a property of
    the input line, so the output raster moves neither the denominator nor the
    user's intent.

    Measured on the bench before the fix: cropped to a vertical extent of 441 of
    622 at 480p, an output change came back at 532 -- the untuned default, not
    even the new raster's clamp -- and switching back did not restore it.
    """
    try:
        reset_the_framing(host)
        for _ in range(6):
            press(host, probe, "5", pixels=40)
        time.sleep(4)

        tuned = framing(host)
        default_ev = _default_framing["ev"] if _default_framing else None
        assert default_ev is None or tuned["ev"] < default_ev, (
            f"the crop did not take: vertical extent {tuned['ev']} against a "
            f"default of {default_ev}, so this cannot show a framing surviving")

        get(host, OUTPUT_1080P)
        assert wait_for(lambda: (read_field(host, 3, 0x02, 4, 11) or 0) > 1000,
                        timeout=25.0), "the 1080p raster never landed"
        time.sleep(8)
        at_1080p = framing(host)

        get(host, OUTPUT_480P)
        assert wait_for(lambda: 0 < (read_field(host, 3, 0x02, 4, 11) or 0) < 1000,
                        timeout=25.0), "the 480p raster never came back"
        time.sleep(8)
        back = framing(host)

        # framing_matches, not equality: the units are taken against a live
        # measurement and one of them moves a unit either way on its own.
        # Losing the framing is a move of tens -- 441 -> 532 before the fix.
        assert framing_matches(at_1080p, tuned), (
            f"the output change moved the framing from {tuned} to {at_1080p}")
        assert framing_matches(back, tuned), (
            f"coming back moved it to {back} rather than the {tuned} it was "
            "tuned to")
    finally:
        get(host, OUTPUT_480P)
        time.sleep(8)
        recover_lock(host)
        reset_the_framing(host)


def test_a_preset_load_leaves_the_engines_values_not_the_sketchs(host, source):
    """After a preset load, every engine-owned register holds what the ENGINE
    computed -- not what doPostPresetLoadSteps() wrote on the way past.

    **This test is named in Engine.h and did not exist.** The comment on
    solveFromScratch() has cited
    test_a_preset_load_recomputes_every_register_from_scratch since the class was
    written; nothing by that name is in the tree. So the ordering invariant the
    whole design rests on -- table, then bring-up, then raster, then windows --
    has been asserted by nobody.

    It matters because doPostPresetLoadSteps() writes 41 values into registers
    the engine owns: IF_HB_ST2/SP2 alone eighteen times, plus VDS_VSCALE,
    VDS_DIS_VB_ST/SP, IF_VB_ST/SP, VDS_VSCALE_BYPS and PB_CAP_OFFSET. Every one
    is at a lower line number than the geometry.solveFromScratch() at the end of
    the same function, so the engine should overwrite all of them. "Should" is
    the word this test replaces.

    THE COMPARISON IS AGAINST THE ENGINE RE-SOLVING ALONE. Reading the registers
    after a preset load and calling them correct proves nothing -- they would
    look equally plausible if the sketch had won. So: load a preset, read
    everything; then ask for the framing that is already set, which makes
    loop() run Engine::applyRequested() with no preset load and no sketch writes
    anywhere near it; then read everything again. If the two agree, the engine
    was the last writer both times. If the sketch had won the first round, the
    second round would move.
    """
    try:
        _compare_engine_outputs_across_a_preset_load(host)
    finally:
        # **THIS TEST COSTS THE PICTURE FOR ABOUT A MINUTE, AND WITHOUT THIS IT
        # COSTS IT FOR GOOD.** The preset churn flips a separate-sync source onto
        # the csync path and it loses lock -- SP_VTOTAL steady at 97 -- and
        # reloading a preset does not undo it, because the sync type is decided
        # by a VSACT read that the csync path itself makes come out wrong.
        # /sc?~ forces a fresh detection pass, which does.
        # docs/sync-type-selection.md
        recover_lock(host)


def _compare_engine_outputs_across_a_preset_load(host):
    # **BOTH SIDES MUST BE THE SAME FRAMING, AND A PRESET LOAD DOES NOT SET ONE.**
    # A source with a remembered framing comes up on it and a preset load keeps
    # it, so a comparison started from whatever the last test left spans two
    # framings and every window and both scales differ -- measured at capture 747
    # against 973, VDS_HSCALE 431 against 557.
    reset_the_framing(host)
    time.sleep(4)

    get(host, "/sc?%29")  # a real preset load: table, sketch, bring-up, engine
    assert wait_for(lambda: (read_field(host, 3, 0x01, 0, 12) or 0) > 1000, timeout=20.0), (
        "no raster after the preset load, so there is nothing to compare")
    time.sleep(8)  # detection settles; CLAUDE.md says discard ~6 s
    after_preset = _engine_outputs(host)

    # **THE SECOND HALF RE-SOLVES THE FRAMING HELD; IT MUST NOT RESET IT.** A
    # preset load carries the framing proportions forward and the reset control
    # recomputes the default, and those two agree only while the capturable region
    # has not moved underneath them -- which it does by one unit, on its own.
    # docs/investigations/framing-is-anchored-to-a-measured-pulse.md
    #
    # **AND A 200 FROM /sc IS THE COMMAND QUEUED, NOT RUN.** A re-solve landing on
    # the values already there leaves no witness that it ran, which is the PASSING
    # case -- so without a poison this passes against firmware that ignored the
    # request entirely.
    poisoned = after_preset[ENGINE_WITNESS[0]]
    assert poisoned, f"could not read {ENGINE_WITNESS[0]} to poison it"
    poison = poisoned - POISON_OFFSET
    _write_field(host, ENGINE_WITNESS[1:], poison)
    assert read_field(host, *ENGINE_WITNESS[1:]) == poison, (
        f"could not poison {ENGINE_WITNESS[0]}, so a re-solve that never ran "
        "would be indistinguishable from one that agreed")

    assert resolve(host), "/sc?U was refused, so nothing re-solved"
    assert wait_for(lambda: read_field(host, *ENGINE_WITNESS[1:]) != poison,
                    timeout=20.0), (
        f"{ENGINE_WITNESS[0]} stayed at the poisoned {poison}: the engine never "
        "re-solved, so there is nothing to compare the preset load against")
    after_resolve = _engine_outputs(host)

    disagree = {name: (after_preset[name], after_resolve[name])
                for name in after_preset
                if after_preset[name] != after_resolve[name]}
    assert not disagree, (
        "these registers changed when the engine re-solved the SAME framing, so "
        "after the preset load they held something the engine did not compute -- "
        "doPostPresetLoadSteps() won the race for them:\n  "
        + "\n  ".join(f"{n}: preset-load {a} -> engine {b}"
                      for n, (a, b) in sorted(disagree.items())))


# The ADC sampling divider, owned by Tv5725::SourceMeasurement. One quantity, three
# registers, plus the one live counter that can witness whether it was LATCHED.
SAMPLING_PLLAD_MD = (5, 0x12, 0, 12)
SAMPLING_IF_HSYNC_RST = (1, 0x0E, 0, 11)
SAMPLING_SP_RT_HS_SP = (5, 0x4B, 0, 12)
SAMPLING_HTOTAL = (0, 0x17, 0, 12)  # STATUS_SYNC_PROC_HTOTAL
SAMPLING_SYNC_VTOTAL = (0, 0x1B, 0, 11)  # STATUS_SYNC_PROC_VTOTAL

# STATUS_SYNC_PROC_HTOTAL counts real ADC clocks per line, so it reports the
# divider the PLL is actually running. Measured on the bench, LOCKED, it reads
# 2553 against PLLAD_MD 2553 with an occasional 2554 -- so the tolerance is for
# a counter sampled against a live line, not for disagreement. A divider written
# after PLLAD_LAT rather than before it is out by hundreds.
SAMPLING_LATCH_TOLERANCE = 4

# **UNLOCKED, THIS REGISTER IS NOISE.** It counts whatever edges arrive, so with
# the source out it reports a steady value that looks exactly like a small latch
# error -- measured 2026-08-15 at 2558 against PLLAD_MD 2553, held across 22
# consecutive samples while SP_VTOTAL sat at 97/98 and HLOW_LEN threw 24..3004.
# Steady is not valid: ask whether SP_VTOTAL is counting.
SAMPLING_LOCKED_VTOTAL_MIN = 200

# How many times to try laying a poison down before calling it a fault. The
# firmware rewrites these registers on its own schedule, so losing one race is
# not evidence of anything.
POISON_ATTEMPTS = 3


def _settled_sampling(host, timeout=30.0):
    """The three sampling registers once they have stopped moving.

    **A PRESET LOAD IS NOT THE LAST THING THAT TOUCHES THESE.** Measured
    2026-08-15: five seconds after /sc?%29 the divider read 2553 with
    IF_HSYNC_RST already 1279 -- a mixture of two tables, and it looks exactly
    like the invariant being broken. Thirty seconds later it was a consistent
    2558 / 1279 / 2378. Nothing was wrong; the read was taken mid-transition
    while the sync watcher was still having its say, the same trap
    restore_the_preset_the_suite_expects() exists for one layer up. Two
    consecutive agreeing reads, and if it never settles that is the finding.
    """
    deadline = time.monotonic() + timeout
    previous = None
    while time.monotonic() < deadline:
        current = tuple(read_field(host, *spec) for spec in
                        (SAMPLING_PLLAD_MD, SAMPLING_IF_HSYNC_RST,
                         SAMPLING_SP_RT_HS_SP))
        if None not in current and current == previous:
            return current
        previous = current
        time.sleep(1.0)
    return previous


def _write_field(host, spec, value):
    """A field of up to 16 bits, read-modify-write on the high byte."""
    segment, register, offset, width = spec
    assert offset == 0, "only bottom-aligned fields are poisoned here"
    write_reg(host, segment, register, value & 0xFF)
    high = read_reg(host, segment, register + 1)
    assert high is not None, f"could not read s{segment}_{register + 1:02x}"
    keep = ~(((1 << width) - 1) >> 8) & 0xFF
    write_reg(host, segment, register + 1,
              (high & keep) | ((value >> 8) & ~keep & 0xFF))


@pytest.fixture
def poisoned_if_hsync_rst(host, source):
    """IF_HSYNC_RST holding a value the firmware cannot have chosen, checked
    back out however the test ends.

    **A fixture rather than the test's own teardown, because a skip has to
    check it back in too.** The divider test skips when the source has not
    relocked after its preset load, and that path leaves no failure behind to
    explain a poisoned unit.

    leave_the_bench_usable() does not cover this. That fixture rests on the
    engine recomputing whatever the tests derange, which holds for the geometry
    registers and not for this one: the divider is written only on a MODE
    CHANGE. So an un-restored poison outlives the run, an ESP reboot and a
    reflash, because the TV5725 keeps its registers across all three -- leaving
    the input formatter counting to the end of a line the ADC is not
    delivering, with every other register reading back correct.
    """
    # **A POISON NEEDS A QUIET UNIT, AND THE TEST BEFORE THIS ONE DOES NOT LEAVE
    # ONE.** recover_lock() fires /sc?~ and returns as soon as the source locks,
    # while detection is still running -- and detection rewrites all three of
    # these. Measured two seconds into one: PLLAD_MD read 1856 and then 1792
    # against the 2250 it settles on, so the poison is computed from a divider the
    # chip is not using, and the engine writes the settled IF_HSYNC_RST over it
    # before it can be read back. The fixture then fails on its own poison and the
    # test never runs.
    assert _settled_sampling(host), (
        "the sampling registers never stopped moving, so there is no settled "
        "divider to size a poison against")

    divider = read_field(host, *SAMPLING_PLLAD_MD)
    assert divider, "could not read PLLAD_MD, so there is no divider to poison"

    # **ONLY IF_HSYNC_RST IS POISONED, AND SP_RT_HS_SP MUST NOT BE.** The point
    # of a poison is to distinguish a fresh write from a leftover -- the TV5725
    # keeps its registers across an ESP reboot, so reading the right value proves
    # nothing on its own. One register is enough to establish that here, because
    # Tv5725::Geometry::writeSampling() writes all three from one held value or
    # writes none: if IF_HSYNC_RST came back computed, it ran, and the other two
    # assertions below are then about what it computed.
    #
    # That matters because SP_RT_HS_SP IS the sync processor's retime window, and
    # poisoning it takes the source out: measured twice, at 1110 against a
    # 2553-sample line and again only 100 low, SP_VTOTAL falls to a steady 97/98
    # and stays there through the preset load and minutes after it, needing /sc?~
    # to re-detect. The neighbouring tests poison segment 4, the memory map,
    # where a wild value costs a frame and not the lock.
    #
    # Settling is not quite enough on its own -- a write can still land in front
    # of one the firmware had already decided on -- so a poison that does not
    # stick is re-settled and tried again rather than failing the run.
    poison = divider // 2 - 100
    landed = None
    for _ in range(POISON_ATTEMPTS):
        _write_field(host, SAMPLING_IF_HSYNC_RST, poison)
        landed = read_field(host, *SAMPLING_IF_HSYNC_RST)
        if landed == poison:
            break
        _settled_sampling(host)
    assert landed == poison, (
        f"could not poison IF_HSYNC_RST in {POISON_ATTEMPTS} attempts, got "
        f"{landed} want {poison} -- without this the test cannot tell a fresh "
        f"write from a leftover")

    yield poison

    held = read_field(host, *SAMPLING_PLLAD_MD)
    if held:
        _write_field(host, SAMPLING_IF_HSYNC_RST, held // 2)


def test_the_sampling_divider_is_one_quantity_in_three_registers(
        host, poisoned_if_hsync_rst):
    """A preset load leaves PLLAD_MD, IF_HSYNC_RST and SP_RT_HS_SP agreeing.

    **AND THE FOURTH READING IS THE ONE THAT MATTERS.** The other three are all
    written by the firmware, so they agree with each other whenever one owner
    computes them -- which is what this pins. But PLLAD_MD is loaded into the ADC
    PLL by a rising edge on PLLAD_LAT, and between the write and that edge the
    register reports a divider the chip is NOT clocking at. Three self-consistent
    registers therefore prove nothing on their own: on 2026-08-15 PLLAD_MD read
    2210, the PLL ran 2553, IF_HSYNC_RST had been sized for 2210, and the display
    went solid green with every register reading plausible.

    STATUS_SYNC_PROC_HTOTAL is the only witness on the board. It counts ADC
    clocks per line, so it reports the LATCHED divider, and comparing it against
    PLLAD_MD is how a divider written on the wrong side of the latch becomes
    visible. CLAUDE.md says never to key anything on it because it merely echoes
    PLLAD_MD back -- that echo is exactly the property being used here, and it is
    a check on the write rather than a source for a derivation.

    IF_HSYNC_RST and SP_RT_HS_SP are poisoned first, for the reason the
    neighbouring tests poison: the TV5725 keeps its registers across an ESP
    reboot, so reading the right value proves nothing until a load has been
    forced over a wrong one. PLLAD_MD itself is NOT poisoned -- moving it is what
    --pllad-hostile exists for, and the picture would go with it.

    **THE POISON IS CHECKED IN, AND CHECKED OUT AGAIN.** Written without both,
    this passed in 3.4 s: settled_geometry() returns as soon as two consecutive
    reads agree, which they do before a load fired milliseconds earlier has begun
    writing, so it can sail past the load entirely and assert on the state that
    was already there. Waiting for the poison to be overwritten is the positive
    signal that the load actually ran -- the same shape as
    test_the_frame_buffer_subsystem_owns_the_memory_map waiting on
    PB_CAP_BUF_STA_ADDR_A.
    """
    poison_if = poisoned_if_hsync_rst

    get(host, "/sc?%29")  # a real preset load: table, sketch, bring-up, engine

    assert wait_for(
        lambda: read_field(host, *SAMPLING_IF_HSYNC_RST) != poison_if,
        timeout=20.0), (
        "IF_HSYNC_RST still holds the poison 20 s after a preset load, so "
        "nothing wrote the sampling divider at all")
    restore_the_preset_the_suite_expects(host)

    divider, if_line, retime = _settled_sampling(host)
    assert None not in (divider, if_line, retime), (
        "could not read the sampling registers back after the preset load")
    assert divider, "PLLAD_MD came back 0 after a preset load"

    # The value is deliberately NOT pinned. Which divider the unit lands on is
    # the table's business and it moved 2553 -> 2558 across this very run; what
    # must hold is that the three registers describe the SAME line.

    # Tv5725::SourceMeasurement::ifLineFor and ::retimeStopFor. The IF counts the ADC line
    # after decimation by two; the retime stop sits 93% of the way along it.
    assert if_line == divider // 2, (
        f"IF_HSYNC_RST is {if_line}, want {divider // 2} for PLLAD_MD {divider}. "
        f"The input formatter is counting to the end of a line the ADC is not "
        f"delivering -- 2026-08-09, solid green screen.")
    assert retime == divider * 93 // 100, (
        f"SP_RT_HS_SP is {retime}, want {divider * 93 // 100} for PLLAD_MD "
        f"{divider}. The sync processor's retime window is sized for a divider "
        f"that is no longer in force.")

    # The latch, and ONLY once the source is counting. Both readings come from
    # one /getregs burst: segment 0 holds live measurements, so fetching them
    # separately samples them many video lines apart and a disagreement between
    # them would prove nothing.
    locked = wait_for(
        lambda: (read_field(host, *SAMPLING_SYNC_VTOTAL) or 0) >= SAMPLING_LOCKED_VTOTAL_MIN,
        timeout=25.0)
    if not locked:
        pytest.skip("the source is not locked after the preset load, and "
                    "STATUS_SYNC_PROC_HTOTAL is noise without it")

    segment0 = read_segment(host, 0)
    assert segment0 is not None, "could not read segment 0 in one burst"
    htotal = field_from(segment0, 0x17, 0, 12)
    source_vtotal = field_from(segment0, 0x1B, 0, 11)
    assert source_vtotal >= SAMPLING_LOCKED_VTOTAL_MIN, (
        f"the source stopped counting between the lock check and the read "
        f"(SP_VTOTAL {source_vtotal}), so HTOTAL is not a measurement")

    assert abs(htotal - divider) <= SAMPLING_LATCH_TOLERANCE, (
        f"STATUS_SYNC_PROC_HTOTAL counts {htotal} ADC clocks per line but "
        f"PLLAD_MD holds {divider}, with the source locked at {source_vtotal} "
        f"lines. The register was written but never LATCHED, so the ADC PLL is "
        f"still running the previous divider while every register reads back "
        f"correct. PLLAD_LAT loads MD, ND, KS, CKOS and ICP together -- whatever "
        f"writes PLLAD_MD must run BEFORE latchPLLAD(), not after it.")


# updateSpDynamic()'s two branches for videoStandardInput >= 13, which is the
# whole of what the sync-type choice does to the sync processor.
SP_PRE_COAST = (5, 0x38, 0, 8)
SP_POST_COAST = (5, 0x39, 0, 8)
SP_DLT_REG = (5, 0x35, 0, 12)
SP_H_PULSE_IGNOR = (5, 0x37, 0, 8)

SYNC_CSYNC = {SP_PRE_COAST: 0x04, SP_POST_COAST: 0x07,
              SP_DLT_REG: 0x70, SP_H_PULSE_IGNOR: 0x02}
SYNC_SEPARATE = {SP_PRE_COAST: 0x00, SP_POST_COAST: 0x00,
                 SP_DLT_REG: 0x00, SP_H_PULSE_IGNOR: 0xFF}


@pytest.mark.xfail(strict=True, reason=(
    "sourceHasOwnVsync() answers yes on a source that has none, so the probe "
    "picks separate H/V. Reproduces every boot; docs/sync-type-selection.md. "
    "Strict so fixing the probe turns this red and the marker comes out."))
def test_a_preset_load_does_not_redecide_the_sync_type(host, source):
    """The bench source has no separate V sync, and a preset load must not say it has.

    **THE CHOICE MUST NOT BE MADE FROM A BIT THAT ONLY ANSWERS ONCE THE CHOICE IS
    RIGHT.** `syncTypeCsync = (STATUS_SYNC_PROC_VSACT == 0)` on every mode change
    is circular: VSACT reports the sync path you are already on, so the read
    agrees with whatever the chip is configured for and latches it. A preset table
    loading first hides that -- the table establishes a csync-ish sync processor,
    VSACT reads 0, and the answer comes out right for the wrong reason.
    See CLAUDE.md and docs/sync-type-selection.md.

    The consequence is entirely visible in the sync processor, because
    updateSpDynamic() writes one of exactly two quadruples for
    videoStandardInput >= 13, and they share no value:

        csync     SP_PRE_COAST 0x04  SP_POST_COAST 0x07  DLT 0x70  IGNOR 0x02
        separate  SP_PRE_COAST 0x00  SP_POST_COAST 0x00  DLT 0x00  IGNOR 0xFF

    So this reads the quadruple and asks which decision was taken, rather than
    trying to observe a bool in the firmware.

    **WHY CSYNC IS THE RIGHT ANSWER HERE AND NOT JUST THE CURRENT ONE.** The
    bench RiscPC feeds RGBS with sync on green, and the schematic's routing for
    that input is HS_IN = SOGIN -- there is no separate HSync on the pin and no
    separate VSync line at all. A source with its own V sync is by definition not
    composite-sync; this one has none, so "separate H/V" is not a tuning
    preference here, it is wrong. inputAndSyncDetect() establishes that once with
    sourceHasOwnVsync(), which switches SP_EXT_SYNC_SEL off and asks whether a V
    sync actually arrives; a preset load has no business overruling it.

    Not a value pin: nothing here asserts the divider, the raster or any framing.
    """
    before = {spec: read_field(host, *spec) for spec in SYNC_CSYNC}
    assert None not in before.values(), "could not read the sync processor"

    get(host, "/sc?%29")  # a real preset load: table, sketch, bring-up, engine
    restore_the_preset_the_suite_expects(host)

    after = {spec: read_field(host, *spec) for spec in SYNC_CSYNC}
    assert None not in after.values(), (
        "could not read the sync processor back after the preset load")

    def name(spec):
        return {SP_PRE_COAST: "SP_PRE_COAST", SP_POST_COAST: "SP_POST_COAST",
                SP_DLT_REG: "SP_DLT_REG",
                SP_H_PULSE_IGNOR: "SP_H_PULSE_IGNOR"}[spec]

    if after == SYNC_SEPARATE:
        pytest.fail(
            "a preset load configured the sync processor for SEPARATE H/V on a "
            "source that has none -- the sync type was re-decided from "
            "STATUS_SYNC_PROC_VSACT, which reports the path already in force and "
            "so latches. It belongs to inputAndSyncDetect(), once per source. "
            f"before: { {name(k): hex(v) for k, v in before.items()} }")

    wrong = {name(k): (hex(after[k]), hex(v)) for k, v in SYNC_CSYNC.items()
             if after[k] != v}
    assert not wrong, (
        "the sync processor holds neither of updateSpDynamic()'s two quadruples "
        "after a preset load, so something else has written it and this test no "
        "longer measures what it claims -- check updateSpDynamic() before "
        f"trusting the verdict: {wrong} (got, want-csync)")


# Tv5725::SourceMeasurement's own constants, so the expected divider is computed the way
# the firmware computes it rather than pinned to this bench's source.
SAMPLING_MAX_RATE_HZ = 162000000
SAMPLING_RECOMMENDED_PERCENT = 98
SAMPLING_DIVIDER_MAX = 4095
SAMPLING_PAL_VTOTAL_MIN = 290  # Capture::PalVtotalMin
SAMPLING_DEC1_BYPS = (5, 0x1F, 0, 1)
SAMPLING_DEC2_BYPS = (5, 0x1F, 1, 1)


def _expected_divider(lines, oversample, field_rate):
    """SourceMeasurement::recommendedDivider(), in Python."""
    line_rate = int(field_rate * lines)
    largest = min(SAMPLING_MAX_RATE_HZ // (line_rate * oversample),
                  SAMPLING_DIVIDER_MAX)
    backed = (largest * SAMPLING_RECOMMENDED_PERCENT) // 100
    # The second ceiling: the IF halves the divider, so twice the write limit is
    # the longest line the capture path writes to the end of.
    return min(backed, WRITE_LIMIT_UNITS * 2) & ~1


def test_the_sampling_divider_is_solved_from_the_source_not_inherited(host, source):
    """On a settled source the divider is the one the line rate implies.

    **THE COMPANION TEST DELIBERATELY DOES NOT PIN THE VALUE, AND THAT LEFT A
    HOLE THE SIZE OF A WHOLE BOOT.** It checks that PLLAD_MD, IF_HSYNC_RST and
    SP_RT_HS_SP describe the same line and that the divider was latched -- all
    of which a WRONG divider satisfies perfectly, because Tv5725::SourceMeasurement
    writes all three off whatever value it holds. "Which divider the unit lands
    on is the table's business" was true while a table chose it. It is not any
    more: the engine computes it, so it can be checked.

    Measured on a cold boot 2026-08-15 with the tables gone, deterministically
    and on every boot:

        sampling: 271 lines x 49.22 Hz -> line rate 0
        PLLAD_MD 1856, where 2548 was due

    SourceMeasurement::lineRateFrom() correctly refused an unsettled 271 lines at
    49.22 Hz, solveSampling() fell back to adopting whatever was on the chip,
    and what was on the chip was the 1856 that bypassModeSwitch_RGBHV() writes
    as a literal. Nothing retried. The ADC under-sampled every line by 27% for
    the whole session, all three registers agreed with each other about it, the
    latch check passed, and `/sc?~` was the only cure.

    So this asks the one question those cannot: is the divider a MEASUREMENT of
    this source, or a number that was already there?

    The expected value is computed from the source's own line count rather than
    pinned, so the test travels to another source. The field rate is not
    readable over HTTP, so the nominal implied by the line count is used -- the
    same 290-line split Capture::PalVtotalMin makes -- and the tolerance covers
    a real rate a couple of percent off nominal. A divider inherited from
    somewhere else is out by far more than that: 1856 against 2548 is 27%.
    """
    locked = wait_for(
        lambda: (read_field(host, *SAMPLING_SYNC_VTOTAL) or 0) >= SAMPLING_LOCKED_VTOTAL_MIN,
        timeout=25.0)
    if not locked:
        pytest.skip("the source is not locked, so there is no line rate to "
                    "have solved the divider from")

    lines = read_field(host, *SAMPLING_SYNC_VTOTAL)
    divider = read_field(host, *SAMPLING_PLLAD_MD)
    dec1 = read_field(host, *SAMPLING_DEC1_BYPS)
    dec2 = read_field(host, *SAMPLING_DEC2_BYPS)
    assert None not in (lines, divider, dec1, dec2), (
        "could not read the source line count, the divider and the decimators")

    # rto->osr, as doPostPresetLoadSteps() derives it for a custom preset.
    oversample = 1 if (dec1 and dec2) else (2 if dec1 else 4)

    nominal = 50.0 if lines > SAMPLING_PAL_VTOTAL_MIN else 60.0
    expected = _expected_divider(lines, oversample, nominal)

    # 4%: the divider moves inversely with the field rate, so a source running
    # 50.08 against a nominal 50 shifts it 0.16%, and a mode a couple of percent
    # off nominal shifts it that much. An INHERITED divider is not close.
    assert abs(divider - expected) <= expected * 4 // 100, (
        f"PLLAD_MD is {divider}, but {lines} lines at {nominal} Hz with "
        f"{oversample}x oversampling wants about {expected}. The divider is not "
        f"a measurement of this source -- solveSampling() adopted whatever was "
        f"on the chip and nothing retried. 1856 is the literal "
        f"bypassModeSwitch_RGBHV() writes.")


# HSOUT/VSOUT to the encoder. Active low: 0 drives the pins, 1 takes sync away.
PAD_SYNC_OUT_ENZ = (0, 0x49, 2, 1)


def test_a_mode_change_leaves_the_sync_output_driven(host, source, preset_load):
    """After a mode change lands, HSOUT/VSOUT are being driven.

    loop() takes sync away for ENCODER_RELOCK_MS when the engine reports a new
    raster, because the encoder samples the analog output and does not always
    notice the timing under it moved -- it carries on transmitting the mode it
    locked to before, and the display reports that older rate and shows nothing.
    docs/investigations/encoder-stale-timing.md

    **What this guards is the restore, because that is the dangerous half.** A
    drop whose restore depends on some later state leaves a unit with every
    register correct and no HSOUT/VSOUT at all -- no picture ever, rather than a
    stale one, and nothing in a register dump says so. That failure has been
    produced on this bench by pairing the drop with the mode-change pending flag,
    which never clears when the source is not detected.

    The drop itself is not asserted here: it lasts a few hundred milliseconds and
    catching it over HTTP would be a race. It is verified on the bench, by the
    display re-locking without anyone toggling the register by hand.
    """
    try:
        get(host, "/sc?%29")  # a real preset load: table, sketch, bring-up, engine
        assert wait_for(lambda: (read_field(host, 3, 0x01, 0, 12) or 0) > 1000,
                        timeout=20.0), (
            "no raster after the preset load, so the mode change never landed "
            "and this says nothing about what happens when it does")
        time.sleep(8)  # detection settles; CLAUDE.md says discard ~6 s

        enz = read_field(host, *PAD_SYNC_OUT_ENZ)
        assert enz == 0, (
            f"PAD_SYNC_OUT_ENZ reads {enz} after the geometry landed, so the "
            "sync output was left disabled. The unit is driving no HSOUT/VSOUT: "
            "every register reads correct and the display shows nothing. Clear "
            "bit 2 of s0_49 to get the picture back.")
    finally:
        recover_lock(host)


# The four registers InputFormatter::applyScanMode() puts into one of two
# states. Line-doubled is 1/0/0/0, progressive is 0/1/1/1.
SCAN_HS_DEC_FACTOR = (1, 0x0B, 4, 2)
SCAN_LD_SEL_PROV = (1, 0x0B, 7, 1)
SCAN_LD_RAM_BYPS = (1, 0x0C, 0, 1)
SCAN_PRGRSV_CNTRL = (1, 0x00, 6, 1)

# Below this many source lines the capture is line-doubled. Mirrors
# SourceMeasurement::LineDoubleBelowLines.
SCAN_LINE_DOUBLE_BELOW = 400


def _scan_mode(host):
    """('doubled'|'progressive'|'mixed', the four raw values)."""
    values = tuple(read_field(host, *spec) for spec in
                   (SCAN_HS_DEC_FACTOR, SCAN_LD_SEL_PROV,
                    SCAN_LD_RAM_BYPS, SCAN_PRGRSV_CNTRL))
    if values == (1, 0, 0, 0):
        return "doubled", values
    if values == (0, 1, 1, 1):
        return "progressive", values
    return "mixed", values


def test_a_preset_load_leaves_the_scan_mode_the_source_calls_for(host, source):
    """A preset load must not change how many lines the capture takes in.

    A source that qualifies for scaling RGBHV is filed under
    PresetLoad::ScalingRgbhvStandard, which is 3, and doPostPresetLoadSteps()
    used to branch `3 || 4 || 8 || 9` straight into
    applyScanMode(Progressive). So a 15 kHz RGBHV source of 311 lines -- which
    both qualifies for scaling RGBHV and needs the line doubler -- came out
    progressive, and every register downstream was then correct for a premise
    the source contradicts:

        PLLAD_MD 1124 where 2250 was due, IF_HSYNC_RST following it to 1124,
        SP_VTOTAL collapsing from 311 to noise, HPERIOD_IF railing.

    Nothing in a register dump looks wrong, because the divider IS right for
    progressive. One number was carrying both the input format and the scan
    mode.

    The scan mode is the engine's now, derived from the line count it measures,
    so a preset load cannot move it.
    """
    lines = read_field(host, *SAMPLING_SYNC_VTOTAL)
    assert lines and lines >= SAMPLING_LOCKED_VTOTAL_MIN, (
        f"source not locked ({lines} lines), so there is nothing to preserve")
    wanted = "doubled" if lines < SCAN_LINE_DOUBLE_BELOW else "progressive"

    before, raw_before = _scan_mode(host)
    assert before == wanted, (
        f"before the preset load a {lines}-line source is already {before} "
        f"{raw_before}, wanting {wanted}")

    try:
        get(host, "/sc?%29")  # loadComputedPreset(Mode1080p, 0x15)
        assert wait_for(lambda: (read_field(host, 3, 0x01, 0, 12) or 0) > 1000,
                        timeout=20.0), "no raster after the preset load"
        time.sleep(8)  # detection settles; CLAUDE.md says discard ~6 s

        after, raw_after = _scan_mode(host)
        settled = _settled_sampling(host)
        assert after == wanted, (
            f"the preset load moved a {lines}-line source from {before} to "
            f"{after} {raw_after}. The scan mode followed the enumerated "
            f"standard rather than the source, and the divider went with it: "
            f"PLLAD_MD is now {settled[0] if settled else 'unreadable'}.")
    finally:
        recover_lock(host)
