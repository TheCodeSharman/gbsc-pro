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
import socket
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import read_fields, reset_framing, wait_for

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


def mode_serv(where, command):
    """One command per connection: the close is the end of the reply."""
    with socket.create_connection((where, 6502), 10) as link:
        link.sendall((command + "\n").encode())
        return link.recv(200).decode(errors="replace").strip()


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


@pytest.fixture
def bench_source(request, host):
    """The bench mode, with the framing back at what the solve places."""
    where = request.config.getoption("--modeserv")
    reply = mode_serv(where, BENCH_MODE)
    if not reply.startswith("OK"):
        pytest.skip(f"the source refused {BENCH_MODE}: {reply}")
    time.sleep(SETTLE_SECONDS)
    reset_framing(host)
    yield


@pytest.mark.source_mode
def test_a_source_running_no_published_raster_takes_the_envelope(host, bench_source):
    at = read_fields(host, FIELDS)
    assert at, "no register read came back"

    line = at["IF_HSYNC_RST"] + 1
    start = at["IF_HB_SP2"] / line
    extent = (at["IF_HB_ST2"] - at["IF_HB_SP2"]) / line

    assert abs(start - ENVELOPE_START) <= ENVELOPE_TOLERANCE, (
        f"capture starts at {100 * start:.2f}% of the line, "
        f"not the envelope's {100 * ENVELOPE_START:.1f}%")
    assert abs(extent - ENVELOPE_EXTENT) <= ENVELOPE_TOLERANCE, (
        f"capture runs {100 * extent:.2f}% of the line, "
        f"not the envelope's {100 * ENVELOPE_EXTENT:.1f}%")

    # Later than centring would put it, because sync and back porch sit at the
    # head of the line and run far longer than the front porch. Centring is what
    # cropped the far edge of every source measured.
    assert start > (1.0 - extent) / 2.0, (
        f"capture starts at {100 * start:.2f}% where centring a "
        f"{100 * extent:.1f}% window would put it at "
        f"{100 * (1.0 - extent) / 2.0:.2f}%")
