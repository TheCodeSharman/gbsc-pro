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
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe
import geometry_math as gm
from gbs_unit import (field_from, get, get_json, read_reg, read_segment,
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
# ZOOM_STEP_PX mirrors ControlSteps -- one measured output raster, and
# upstream's untouched value for every raster nobody has swept.
LINE_1080P = 1445
FETCH_1080P = 204
OFFSET_1080P = 250
UPSTREAM_FETCH = 256
UPSTREAM_OFFSET = UPSTREAM_FETCH + 4


# Mirrors Tv5725::Memory. The fetch has to cover the source pixels each output
# line needs, over the playback request budget; the offset deliberately does not
# follow it, being clean anywhere across 190..256 when it was measured.
REQUESTS_PER_LINE = 4
FETCH_FLOOR = 150


def expected_burst(line_px, capture):
    if line_px != LINE_1080P:
        return UPSTREAM_FETCH, UPSTREAM_OFFSET
    fetch = max(FETCH_FLOOR, -(-capture // REQUESTS_PER_LINE))
    return fetch, OFFSET_1080P


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
    if line_px != LINE_1080P:
        pytest.skip(f"a {line_px} px output line keeps upstream's value, so "
                    f"there is no repair to observe -- put the unit in 1080p")

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
    """
    seen = set()
    for _ in range(24):
        press(host, probe, "z")
        settled_geometry(host)
        seen.add(probe.read_field(PB_FETCH_NUM))

    assert len(seen) > 1, (
        f"PB_FETCH_NUM stayed at {seen} across 24 zoom presses -- it is either "
        "not being written or the capture is not reaching Memory::fetchFor")


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
