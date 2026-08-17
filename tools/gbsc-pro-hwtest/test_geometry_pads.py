"""The pads, against a live unit.

The firmware is the implementation. These tests assert the properties its
geometry has to hold -- the windows hug the picture, the picture fills the
raster, nothing is inherited -- by recomputing them from the capture and the
raster the unit actually holds after a press. geometry_math is used only as a
convenient calculator for that arithmetic; it is scratch tooling, not a
specification, and a disagreement between it and the firmware is not by itself
a firmware fault.

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

import math
import os
import re
import subprocess
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe
import geometry_math as gm
from gbs_unit import (field_from, get, get_json, read_field, read_reg, read_segment,
                      wait_for, write_reg)

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


def units_for(pixels, scale_reg):
    """Output pixels -> input units: what Geometry::Controls::unitsFor computes.

    magnification = 1024 / VDS_?SCALE, and ONE INPUT UNIT IS THE FLOOR -- at
    x1.315 a single unit is already 1.315 output pixels, and no rounding gets
    under it. Reading the scale off the chip rather than assuming one keeps this
    honest at any magnification.
    """
    if scale_reg == 0:              # a dropped read, not a setting
        return pixels
    units = max(1, round(abs(pixels) * scale_reg / 1024))
    return -units if pixels < 0 else units

SOLVED_REGISTERS = ("VDS_HB_SP", "VDS_HB_ST", "VDS_DIS_HB_SP", "VDS_DIS_HB_ST",
                    "VDS_VB_SP", "VDS_VB_ST", "VDS_DIS_VB_SP", "VDS_DIS_VB_ST",
                    "VDS_HSCALE", "VDS_VSCALE")


def framing(host):
    """The engine's framing: zoom steps and pan offsets, per axis."""
    return get_json(host, "/geometry")


def set_framing(host, zh, zv, ph, pv):
    return get_json(host, f"/geometry?zh={zh}&zv={zv}&ph={ph}&pv={pv}")


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


def predict(state):
    """The ten output registers implied by the capture the unit actually holds.

    Re-derived from the capture read back AFTER the press, not from what we
    asked for: the property under test is that the windows follow the capture
    and the raster, and reading it back stops a firmware that ignored the press
    from passing by accident. That a press moves the capture at all is asserted
    separately.
    """
    line_px = state["VDS_HSYNC_RST"] + 1
    frame_lines = state["VDS_VSYNC_RST"] + 1
    capture_h = state["IF_HB_ST2"] - state["IF_HB_SP2"]
    capture_v = state["IF_VB_ST"] - state["IF_VB_SP"]
    hscale, _ = gm.fit_to_raster(capture_h, line_px, gm.AXIS_H)
    vscale, _ = gm.fit_to_raster(capture_v, frame_lines, gm.AXIS_V)
    solved = gm.solve_geometry(
        capture_h=capture_h, capture_v=capture_v,
        hscale=hscale, vscale=vscale,
        line_px=line_px, frame_lines=frame_lines)
    registers = dict(solved["registers"])
    registers["VDS_HSCALE"] = hscale
    registers["VDS_VSCALE"] = vscale
    return registers


@pytest.fixture
def probe(host):
    return probe_for(host)


@pytest.fixture
def scaling(host, source):
    """The geometry under test, or a skip saying why there is none.

    Bypass is the expected reason: an RGBHV source over 535 lines is trapped
    there, VDS_?SYNC_RST reads 0, and the VDS is out of the video path
    entirely. That is not a failure of the pads -- there is nothing for them to
    do. The bench 800x600 (VTOTAL 627) hits it every boot.
    """
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
    """The geometry at a KNOWN framing, supplied rather than inherited.

    Each test states the state it needs instead of saving and restoring
    whatever it found, so nothing depends on what ran before it. The engine's
    state is the framing; the registers are an output of it, which is why
    writing registers back restores nothing -- the next press recomputes from a
    framing nobody reset. Getting that wrong let eleven zoom steps accumulate
    across a suite while every test believed it had made one.
    """
    set_framing(host, 0, 0, 0, 0)
    return settled_geometry(host)


# What a press moves first, so there is something to wait on.
WATCH = {"z": ("IF_HB_ST2", 1, 0x18, 0, 11),
         "h": ("IF_HB_ST2", 1, 0x18, 0, 11),
         "+": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         "-": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         # The OSD and IR 'move' keys, which reach the same handler.
         "6": ("IF_HB_SP2", 1, 0x1A, 0, 11),
         "7": ("IF_HB_SP2", 1, 0x1A, 0, 11)}


def press(host, probe, command, timeout=6.0):
    """Issue a pad press and wait for it to land.

    /sc only sets `serialCommand`; web_service() consumes it from loop() on its
    next tick, so a 200 means queued, not done. And only ONE command fits in
    that global -- a second press before the first is consumed is a press thrown
    away, which would make a repeated-press test quietly measure fewer presses
    than it made.
    """
    spec = WATCH[command]
    before = probe.read_field(spec)
    status, _ = get(host, f"/sc?{command}")
    assert status == 200, f"/sc?{command} returned {status}"
    landed = wait_for(lambda: probe.read_field(spec) != before, timeout=timeout)
    assert landed, (f"/sc?{command} did not move {spec[0]} from {before} "
                    f"within {timeout}s -- was the press absorbed?")


def press_until_saturated(host, probe, command, limit=24):
    """Press until the control stops moving, and report how many landed.

    press() asserts every press moves something, which is right for a test about
    ONE press and wrong for a test that walks a control to its end -- a zoom has
    a finite range and reaching it is correct behaviour, not a fault.

    That range is not a constant, which is why this counts rather than assuming.
    AxisH magnifies at most geometry_math.MAX_MAGNIFICATION, so the smallest
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


def differences(want, got):
    return {name: (want[name], got[name])
            for name in SOLVED_REGISTERS if want[name] != got[name]}


@pytest.mark.zoom
def test_a_zoom_press_leaves_the_windows_following_the_capture(host, probe, framed):
    """End to end: press the pad, read the registers back, and check every
    output window is the one the capture and the raster imply."""
    press(host, probe, "z")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    assert not differences(predict(after), after)


@pytest.mark.pan
def test_a_pan_press_leaves_the_windows_following_the_capture(host, probe, framed):
    press(host, probe, "+")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    assert not differences(predict(after), after)


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


# The playback burst, which is not geometry and is owned by the engine anyway.
#
# Segment 4 is not in GEOMETRY_FIELDS and deliberately stays out of it: these
# are read a field at a time so a failure names the register it is about.
PB_FETCH_NUM = ("PB_FETCH_NUM", 4, 0x39, 0, 10)
PB_CAP_OFFSET = ("PB_CAP_OFFSET", 4, 0x37, 0, 10)
VDS_HSYNC_RST = ("VDS_HSYNC_RST", 3, 0x01, 0, 12)

# What Tv5725::Memory holds. Mirrors src/tv5725/Memory.h, the same way
# ZOOM_STEP_PX mirrors ControlSteps.
LINE_1080P = 1445
OFFSET_1080P = 250
UPSTREAM_FETCH = 256
UPSTREAM_OFFSET = UPSTREAM_FETCH + 4


# Mirrors Tv5725::Memory. The fetch has to cover the source pixels each output
# line needs, over the playback request budget; the offset deliberately does not
# follow it, being clean anywhere across 190..256 when it was measured.
REQUESTS_PER_LINE = 4
FETCH_FLOOR = 150
FETCH_MAX = 512


def expected_burst(line_px, capture):
    """The pair Tv5725::Memory computes for this raster and capture.

    **THE FETCH HAS NO RASTER GATE, AND THIS MIRRORED ONE FOR TOO LONG.**
    Memory::fetchFor dropped its gate deliberately and records why: a preset
    load moved the unit to a 1435 px raster, the rule switched itself off, and
    PB_FETCH_NUM sat at upstream's 256 against a capture of 1185 that needed
    297. 256 is not a neutral default; it is a value tuned for a raster it is
    not, and it can sit below the floor.

    Keeping the gate here meant this file asserted the fault. On the bench's
    1436 px raster it expected 256 while the firmware correctly computed 251,
    which failed the test above and silently SKIPPED the stronger one below --
    so the register that tore 80 of 493 framings has had no live cover at all.

    The offset keeps its gate, because Memory::offsetFor still has one.
    """
    fetch = min(FETCH_MAX, max(FETCH_FLOOR, -(-capture // REQUESTS_PER_LINE)))
    offset = OFFSET_1080P if line_px == LINE_1080P else UPSTREAM_OFFSET
    return fetch, offset


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

    line_px = probe.read_field(VDS_HSYNC_RST) + 1
    capture = after["IF_HB_ST2"] - after["IF_HB_SP2"]
    fetch, offset = expected_burst(line_px, capture)
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

    line_px = probe.read_field(VDS_HSYNC_RST) + 1

    # The press MOVES the capture, so what to expect can only be computed from
    # the framing the press lands on -- not from the one it started in.
    press(host, probe, "z")
    after = settled_geometry(host)
    capture = after["IF_HB_ST2"] - after["IF_HB_SP2"]
    fetch, offset = expected_burst(line_px, capture)
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

    assert after["VDS_HB_SP"] >= gm.AXIS_H.window_sp_min


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

    set_framing(host, -5000, 0, 0, 0)
    after = settled_geometry(host)

    assert after["IF_HB_SP2"] >= guard, (
        f"the capture starts at {after['IF_HB_SP2']}, inside the {guard} unit "
        f"hsync pulse at the head of a {units} unit line")


@pytest.mark.zoom
def test_a_zoomed_out_capture_still_takes_the_whole_tail(host, probe, framed):
    """The other half of the same decision, and the one a future guard would
    quietly break. Nothing derivable bounds the tail, so zoom-out must still
    reach the last unit before the wrap -- that is the reach which recovers
    active video the 0.76 default active fraction crops."""
    units = framed["IF_HSYNC_RST"] + 1

    set_framing(host, -5000, 0, 0, 0)
    after = settled_geometry(host)

    assert after["IF_HB_ST2"] == units - 1, (
        f"the capture stops at {after['IF_HB_ST2']} of a {units} unit line, "
        f"short of the {units - 1} the wrap allows: something is guarding the "
        "tail, and no measurement supports a bound there")


@pytest.mark.pan
def test_panning_to_the_left_stop_never_takes_the_hsync_pulse(host, probe, framed):
    """The same bound, reached the other way. capture() clamps the window and
    clampToLine() clamps the framing, and they are separate arithmetic -- a pan
    that stops one unit short of where a zoom stops is the dead zone of
    2026-08-09 wearing a different hat."""
    units = framed["IF_HSYNC_RST"] + 1
    guard = sync_units(probe, units)

    set_framing(host, 0, 0, -5000, 0)
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
        needed = max(FETCH_FLOOR, -(-capture // REQUESTS_PER_LINE))
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


def test_the_frame_buffer_subsystem_owns_the_memory_map(host, source):
    """A preset load leaves the map Tv5725::FrameBuffer writes, not a table's.

    POISONED FIRST, AND THAT IS THE WHOLE POINT. The TV5725 keeps its registers
    when the ESP reboots, so reading the right value proves nothing on its own
    -- it may simply be what was already there. Two conclusions were drawn from
    exactly that mistake on 2026-08-13, in both directions: leftover hand-writes
    read as "the code ran", and a boot that never loaded a preset read as "the
    code is broken". Writing a value that is neither the table's nor the
    firmware's, then forcing a load, is what distinguishes them.

    This is also a two-owner guard. FrameBuffer::apply() wrote nine registers
    and eight landed, because doPostPresetLoadSteps() carried its own
    uncommented CAP_SAFE_GUARD_EN::write(0) further down the same function and
    won by running later. Every check that compared the map against the tables
    passed throughout. Only reading the chip after a real load catches it.

    /sc?) is the trigger because it is a REAL preset load. /sc?# is not:
    applyPresets(13) returns early -- `result == 5 || 6 || 7 || 13` -- before
    doPostPresetLoadSteps(), so the bring-up block and this map never run.
    """
    poison = {0x21: 0x43, 0x33: 0x0A, 0x26: 0x15}
    for register, value in poison.items():
        write_reg(host, 4, register, value)
    got = {r: read_reg(host, 4, r) for r in poison}
    assert got == poison, (
        f"could not poison the map registers, got {got} -- without this the "
        "test cannot tell a fresh write from a leftover")

    get(host, "/sc?%29")  # writeProgramArrayNew(pal_1920x1080) + post steps

    assert wait_for(lambda: read_reg(host, 4, 0x33) == 0x06, timeout=15.0), (
        "PB_CAP_BUF_STA_ADDR_A high byte never reached 0x06 after a preset "
        "load: FrameBuffer::apply() did not run, or something overwrote it")

    wrong = []
    for name, seg, reg, lo, width, want in MEMORY_MAP:
        got = read_field(host, seg, reg, lo, width)
        if got != want:
            wrong.append(f"{name} (s{seg}_{reg:02x}) = 0x{got:X}, want 0x{want:X}")
    assert not wrong, (
        "the subsystem does not own its registers after a preset load:\n  "
        + "\n  ".join(wrong))


# The SDRAM bus, owned by Tv5725::MemoryBus. The clock the bus runs at and the
# nanosecond trims that compensate this board's traces -- all four measured on
# the bench 2026-08-13, none of them a property of the output mode.
MEMORY_BUS = [
    ("PLL_MS", 0, 0x40, 4, 3, 3),            # 162MHz, fastest in spec for the part
    ("MEM_FK_RD_DLY", 4, 0x04, 0, 3, 2),
    ("MEM_DATA_DLY_REG", 4, 0x18, 0, 3, 0),
    ("MEM_CLK_DLY_REG", 4, 0x1B, 4, 3, 4),
    # Three clocks each, which is what 162MHz needs to cover tRCD and tRP.
    # **Neither of these can discriminate** -- all twelve tables carry 1 as
    # well, so they read correct whoever wrote them. They are here because the
    # subsystem owns them, not because they prove it did; the four above are
    # what makes the test mean anything.
    ("MEM_ACT_CYCLE", 4, 0x05, 0, 2, 1),
    ("MEM_PCHG_CYCLE", 4, 0x05, 4, 2, 1),
]


def test_the_memory_bus_subsystem_owns_its_timing(host, source):
    """A preset load leaves the bus timing Tv5725::MemoryBus writes, not a table's.

    THE TRIGGER IS pal_1280x720 AND THAT IS THE WHOLE POINT. Its table carries
    PLL_MS 7, FK_RD_DLY 0, DATA_DLY 6, CLK_DLY 5 -- a different value from the
    owned one in every one of the four fields. Triggering with /sc?) instead
    would load pal_1920x1080, whose table already carries three of the four
    owned values, and the test would pass whether or not MemoryBus ran at all.

    Poisoned first for the reason the frame buffer test is: the TV5725 keeps its
    registers when the ESP reboots, so reading the right value proves nothing on
    its own. The poison is a third set, neither the table's nor the firmware's.
    PLL_MS is poisoned to 4 (144MHz) rather than 5 or 6, which would run the
    EM638325TS-6 above its 166MHz rating while the test is mid-flight.

    Why these values are not a table's to choose, measured on the bench:
      - All three delays were swept to both ends of their range at 129.6MHz with
        the picture unchanged at every step, under the heaviest playback load
        the unit has (1920x1080). A positive control -- MEM_RD_LAT_PIP forced to
        0 -- visibly corrupted the picture, so the path was live and sensitive
        throughout and the clean results are evidence rather than an artefact.
      - PLL_MS was measured clean at 129.6MHz, at FBCLK, and at 162MHz. The
        twelve tables split it six/six with no correlation to output size, so it
        was never a mode-dependent quantity.
      - 162MHz is the fastest of the eight PLL_MS codes that stays in spec for
        the EM638325TS-6: tCK3 6ns (6.17 actual), and tRCD/tRP 18ns against the
        18.5ns that MEM_ACT_CYCLE/MEM_PCHG_CYCLE give at 3 clocks. 185MHz and
        216MHz break all three. docs/EM638325-Industrial_Rev-3.2.pdf.
    """
    poison = {(0, 0x40, 4, 3): 4, (4, 0x04, 0, 3): 5,
              (4, 0x18, 0, 3): 3, (4, 0x1B, 4, 3): 6}
    for (seg, reg, lo, width), value in poison.items():
        raw = read_reg(host, seg, reg)
        mask = ((1 << width) - 1) << lo
        write_reg(host, seg, reg, (raw & ~mask) | (value << lo))
    got = {k: read_field(host, k[0], k[1], k[2], k[3]) for k in poison}
    assert got == poison, (
        f"could not poison the bus registers, got {got} -- without this the "
        "test cannot tell a fresh write from a leftover")

    get(host, "/sc?y")  # writeProgramArrayNew(pal_1280x720) + post steps

    assert wait_for(lambda: read_field(host, 0, 0x40, 4, 3) != 4, timeout=15.0), (
        "PLL_MS never moved off the poison after a preset load: "
        "MemoryBus::apply() did not run")

    try:
        wrong = []
        for name, seg, reg, lo, width, want in MEMORY_BUS:
            got = read_field(host, seg, reg, lo, width)
            if got != want:
                wrong.append(f"{name} (s{seg}_{reg:02x}) = {got}, want {want}")
        assert not wrong, (
            "the subsystem does not own the bus after a preset load:\n  "
            + "\n  ".join(wrong))
    finally:
        get(host, "/sc?%29")  # back to pal_1920x1080, where the suite expects it
        restore_the_preset_the_suite_expects(host)


# The FIFO request watermarks and the line-double reset position, owned by
# Tv5725::FrameBuffer and Tv5725::InputFormatter.
#
# The last column is what pal_768x576 carries, and it is the point: six of the
# seven differ from the owned value, so the trigger below can actually fail.
WATERMARKS = [
    ("PB_MAST_FLAG_REG", 4, 0x2C, 0, 6, 24),      # table: 43
    ("PB_GENERAL_FLAG_REG", 4, 0x2D, 0, 6, 61),   # table: 0
    ("RFF_MASTER_FLAG", 4, 0x4E, 0, 6, 36),       # table: 14
    ("RFF_GENERAL_FLAG", 4, 0x4F, 0, 6, 60),      # table: 18
    ("WFF_LINE_FLIP", 4, 0x4A, 4, 1, 1),          # table: 0
    ("IF_LD_ST", 1, 0x0C, 1, 4, 5),               # table: 3
    # **This one cannot discriminate.** pal_768x576 carries 0 too, so it reads
    # correct whoever wrote it. Asserted because the subsystem owns it, not
    # because it is evidence -- six fields above are the evidence, not seven.
    ("WFF_FF_HALF_REQ", 4, 0x42, 1, 1, 0),
]


def test_the_subsystems_own_the_fifo_watermarks(host, source):
    """A preset load leaves the watermarks the subsystems write, not a table's.

    **THE TRIGGER IS pal_768x576 AND THAT IS THE WHOLE POINT.** Its table is the
    most hostile of the twelve to these fields: PB_MAST_FLAG_REG 43,
    PB_GENERAL_FLAG_REG 0, RFF_MASTER_FLAG 14, RFF_GENERAL_FLAG 18,
    WFF_LINE_FLIP 0 and IF_LD_ST 3 -- every one different from the owned value.
    Triggering with /sc?) loads pal_1920x1080, whose table already carries all
    seven owned values, and the test then passes whether or not
    FrameBuffer::apply() and InputFormatter::apply() ran at all.

    It is also the load that installs PB_GENERAL_FLAG_REG = 0, a low-request
    watermark that can never fire; two of the twelve tables ship that.

    Poisoned first, for the reason the two tests above are: the TV5725 keeps its
    registers when the ESP reboots, so reading the right value proves nothing on
    its own. The poison is a third set, neither the table's nor the firmware's.

    /sc?2 is a real preset load -- writeProgramArrayNew(pal_768x576) followed by
    doPostPresetLoadSteps(), where the bring-up block and both subsystems run.
    Unlike /sc?h it does not touch presetPreference, so it leaves no persistent
    user preference behind.

    **THE TEARDOWN MAY NOT BE ENOUGH**, and that is a property of the unit rather
    than of this test. Preset churn can leave a separate-sync source on the csync
    path with no lock -- SP_VTOTAL steady at a non-mode value -- and reloading
    pal_1920x1080 does not undo it, because the sync type is decided by a VSACT
    read that the csync path itself makes come out wrong. Recover with
    `curl 'http://<ip>/sc?~'`, which forces a fresh detection pass.
    docs/sync-type-selection.md
    """
    poison = {(4, 0x2C, 0, 6): 7, (4, 0x2D, 0, 6): 9,
              (4, 0x4E, 0, 6): 11, (4, 0x4F, 0, 6): 13,
              (4, 0x4A, 4, 1): 0, (4, 0x42, 1, 1): 1,
              (1, 0x0C, 1, 4): 9}
    for (seg, reg, lo, width), value in poison.items():
        raw = read_reg(host, seg, reg)
        mask = ((1 << width) - 1) << lo
        write_reg(host, seg, reg, (raw & ~mask) | (value << lo))
    got = {k: read_field(host, k[0], k[1], k[2], k[3]) for k in poison}
    assert got == poison, (
        f"could not poison the watermark registers, got {got} -- without this "
        "the test cannot tell a fresh write from a leftover")

    get(host, "/sc?2")  # writeProgramArrayNew(pal_768x576) + post steps

    assert wait_for(lambda: read_field(host, 4, 0x2C, 0, 6) != 7, timeout=15.0), (
        "PB_MAST_FLAG_REG never moved off the poison after a preset load: "
        "FrameBuffer::apply() did not run")

    try:
        wrong = []
        for name, seg, reg, lo, width, want in WATERMARKS:
            got = read_field(host, seg, reg, lo, width)
            if got != want:
                wrong.append(f"{name} (s{seg}_{reg:02x}) = {got}, want {want}")
        assert not wrong, (
            "the subsystems do not own the watermarks after a preset load:\n  "
            + "\n  ".join(wrong))
    finally:
        get(host, "/sc?%29")  # back to pal_1920x1080, where the suite expects it
        restore_the_preset_the_suite_expects(host)


# Every register Engine::write() and Engine::solveRaster() produce, addresses
# taken from Tv5725.h rather than recalled -- five were fumbled from memory in
# one session and each produced a confident wrong reading.
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


def _engine_outputs(host):
    return {name: read_field(host, seg, reg, lo, width)
            for name, seg, reg, lo, width in ENGINE_OUTPUTS}


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
        get(host, "/sc?~")
        wait_for(lambda: read_field(host, 0, 0x1B, 0, 11) > 200, timeout=30.0)


def _compare_engine_outputs_across_a_preset_load(host):
    get(host, "/sc?%29")  # a real preset load: table, sketch, bring-up, engine
    assert wait_for(lambda: read_field(host, 3, 0x01, 0, 12) > 1000, timeout=20.0), (
        "no raster after the preset load, so there is nothing to compare")
    time.sleep(8)  # detection settles; CLAUDE.md says discard ~6 s
    after_preset = _engine_outputs(host)

    # get_json returns (status, parsed); the module's own framing() helper does
    # the same, which is why this unpacks rather than subscripting.
    status, current = get_json(host, "/geometry")
    assert status == 200 and current is not None, (
        f"/geometry did not answer with the framing: {status}")
    get(host, f"/geometry?ph={current['ph']}&pv={current['pv']}"
              f"&zh={current['zh']}&zv={current['zv']}")
    time.sleep(4)  # loop() drains the request and re-solves
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


# The ADC sampling divider, owned by Tv5725::Sampling. One quantity, three
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


def test_the_sampling_divider_is_one_quantity_in_three_registers(host, source):
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
    divider = read_field(host, *SAMPLING_PLLAD_MD)
    assert divider, "could not read PLLAD_MD, so there is no divider to check"

    # **ONLY IF_HSYNC_RST IS POISONED, AND SP_RT_HS_SP MUST NOT BE.** The point
    # of a poison is to distinguish a fresh write from a leftover -- the TV5725
    # keeps its registers across an ESP reboot, so reading the right value proves
    # nothing on its own. One register is enough to establish that here, because
    # Tv5725::Sampling::write() writes all three from one held value or writes
    # none: if IF_HSYNC_RST came back computed, write() ran, and the other two
    # assertions below are then about what it computed.
    #
    # That matters because SP_RT_HS_SP IS the sync processor's retime window, and
    # poisoning it takes the source out: measured twice, at 1110 against a
    # 2553-sample line and again only 100 low, SP_VTOTAL falls to a steady 97/98
    # and stays there through the preset load and minutes after it, needing /sc?~
    # to re-detect. The neighbouring tests poison segment 4, the memory map,
    # where a wild value costs a frame and not the lock.
    poison_if = divider // 2 - 100
    _write_field(host, SAMPLING_IF_HSYNC_RST, poison_if)
    landed = read_field(host, *SAMPLING_IF_HSYNC_RST)
    assert landed == poison_if, (
        f"could not poison IF_HSYNC_RST, got {landed} want {poison_if} -- "
        f"without this the test cannot tell a fresh write from a leftover")

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

    # Tv5725::Sampling::ifLineFor and ::retimeStopFor. The IF counts the ADC line
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


# Tv5725::Sampling's own constants, so the expected divider is computed the way
# the firmware computes it rather than pinned to this bench's source.
SAMPLING_MAX_RATE_HZ = 162000000
SAMPLING_RECOMMENDED_PERCENT = 98
SAMPLING_DIVIDER_MAX = 4095
SAMPLING_PAL_VTOTAL_MIN = 290  # Capture::PalVtotalMin
SAMPLING_DEC1_BYPS = (5, 0x1F, 0, 1)
SAMPLING_DEC2_BYPS = (5, 0x1F, 1, 1)


def _expected_divider(lines, oversample, field_rate):
    """Sampling::recommendedDivider(), in Python."""
    line_rate = int(field_rate * lines)
    largest = min(SAMPLING_MAX_RATE_HZ // (line_rate * oversample),
                  SAMPLING_DIVIDER_MAX)
    return ((largest * SAMPLING_RECOMMENDED_PERCENT) // 100) & ~1


def test_the_sampling_divider_is_solved_from_the_source_not_inherited(host, source):
    """On a settled source the divider is the one the line rate implies.

    **THE COMPANION TEST DELIBERATELY DOES NOT PIN THE VALUE, AND THAT LEFT A
    HOLE THE SIZE OF A WHOLE BOOT.** It checks that PLLAD_MD, IF_HSYNC_RST and
    SP_RT_HS_SP describe the same line and that the divider was latched -- all
    of which a WRONG divider satisfies perfectly, because Tv5725::Sampling
    writes all three off whatever value it holds. "Which divider the unit lands
    on is the table's business" was true while a table chose it. It is not any
    more: the engine computes it, so it can be checked.

    Measured on a cold boot 2026-08-15 with the tables gone, deterministically
    and on every boot:

        sampling: 271 lines x 49.22 Hz -> line rate 0
        PLLAD_MD 1856, where 2548 was due

    Sampling::lineRateFrom() correctly refused an unsettled 271 lines at
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
