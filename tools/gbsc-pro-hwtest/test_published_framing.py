"""A source running a published raster, against a live unit.

Where active video starts cannot be measured, so an untuned source is placed
from an assumption -- except where the source is running a mode the standards
state, which says exactly where picture begins and how long it lasts. The proof
is that the capture window lands on the standard's own numbers rather than near
them. docs/investigations/vesa-modes-are-clipped-by-default.md

Changes the SOURCE mode, so it needs --modeserv and puts the bench mode back
however it ends. The picture is a VESA raster while it runs.
"""

import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import (handed_over, mode_serv, read_fields, reset_framing,
                      wait_for, want_route)

# VESA DMT 640x480@60, as the standard states it: 800 pixels a line, active
# video from 144 to 784; 525 lines a frame, active from 35 to 515.
VESA_MODE = "MODE X640 Y480 C256 F60"
BENCH_MODE = "MODE X320 Y256 C256 F50"
VESA_LINES = 525
VESA_START_FRACTION = 144.0 / 800.0
VESA_ACTIVE_FRACTION = 640.0 / 800.0
VESA_FIRST_LINE = 35
VESA_ACTIVE_LINES = 480

# The horizontal window is a fraction of a line measured in ADC samples, so it
# rounds; the vertical is whole lines of the standard's own frame and does not.
HORIZONTAL_TOLERANCE_UNITS = 3
VERTICAL_TOLERANCE_LINES = 1

# Where a source runs no raster the standards state, the window is placed across
# the envelope of what real sources put on a line. AKF50's 320x256 is such a
# source: it keeps no standard's raster, and the bench is where that is known.
ENVELOPE_START = 0.117
ENVELOPE_EXTENT = 0.864
ENVELOPE_TOLERANCE = 0.004

# A mode change is followed by several seconds of readings inside no standard at
# all, and a solve that lands on one is not the solve being tested.
SETTLE_SECONDS = 11.0

FIELDS = ("STATUS_SYNC_PROC_VTOTAL", "IF_HSYNC_RST",
          "IF_HB_ST2", "IF_HB_SP2", "IF_VB_ST", "IF_VB_SP")




@pytest.fixture
def vesa_source(request, host):
    where = request.config.getoption("--modeserv")
    reply = mode_serv(where, VESA_MODE)
    try:
        if not reply.startswith("OK"):
            pytest.skip(f"the source refused {VESA_MODE}: {reply}")
        time.sleep(SETTLE_SECONDS)
        counted = wait_for(
            lambda: (read_fields(host, ("STATUS_SYNC_PROC_VTOTAL",)) or {}).get(
                "STATUS_SYNC_PROC_VTOTAL"),
            timeout=20.0, interval=0.5)
        # The sync processor counts from zero, so the 525-line frame reads 524.
        if counted != VESA_LINES - 1:
            pytest.skip(f"the source is counting {counted} lines, not {VESA_LINES - 1}")
        # **THE CAPTURE WINDOW IS THE SCALING ROUTE'S.** 525 lines at 60 Hz
        # suits pass-through and a source with nothing stored takes it, where
        # the input formatter is out of the circuit and its registers hold the
        # previous source's -- measured IF_HB_SP2 236 and IF_HB_ST2 926 on this
        # mode, which are the bench 320x256@50 window exactly.
        # docs/rgbhv-bypass-trap.md
        scaled = want_route(host, False, tries=4, settle=25.0)
        assert scaled, f"the source would not scale, so it has no capture window: {scaled}"
        time.sleep(SETTLE_SECONDS)
        yield
    finally:
        mode_serv(where, BENCH_MODE)
        time.sleep(SETTLE_SECONDS)


@pytest.mark.source_mode
def test_a_vesa_source_is_captured_where_its_raster_puts_picture(host, vesa_source):
    at = read_fields(host, FIELDS)
    assert at, "no register read came back"

    line = at["IF_HSYNC_RST"] + 1
    start = at["IF_HB_SP2"]
    width = at["IF_HB_ST2"] - at["IF_HB_SP2"]

    assert abs(start - VESA_START_FRACTION * line) <= HORIZONTAL_TOLERANCE_UNITS, (
        f"capture starts at {start} of {line} units, "
        f"{100.0 * start / line:.2f}% against the raster's 18.00%")
    assert abs(width - VESA_ACTIVE_FRACTION * line) <= HORIZONTAL_TOLERANCE_UNITS, (
        f"capture runs {width} of {line} units, "
        f"{100.0 * width / line:.2f}% against the raster's 80.00%")


@pytest.mark.source_mode
def test_the_vertical_window_is_the_rasters_own_active_lines(host, vesa_source):
    at = read_fields(host, FIELDS)
    assert at, "no register read came back"

    first = at["IF_VB_SP"]
    lines = at["IF_VB_ST"] - at["IF_VB_SP"]

    assert abs(first - VESA_FIRST_LINE) <= VERTICAL_TOLERANCE_LINES, (
        f"capture starts on line {first}, not the raster's {VESA_FIRST_LINE}")
    assert abs(lines - VESA_ACTIVE_LINES) <= VERTICAL_TOLERANCE_LINES, (
        f"capture runs {lines} lines, not the raster's {VESA_ACTIVE_LINES}")


@pytest.mark.source_mode
def test_a_reset_lands_on_a_source_it_hands_over(host, vesa_source):
    """The reset forgets the route with the framing, so a source pass-through
    can carry has no framing afterwards -- and that IS where the reset landed.

    One /framing.txt record per source holds both, and a source with nothing
    stored is passed through. Waiting for a framing to settle therefore asks the
    reset to leave behind the record it exists to forget: measured on this mode,
    /geometry reports zeros at every read from 1 s to 12 s afterwards, and
    reset_framing() answered None for the whole 20 s window. That blocked eight
    of the ten acceptance cases in test_card_framing before they measured
    anything.
    """
    landed = reset_framing(host)

    assert landed is not None, (
        "the reset reported no landing on a source it hands over")
    assert handed_over(host), (
        "the source was not handed over, so this is not the case under test")


# **THE ENVELOPE CASE HAS LEFT THIS FILE**, because no bench source can reach
# it. `SourceTiming`'s table publishes `320x256@50 15.6 kHz` -- 512 total, 36
# sync, 110 back porch, 320 active -- so the bench source is placed from its
# raster like any other: measured 0.2153/0.6249 against the envelope's
# 0.117/0.864, with the shape off and after a reset, and 320/512 is 0.625.
# A test asserting the envelope there asserts the wrong behaviour.
#
# The envelope itself is pinned where it can be exercised:
# test/test_capture_window.cpp, "a source matching no published raster is
# captured across the envelope".
