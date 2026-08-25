"""What the engine measured about the source, against a live unit.

The sync processor's coast settings, its pulse-ignore count and the SOG level
step are set for a 15 kHz line whose vertical interval carries equalisation and
serration pulses. The firmware used to ask a source's standard number for that,
which carries both facts at once and is wrong about a programmable RGBHV source:
the bench RiscPC runs a 15.6 kHz line with separate H and V sync and no
serration, and is filed under 480p because scaling RGBHV borrows that branch.

/geometry reports the held line rate and the answer derived from it, so the
derivation is assertable without reading it back out of a register nothing
writes. docs/investigations/serrated-sync-is-not-line-rate.md

Read-only, so no opt-in flag: nothing here writes a register or moves the
picture.
"""

import json
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import GEOMETRY_GATED, get_json, read_field

CATALOGUE = json.load(open(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "tv5725_registers.json")))

# Tv5725::SourceMeasurement::LowLineRateBelowHz.
LOW_LINE_RATE_BELOW_HZ = 20000


def field(host, name):
    """By name. A hand-written address does not error, it returns a plausible
    number: STATUS_SYNC_PROC_HTOTAL off by two registers reads a steady 0."""
    f = CATALOGUE[name]
    return read_field(host, f["seg"], f["reg"], f["off"], f["width"])


@pytest.fixture
def geometry(host):
    status, body = get_json(host, "/geometry")
    if status == 404:
        pytest.skip(GEOMETRY_GATED)
    assert status == 200, f"/geometry answered {status}"
    if body is None or "lineRateHz" not in body:
        pytest.skip("this firmware's /geometry reports no source measurement")
    return body


def test_the_engine_reports_a_line_rate_it_has_measured(source, geometry):
    assert geometry["lineRateHz"] > 0, (
        "no line rate held; the engine has not completed a measurement")


def test_the_line_rate_agrees_with_the_line_count_the_sync_processor_gives(
        host, source, geometry):
    """rate / lines is the field rate, and no source runs outside 45..75 Hz.

    A rate held against the wrong line count is the failure this catches: it
    passes every bounds check on either quantity alone.
    """
    lines = field(host, "STATUS_SYNC_PROC_VTOTAL")
    assert lines > 0
    field_rate = geometry["lineRateHz"] / lines
    assert 45.0 < field_rate < 75.0, (
        f"{geometry['lineRateHz']} Hz over {lines} lines is {field_rate:.1f} Hz")


def test_the_low_line_rate_answer_follows_the_rate_it_came_from(source, geometry):
    assert geometry["lowLineRate"] == (
        0 < geometry["lineRateHz"] < LOW_LINE_RATE_BELOW_HZ)


def test_a_separate_sync_source_keeps_its_horizontal_count(host, source):
    """The consequence, and the reason the split exists.

    Sub-coast suppresses horizontal counting through the serrated part of the
    vertical interval. Disabled on a source that has no serration, the vertical
    edges enter the count: measured 3116..3251 against a 2250 divider, where
    locked it equals the divider exactly.
    """
    if field(host, "SP_SOG_MODE") != 0:
        pytest.skip("csync source; serration is expected and sub-coast applies")

    divider = field(host, "PLLAD_MD")
    counted = field(host, "STATUS_SYNC_PROC_HTOTAL")
    assert abs(counted - divider) <= 2, (
        f"sync processor counts {counted} ADC clocks a line against a divider "
        f"of {divider}; locked, the two agree")
