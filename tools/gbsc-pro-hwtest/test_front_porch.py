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
from gbs_unit import read_field

FIELDS = [
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12),
    ("VDS_VSYNC_RST", 3, 0x02, 4, 11),
    ("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
    ("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
    ("VDS_HB_ST", 3, 0x04, 0, 12),
    ("VDS_HSCALE_BYPS", 3, 0x00, 4, 1),
]

# Comfortably under the 64 px the engine reserves at 108 MHz and comfortably over
# the ~16 px the bench measured as the floor. The point is to catch a return to
# bounding the picture at the raster's edge, which left 6 px.
MIN_FRONT_PORCH_PX = 40

# CEA-861 1080p: 1080 active + 4 front + 5 sync + 36 back. Vertical is in lines
# and needs no conversion, so the standard's own figure is the floor.
MIN_FRONT_PORCH_LINES = 4


@pytest.fixture
def raster(host, source):
    state = {name: read_field(host, seg, reg, lo, width)
             for name, seg, reg, lo, width in FIELDS}
    if any(v is None for v in state.values()):
        pytest.skip("could not read the raster registers")
    if state["VDS_HSCALE_BYPS"] or not state["VDS_HSYNC_RST"]:
        pytest.skip("the unit is not scaling; there is no raster to check")
    return state


def test_the_display_window_leaves_a_front_porch(raster):
    line = raster["VDS_HSYNC_RST"] + 1
    porch = line - raster["VDS_DIS_HB_ST"]
    assert porch >= MIN_FRONT_PORCH_PX, (
        f"display blanking starts at {raster['VDS_DIS_HB_ST']} in a {line} px "
        f"line, leaving {porch} px of front porch against a floor of "
        f"{MIN_FRONT_PORCH_PX}. Below ~16 px the bench shows a black bar, wrong "
        "colours and a wrapping pixel down the left edge."
    )


def test_the_memory_window_leaves_one_too(raster):
    """The memory window is the display window, so it owes the same porch.

    Separate from the test above because they are separate registers: a change
    that bounded one and not the other would pass with the picture torn.
    """
    line = raster["VDS_HSYNC_RST"] + 1
    porch = line - raster["VDS_HB_ST"]
    assert porch >= MIN_FRONT_PORCH_PX, (
        f"VDS_HB_ST {raster['VDS_HB_ST']} leaves {porch} px of a {line} px line"
    )


def test_the_frame_leaves_a_vertical_front_porch(raster):
    frame = raster["VDS_VSYNC_RST"] + 1
    porch = frame - raster["VDS_DIS_VB_ST"]
    assert porch >= MIN_FRONT_PORCH_LINES, (
        f"display blanking starts at line {raster['VDS_DIS_VB_ST']} of {frame}, "
        f"leaving {porch} lines against CEA-861's {MIN_FRONT_PORCH_LINES}"
    )
