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


def retime(host, probe, command, timeout=8.0):
    """Move VDS_HSYNC_RST by one and wait for the retime to land.

    /sc?a and /sc?A set rto->forceRetime and call applyBestHTotal directly, so
    they are the only way to provoke a retime from outside. It matters that
    they force it: applyBestHTotal returns early when diffHTotal is 0, which is
    every settled lock, and that early return is why a press-then-read test
    cannot see this fault.
    """
    spec = ("VDS_HSYNC_RST", 3, 0x01, 0, 12)
    before = probe.read_field(spec)
    status, _ = get(host, f"/sc?{command}")
    assert status == 200, f"/sc?{command} returned {status}"
    landed = wait_for(lambda: probe.read_field(spec) != before, timeout=timeout)
    assert landed, (f"/sc?{command} did not move VDS_HSYNC_RST from {before} "
                    f"within {timeout}s")


@pytest.mark.zoom
def test_the_geometry_survives_a_retime(host, probe, framed):
    """A retime must re-solve the windows, not slide them.

    applyBestHTotal moves VDS_HSYNC_RST and then shifts the four blanking
    registers by diffHTotal/2. The picture does not follow: `produced` is
    capture x 1024 / scale and has no htotal term in it, so the windows end up
    somewhere the picture is not, and unwritten memory scans out at the edge.

    Retiming four times and back keeps the raster where it started while making
    the firmware take that path eight times, so nothing here depends on the
    unit ending on a different htotal from the one it began with.
    """
    press(host, probe, "z")
    settled_geometry(host)

    for _ in range(4):
        retime(host, probe, "a")
    for _ in range(4):
        retime(host, probe, "A")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    assert not differences(predict(after), after), (
        "the windows no longer match what the capture and the raster imply -- "
        "they were slid rather than re-solved")


def derange(probe):
    """Put the output geometry somewhere the engine would never leave it.

    A pan on its own cannot expose a stale window: it does not change the
    capture WIDTH, so `produced` is unchanged and the window the engine would
    compute is the one already there. The staleness has to be put in first --
    which is what test_regpanel did by seeding its fake device, and what a
    session of experiments does to a real unit.

    1:1 with the aperture wound in is the state the bench was actually left in
    on 2026-08-08: a 1065 px picture inside a 1445 px raster that no amount of
    panning could push out to the panel edge.
    """
    probe.write_field(("VDS_HSCALE", 3, 0x16, 0, 10), 1023)
    probe.write_field(("VDS_DIS_HB_SP", 3, 0x11, 4, 12), 256)
    probe.write_field(("VDS_DIS_HB_ST", 3, 0x10, 0, 12), 1262)


@pytest.mark.pan
def test_the_osd_move_recomputes_the_display_window(host, probe, framed):
    """Ported from test_regpanel's test_panning_recomputes_the_display_window_too.

    Size on the remote goes through the engine because the OSD writes into
    serialCommand; move did not. It wrote IF_HB_SP2/ST2 by +/-4 directly and
    left the display window standing, so the capture moved and the aperture
    did not -- seen on the bench on 2026-08-08 as a picture that stopped short
    of the panel edge no matter how far it was panned.
    """
    derange(probe)
    press(host, probe, "7")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    assert not differences(predict(after), after), (
        "the move key left a stale display window against a moved capture")


@pytest.mark.pan
def test_the_osd_move_keeps_the_capture_window_intact(host, probe, framed):
    """The two edges must move together.

    The old handler gave each edge its own limit and wrapped it to 0 on its
    own, so IF_HB_ST2 could roll to the start of the line while IF_HB_SP2 kept
    climbing. That is how a pan produced capture 292..80 -- a window whose stop
    is before its start, which no solver can use.
    """
    before = framed
    press(host, probe, "7")
    after = settled_geometry(host)

    assert after["IF_HB_ST2"] > after["IF_HB_SP2"], (
        f"capture inverted: {after['IF_HB_SP2']}..{after['IF_HB_ST2']}")
    assert after["IF_HB_ST2"] - after["IF_HB_SP2"] \
        == before["IF_HB_ST2"] - before["IF_HB_SP2"], "a move must not resize"


@pytest.mark.pan
def test_the_display_window_never_starts_before_the_scaler_does(host, probe, framed):
    """Ported from test_regpanel. The scaler does not begin writing until
    VDS_HB_SP + origin_offset(magnification); a display window opening earlier
    scans out frame buffer nobody wrote this frame."""
    derange(probe)
    press(host, probe, "7")
    after = settled_geometry(host)

    magnify = gm.magnification(after["VDS_HSCALE"], after["VDS_HSCALE_BYPS"])
    write_start = after["VDS_HB_SP"] + gm.AXIS_H.origin_offset(magnify)
    assert after["VDS_DIS_HB_SP"] >= write_start - 1


@pytest.mark.pan
def test_the_display_window_never_ends_after_the_scaler_stops(host, probe, framed):
    """The same invariant at the far edge, which is where it was first seen as
    a band of unwritten memory down the right of the screen."""
    derange(probe)
    press(host, probe, "7")
    after = settled_geometry(host)

    magnify = gm.magnification(after["VDS_HSCALE"], after["VDS_HSCALE_BYPS"])
    write_start = after["VDS_HB_SP"] + gm.AXIS_H.origin_offset(magnify)
    produced = gm.produced_px(after["IF_HB_ST2"] - after["IF_HB_SP2"],
                              after["VDS_HSCALE"], after["VDS_HSCALE_BYPS"],
                              axis=gm.AXIS_H)
    assert after["VDS_DIS_HB_ST"] <= write_start + produced + 1


@pytest.mark.pan
def test_any_press_puts_the_picture_back_to_full_size(host, probe, framed):
    """Ported from test_regpanel, and Michael's rule: compute a sensible state,
    never inherit one. Whatever the registers hold -- a scale left at 1:1 by a
    test, a window someone typed in -- the next press recomputes rather than
    building on it."""
    derange(probe)
    press(host, probe, "7")
    after = settled_geometry(host)

    capture = after["IF_HB_ST2"] - after["IF_HB_SP2"]
    scale, full = gm.fit_to_raster(capture, after["VDS_HSYNC_RST"] + 1, gm.AXIS_H)
    assert after["VDS_HSCALE"] == scale, "the scale must be recomputed"
    width = after["VDS_DIS_HB_ST"] - after["VDS_DIS_HB_SP"]
    assert full - gm.AXIS_H.margin - 1 <= width <= full


# Every geometry register the engine owns, set to something it must not keep:
# capture windows inverted on both axes, both scales at 1:1 and bypassed, and
# the memory and display windows wound into a narrow band in the middle of the
# raster. Nothing here is a plausible state -- that is the point.
HOSTILE = {
    "IF_HB_SP2": 300, "IF_HB_ST2": 200,
    "IF_VB_SP": 400, "IF_VB_ST": 100,
    "VDS_HSCALE": 1023, "VDS_VSCALE": 1023,
    "VDS_HSCALE_BYPS": 1, "VDS_VSCALE_BYPS": 1,
    "VDS_HB_SP": 700, "VDS_HB_ST": 800,
    "VDS_VB_SP": 500, "VDS_VB_ST": 600,
    "VDS_DIS_HB_SP": 700, "VDS_DIS_HB_ST": 800,
    "VDS_DIS_VB_SP": 500, "VDS_DIS_VB_ST": 600,
}


def make_hostile(probe):
    """Overwrite every engine-owned register with a value it must not inherit."""
    for spec in GEOMETRY_FIELDS:
        if spec[0] in HOSTILE:
            probe.write_field(spec, HOSTILE[spec[0]])


def expected_default_capture(units, vertical, field_rate=50.0):
    """The capture window src/tv5725/driver.h derives for an untuned source.

    Mirrors Geometry::captureFor at rest. Python's default_capture_window is
    horizontal only, so the vertical fraction is spelled out here; and the
    firmware rounds with lrintf (half away from zero) where Python's round() is
    half to even, which is a real one-count difference on some inputs.
    """
    fraction = gm.DEFAULT_H_ACTIVE_FRACTION
    if vertical:
        fraction = (gm.DEFAULT_V_ACTIVE_FRACTION_50HZ if field_rate < 55.0
                    else gm.DEFAULT_V_ACTIVE_FRACTION_60HZ)
    width = min(units, int(math.floor(units * fraction * gm.OVER_CAPTURE + 0.5)))
    start = (units - width) // 2
    return start, start + width


def test_a_preset_load_recomputes_every_register_from_scratch(
        host, probe, preset_load, framed):
    """Setting a mode must compute the geometry, inheriting nothing.

    doPostPresetLoadSteps wrote VDS_VSCALE 455/683/410/402/546/400 and a table
    of IF_HB_ST2/SP2 straight from the preset, and nothing recomputed them, so
    the unit came up in whatever the preset left and stayed there until a pad
    was pressed. On the bench that was VDS_HSCALE 1023 bypassed -- a 1020 px
    picture in a 1445 px raster -- and 83 source lines stretched over 1126.
    Every broken picture of 2026-08-08 was that state; every clean one was one
    press away.

    Reading the capture back and repairing it only when it is degenerate is not
    enough, and was the first attempt at this: it still lets the preset choose
    the framing. The window has to be derived from the line length and the field
    rate, which is what the Python prototype has always done.

    So this writes a hostile value into EVERY register the engine owns first --
    inverted captures, 1:1 bypassed scales, windows wound into a band in the
    middle of the raster -- and requires all of it to be replaced.

    CHANGES THE OUTPUT PRESET, hence --preset-load. /sc?2 loads pal_768x576,
    which is the point: the engine must solve against the raster the preset just
    chose. Nothing here puts the output raster back -- FrameSync owns
    VDS_?SYNC_RST -- so the unit stays in that preset until the next real mode
    change.
    """
    raster = ("VDS_HSYNC_RST", 3, 0x01, 0, 12)
    before = probe.read_field(raster)
    make_hostile(probe)

    status, _ = get(host, "/sc?2")
    assert status == 200, f"/sc?2 returned {status}"
    landed = wait_for(lambda: probe.read_field(raster) != before, timeout=20.0)
    assert landed, f"the preset did not load: VDS_HSYNC_RST stayed at {before}"

    after = settled_geometry(host, timeout=20.0)
    assert after is not None, "could not read the geometry back"

    # The capture is DERIVED, not read back and patched up.
    want_h = expected_default_capture(after["IF_HSYNC_RST"] + 1, vertical=False)
    want_v = expected_default_capture(2 * (after["VTOTAL"] + 1), vertical=True)
    assert (after["IF_HB_SP2"], after["IF_HB_ST2"]) == want_h, (
        "the horizontal capture was inherited rather than computed")
    assert (after["IF_VB_SP"], after["IF_VB_ST"]) == want_v, (
        "the vertical capture was inherited rather than computed")

    # And everything downstream of it follows from that capture and the raster.
    assert not differences(predict(after), after), (
        "the preset's geometry was left standing instead of being recomputed")


@pytest.mark.zoom
def test_a_press_computes_the_progressive_line_window_from_scratch(host, probe, framed):
    """Both ends of IF_LINE_ST/SP, recomputed rather than inherited.

    That window is the line double's timing -- change it and the two fields of
    an interlaced source separate -- and it has to span exactly one line. It was
    maintained in one place, the 'n' serial key, as a constant 0x40 plus the
    line, while PLLAD_MD moved from six.

    The START is the point of this test. Deriving the stop from a read of
    IF_LINE_ST was the first version and it broke the engine's own rule:
    readRasters() reads rasters and source measurements, and nothing else,
    because everything else is inherited by definition. Worse, it meant a
    clobbered IF_LINE_ST -- which is what a write landing in the wrong segment
    produces -- would propagate into IF_LINE_SP on every solve for as long as
    the mode lasted. So both ends are deranged here, and both must come back.
    """
    line_st = ("IF_LINE_ST", 1, 0x20, 0, 12)
    line_sp = ("IF_LINE_SP", 1, 0x22, 0, 12)
    hsync_rst = ("IF_HSYNC_RST", 1, 0x0E, 0, 11)

    probe.write_field(line_st, 300)
    probe.write_field(line_sp, 999)

    press(host, probe, "z")
    settled_geometry(host)

    units = probe.read_field(hsync_rst) + 1
    start = probe.read_field(line_st)
    stop = probe.read_field(line_sp)

    assert start == 64, f"IF_LINE_ST inherited rather than written: {start}"
    assert stop == start + units, (
        f"progressive window does not span one line: {start}..{stop} "
        f"is {stop - start} of {units} units")


def test_a_press_recovers_from_any_register_state(host, probe, framed):
    """The same hostile state, cleared by a single pad press.

    Michael, 2026-08-06: "you should CALCULATE a sensible starting state and not
    rely on the existing state AT ALL." A press keeps the user's framing -- that
    is the one thing it must inherit, or panning would be meaningless -- but
    every register downstream of the capture is recomputed. This needs no preset
    load, so it runs on a plain --source suite.
    """
    make_hostile(probe)
    press(host, probe, "z")
    after = settled_geometry(host)

    assert after is not None, "could not read the geometry back"
    assert after["IF_HB_ST2"] > after["IF_HB_SP2"], (
        f"horizontal capture left unusable: "
        f"{after['IF_HB_SP2']}..{after['IF_HB_ST2']}")
    assert after["IF_VB_ST"] > after["IF_VB_SP"], (
        f"vertical capture left unusable: "
        f"{after['IF_VB_SP']}..{after['IF_VB_ST']}")
    assert not differences(predict(after), after)


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
