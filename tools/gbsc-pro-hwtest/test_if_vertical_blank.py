"""The input formatter stays measurable across a re-solve, against a live unit.

The field rate is timed off the input formatter's test bus and HPERIOD_IF is
counted inside the same block, and both stop when IF_VB_ST lies beyond the
frame: the window never fires, so there is nothing to time and nothing to read.
Geometry::write() is the only writer and it runs after a successful solve, so a
window solved for a taller mode strands the measurement that would replace it,
and the engine never gets out.

Geometry::holdReferenceSampling() parks the window at 0..2 -- inside every frame
any source presents -- before anything is timed through the block. This puts a
window beyond the frame by hand and asks the engine to re-solve: without that
parking the solve cannot complete and the capture stays frozen.

docs/investigations/if-vertical-blank-strands-the-measurement.md

Opt-in: it drops the framing and disturbs the picture for a few seconds.
"""

import json
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import (GEOMETRY_GATED, get, get_json, read_field, read_reg,
                      write_reg, wait_for)

CATALOGUE = json.load(open(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "tv5725_registers.json")))

# Tv5725::SourceMeasurement::LineDoubleBelowLines.
LINE_DOUBLE_BELOW_LINES = 400


def field(host, name):
    f = CATALOGUE[name]
    return read_field(host, f["seg"], f["reg"], f["off"], f["width"])


def write_field(host, name, value):
    """Read-modify-write, so the neighbour sharing the byte survives."""
    f = CATALOGUE[name]
    span = (f["off"] + f["width"] + 7) // 8
    raw = 0
    for i in range(span):
        byte = read_reg(host, f["seg"], f["reg"] + i)
        assert byte is not None, f"could not read {name}"
        raw |= byte << (8 * i)
    mask = ((1 << f["width"]) - 1) << f["off"]
    updated = (raw & ~mask) | ((value << f["off"]) & mask)
    for i in range(span):
        if ((updated >> (8 * i)) & 0xFF) != ((raw >> (8 * i)) & 0xFF):
            write_reg(host, f["seg"], f["reg"] + i, (updated >> (8 * i)) & 0xFF)


def if_frame_lines(host):
    """What the input formatter's vertical counter wraps at: the source's line
    count, doubled when the line doubler is in the path."""
    lines = field(host, "STATUS_SYNC_PROC_VTOTAL")
    assert lines, "the sync processor is not counting the source"
    return lines * (2 if lines < LINE_DOUBLE_BELOW_LINES else 1)


def _seed_beyond_the_frame(host, frame):
    """Put IF_VB_ST outside the frame, and say whether it stayed there."""
    write_field(host, "IF_VB_ST", frame + 50)
    return field(host, "IF_VB_ST") == frame + 50 or None


def test_a_vertical_blank_beyond_the_frame_does_not_strand_the_engine(host, source):
    frame = if_frame_lines(host)
    before = field(host, "IF_VB_ST")
    assert before < frame, (
        f"IF_VB_ST {before} is already beyond the {frame}-line frame, so the "
        "unit is stranded before this test starts")

    status, report = get_json(host, "/geometry")
    if status == 404:
        pytest.skip(GEOMETRY_GATED)
    rate_before = report["lineRateHz"]
    assert rate_before, "no line rate measured, so there is nothing to lose"

    # Beyond the frame by enough that no rounding brings it back inside.
    #
    # Retried, because the engine owns this register and re-solves on its own
    # schedule: a single write can be overwritten between the write and the
    # read-back, which reads as the write having failed. Measured -- a run came
    # back with 620, an ordinary solved value, inside the same second.
    seeded = wait_for(lambda: _seed_beyond_the_frame(host, frame),
                      timeout=15.0, interval=1.0)
    assert seeded, (
        f"could not get IF_VB_ST past the {frame}-line frame: the engine "
        "rewrote it every time, so the fault this proves recovery from was "
        "never seeded")

    status, _ = get(host, "/sc?B")
    assert status == 200, f"/sc?B answered {status}"

    # The engine has to park the window itself to measure at all, so this is
    # both the fix and the only route back: nothing else writes IF_VB_ST.
    settled = wait_for(lambda: field(host, "IF_VB_ST") < frame, timeout=30.0,
                       interval=0.5)
    assert settled, (
        f"IF_VB_ST never came back inside the {frame}-line frame; the engine "
        "measured through the window it could not measure through")

    rate = wait_for(lambda: get_json(host, "/geometry")[1]["lineRateHz"],
                    timeout=30.0, interval=0.5)
    assert rate, "the engine solved no line rate after the re-solve"
    assert abs(rate - rate_before) * 1000 <= 50 * rate_before, (
        f"line rate moved from {rate_before} to {rate} across a re-solve that "
        "did not change the source")

    assert field(host, "CAPTURE_ENABLE") == 1, (
        "capture is still frozen, so the solve never completed")
