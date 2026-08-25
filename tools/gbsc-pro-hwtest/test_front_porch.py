"""The output front porch, against a live unit.

The scaled picture must stop short of VDS_HSYNC_RST rather than running up to
it. Measured on the bench, RiscPC 320x256@50 into a 1916 px raster: the display
window is good at VDS_DIS_HB_ST 1900 and wrong at 1910 -- a black bar, wrong
colours and a wrapping pixel down the left edge -- so the requirement is a floor
of about 16 px. src/tv5725/OutputRaster.cpp reserves CEA-861's own minimum
instead, which is 1080p60's 88 px at 148.5 MHz taken as a time: 592.6 ns, or
64 px at 108 MHz.

Read-only: no pad presses and no register writes, so this leaves the picture
alone. Needs a source the unit is SCALING -- in RGBHV bypass the VDS is out of
the video path and there is no geometry to solve, so these skip rather than
fail. See docs/rgbhv-bypass-trap.md.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import LOCKED_VTOTAL_MIN, get_json, read_field

FIELDS = [
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12),
    ("STATUS_SYNC_PROC_VTOTAL", 0, 0x1B, 0, 11),
    ("VDS_VSYNC_RST", 3, 0x02, 4, 11),
    ("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
    ("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
    ("VDS_HB_ST", 3, 0x04, 0, 12),
    ("VDS_HSCALE_BYPS", 3, 0x00, 4, 1),
]

# **A TIME, not a pixel count.** The engine reserves the front porch as
# `frontPorchNs x clock`, so the same porch is half the pixels at half the
# clock: a 480p raster runs ~54 MHz against 1080p's ~108, and a fixed pixel
# floor calibrated on one rejects the other while the engine is doing exactly
# the right thing.
#
# 444.4444 ns is the SHORTEST front porch OutputMode.cpp's table reserves
# (576p and 1024p), so it is at or under what the engine reserved whatever mode
# is in force, and still far above the ~16 px the bench measured as the
# physical floor. The point is to catch a return to bounding the picture at the
# raster's edge, which left 6 px.
MIN_FRONT_PORCH_NS = 444.4444

# CEA-861 1080p: 1080 active + 4 front + 5 sync + 36 back. Vertical is in lines
# and needs no conversion, so the standard's own figure is the floor.
MIN_FRONT_PORCH_LINES = 4

# What any source this scaler takes actually runs at. The field rate is derived
# from two registers read one after the other, and an unlocked or mid-change
# sync processor yields a plausible-looking number from them -- 284 Hz, off a
# count of 55 -- which then sets a floor nothing could satisfy.
FIELD_RATE_MIN_HZ = 45.0
FIELD_RATE_MAX_HZ = 90.0


@pytest.fixture
def raster(host, source):
    state = {name: read_field(host, seg, reg, lo, width)
             for name, seg, reg, lo, width in FIELDS}
    if any(v is None for v in state.values()):
        pytest.skip("could not read the raster registers")
    if state["VDS_HSCALE_BYPS"] or not state["VDS_HSYNC_RST"]:
        pytest.skip("the unit is not scaling; there is no raster to check")

    # The clock the porch is reserved at, which needs the source's field rate:
    # /geometry holds the line rate and the sync processor the count it is over.
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
    """The floor in pixels of THIS raster, from the clock it runs at. The output
    frame is steered to the source's field rate, so the clock is the raster
    times that rate."""
    line = raster["VDS_HSYNC_RST"] + 1
    frame = raster["VDS_VSYNC_RST"] + 1
    clock_hz = line * frame * raster["fieldRateHz"]
    return round(MIN_FRONT_PORCH_NS * clock_hz / 1e9)


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
