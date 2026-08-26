"""The output front porch, against a live unit.

The scaled picture must stop short of VDS_HSYNC_RST rather than running up to
it. Measured on the bench, RiscPC 320x256@50 into a 1916 px raster: the display
window is good at VDS_DIS_HB_ST 1900 and wrong at 1910 -- a black bar, wrong
colours and a wrapping pixel down the left edge. OutputMode::FrontPorchMinPx
reserves 16 px against that, in pixels rather than as a time, and this asserts
the same number.

Read-only: no pad presses and no register writes, so this leaves the picture
alone. Needs a source the unit is SCALING -- in RGBHV bypass the VDS is out of
the video path and there is no geometry to solve, so these skip rather than
fail. See docs/rgbhv-bypass-trap.md.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import (LOCKED_VTOTAL_MIN, field_from, get_json, locked_steadily,
                      read_field, read_segment, wait_for)

# **ALL OF SEGMENT 3, READ IN ONE PASS.** Every figure the porch arithmetic uses
# is an output of the same solve, and the engine re-solves whenever the measured
# field rate moves -- so a window read in one request and a raster total read in
# the next can come from two different solves and disagree by design. Measured:
# VDS_DIS_HB_ST 1896 from a 1916 px raster paired with a VDS_HSYNC_RST from a
# 1911 px one, reported as the picture overrunning the front porch by a pixel
# when neither solve did. CLAUDE.md, "Read every register the arithmetic uses in
# ONE pass".
RASTER_FIELDS = [
    ("VDS_HSYNC_RST", 0x01, 0, 12),
    ("VDS_VSYNC_RST", 0x02, 4, 11),
    ("VDS_DIS_HB_ST", 0x10, 0, 12),
    ("VDS_DIS_VB_ST", 0x13, 0, 11),
    ("VDS_HB_ST", 0x04, 0, 12),
    ("VDS_HSCALE_BYPS", 0x00, 4, 1),
]

# **A PIXEL COUNT, not a time**, and the one horizontal quantity that is.
# OutputMode converts CEA-861's sync and back porch to times so they port to a
# raster the standard does not have, and deliberately does not do that here: the
# encoder generates its own HDMI blanking and never sees ours, so what the far
# end of the line owes is the board's floor. OutputMode::FrontPorchMinPx, and
# this is the same number.
#
# The point is to catch a return to bounding the picture at the raster's edge,
# which left 6 px. docs/scaler-geometry-model.md "The output front porch"
MIN_FRONT_PORCH_PX = 16

# CEA-861 1080p: 1080 active + 4 front + 5 sync + 36 back. Vertical is in lines
# and needs no conversion, so the standard's own figure is the floor.
MIN_FRONT_PORCH_LINES = 4

# What any source this scaler takes actually runs at. The field rate is derived
# from two registers read one after the other, and an unlocked or mid-change
# sync processor yields a plausible-looking number from them -- 284 Hz, off a
# count of 55 -- which then sets a floor nothing could satisfy.
FIELD_RATE_MIN_HZ = 45.0
FIELD_RATE_MAX_HZ = 90.0


# A detection pass takes the source out for about five seconds, and a preceding
# teardown returns as soon as it locks rather than when the pass ends.
LOCK_WAIT_S = 40.0


@pytest.fixture
def raster(host, source):
    # **WAIT FOR THE SOURCE, DO NOT SKIP ON IT.** These read the sync processor
    # to derive the field rate, so a run that reaches them mid-detection skips --
    # and a test that skips is not a test that passes. Measured: they skipped
    # through the change to OutputMode::FrontPorchMinPx that they exist to guard,
    # in every full run taken, because a neighbour had just re-detected.
    wait_for(lambda: locked_steadily(host), timeout=LOCK_WAIT_S)

    registers = read_segment(host, 3, 0x00, 0x20)
    if registers is None:
        pytest.skip("no /getregs on this firmware, so the raster cannot be read "
                    "in one pass and the figures cannot be compared")
    state = {name: field_from(registers, reg, lo, width)
             for name, reg, lo, width in RASTER_FIELDS}
    state["STATUS_SYNC_PROC_VTOTAL"] = read_field(host, 0, 0x1B, 0, 11)
    if any(v is None for v in state.values()):
        pytest.skip("could not read the raster registers")
    if state["VDS_HSCALE_BYPS"] or not state["VDS_HSYNC_RST"]:
        pytest.skip("the unit is not scaling; there is no raster to check")

    # **NOT for the porch, which is a constant in pixels.** Deriving the field
    # rate is how this establishes that the source is really being followed:
    # /geometry holds the line rate and the sync processor the count it is over,
    # and two readings from different moments give a rate no source runs.
    status, report = get_json(host, "/geometry")
    if status != 200 or not report or not report.get("lineRateHz"):
        pytest.skip("no source line rate held, so the output clock is unknown")
    lines = state["STATUS_SYNC_PROC_VTOTAL"]
    if lines < LOCKED_VTOTAL_MIN:
        pytest.skip(f"the sync processor counts {lines}, so it is not following "
                    "the source and the field rate cannot be derived")
    rate = report["lineRateHz"] / lines
    if not FIELD_RATE_MIN_HZ <= rate <= FIELD_RATE_MAX_HZ:
        pytest.skip(f"derived a field rate of {rate:.1f} Hz, which no source "
                    "runs: the two readings did not come from one moment")
    state["fieldRateHz"] = rate
    return state


def minimum_porch_px(raster):
    """The floor the engine reserves, which is a constant in pixels."""
    return MIN_FRONT_PORCH_PX


def test_the_display_window_leaves_a_front_porch(raster):
    line = raster["VDS_HSYNC_RST"] + 1
    porch = line - raster["VDS_DIS_HB_ST"]
    floor = minimum_porch_px(raster)
    assert porch >= floor, (
        f"display blanking starts at {raster['VDS_DIS_HB_ST']} in a {line} px "
        f"line, leaving {porch} px of front porch against a floor of {floor}. "
        "Below ~16 px the bench shows a black bar, wrong colours and a wrapping "
        "pixel down the left edge."
    )


def test_the_memory_window_leaves_one_too(raster):
    """The memory window is the display window, so it owes the same porch.

    Separate from the test above because they are separate registers: a change
    that bounded one and not the other would pass with the picture torn.
    """
    line = raster["VDS_HSYNC_RST"] + 1
    porch = line - raster["VDS_HB_ST"]
    floor = minimum_porch_px(raster)
    assert porch >= floor, (
        f"VDS_HB_ST {raster['VDS_HB_ST']} leaves {porch} px of a {line} px line, "
        f"against a floor of {floor}"
    )


def test_the_frame_leaves_a_vertical_front_porch(raster):
    frame = raster["VDS_VSYNC_RST"] + 1
    porch = frame - raster["VDS_DIS_VB_ST"]
    assert porch >= MIN_FRONT_PORCH_LINES, (
        f"display blanking starts at line {raster['VDS_DIS_VB_ST']} of {frame}, "
        f"leaving {porch} lines against CEA-861's {MIN_FRONT_PORCH_LINES}"
    )
