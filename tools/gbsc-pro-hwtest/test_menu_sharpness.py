"""The menu's Sharpness row, against a live unit.

Sharpness is the two peaking band gains, and it is the one Picture Settings row
the chain could not hold: it read `VDS_PK_LB_GAIN` back to decide what to draw,
so `applyOutputResolutionSettings()` writing the resting gain on every output
change silently moved the row. A held preference is what the host suite proves;
what only the unit can show is that the gains reach the chip, survive an output
change, and come back after a boot. docs/osd-menu.md

Reversible by construction: each test puts the preference back where it found it,
so a run leaves the picture as it was.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get_json, read_fields, read_named, wait_for

PICTURE_SETTINGS = "Picture Settings"
SHARPNESS = "Sharpness"

GAINS = ["VDS_PK_LB_GAIN", "VDS_PK_LH_GAIN"]

# The gains the control writes. Sharpened is 0x1F on both bands because each
# field is six bits wide; unsharpened, only the high band follows the output.
SHARPENED = (0x1F, 0x1F)
RESTING_1080P = (0x16, 0x0A)
RESTING_OTHER = (0x16, 0x18)


def page(host, key=None):
    status, body = get_json(host, "/menu" + (f"?key={key}" if key else ""))
    if status == 404:
        pytest.skip("/menu is behind GBS_DEBUG; this build has no route")
    assert status == 200, f"/menu answered {status}"
    return body


def selected(body):
    return next(row["label"] for row in body["rows"] if row["selected"])


def row(host, label):
    """The row as the menu would draw it, from wherever the cursor is."""
    page(host, "exit")
    page(host, "menu")
    while selected(page(host)) != PICTURE_SETTINGS:
        page(host, "down")
    page(host, "ok")
    while selected(page(host)) != label:
        page(host, "down")
    return next(r for r in page(host)["rows"] if r["label"] == label)


def gains(host):
    values = read_fields(host, GAINS)
    assert values is not None, "the gains did not read back"
    return tuple(values[name] for name in GAINS)


def settles(host, want):
    """The gains once the queued letter has reached handleType2Command()."""
    return wait_for(lambda: gains(host) == want and want, timeout=5.0)


@pytest.fixture
def closed(host):
    yield
    page(host, "exit")


@pytest.fixture
def unsharpened(host):
    """Sharpness off at both ends, whatever it was when the run started."""
    if row(host, SHARPNESS)["value"] == "ON":
        page(host, "ok")
    yield
    if row(host, SHARPNESS)["value"] == "ON":
        page(host, "ok")
    page(host, "exit")


def test_the_row_reports_the_preference_and_the_gains_agree_with_it(
        host, source, preset_save, unsharpened):
    assert row(host, SHARPNESS)["value"] == "OFF"
    assert gains(host) in (RESTING_1080P, RESTING_OTHER)

    page(host, "ok")
    assert settles(host, SHARPENED) == SHARPENED, "Ok did not raise the gains"
    assert row(host, SHARPNESS)["value"] == "ON"


def test_sharpness_does_not_move_the_peaking_row(
        host, source, preset_save, unsharpened):
    # The chain's Ok wrote VDS_PK_Y_H_BYPS too, so pressing Sharpness left the
    # Peaking row showing a value the user had not set.
    before = row(host, "Peaking")["value"]

    row(host, SHARPNESS)
    page(host, "ok")
    assert settles(host, SHARPENED) == SHARPENED

    assert row(host, "Peaking")["value"] == before


def raster_height(host):
    """The output vertical total, which says which resolution is loaded."""
    total = read_named(host, "VDS_VSYNC_RST")
    assert total is not None, "the output raster did not read back"
    return total


def loads(host, letter, away_from):
    """The output raster once the preset the letter asks for has landed."""
    get_json(host, "/uc?" + letter)

    def moved():
        now = raster_height(host)
        return now if now != away_from else None

    return wait_for(moved, timeout=30.0)


def test_sharpness_survives_an_output_resolution_change(
        host, source, preset_save, preset_load, unsharpened):
    # The gains are rewritten on every output change, which is what lost the
    # chain's setting. applyOutputResolutionSettings() asks the preference now.
    row(host, SHARPNESS)
    page(host, "ok")
    assert settles(host, SHARPENED) == SHARPENED

    at1080p = raster_height(host)
    at720p = loads(host, "g", at1080p)
    assert at720p is not None, "the output never left 1080p"
    assert gains(host) == SHARPENED, "the output change dropped the sharpening"

    assert loads(host, "s", at720p) == at1080p
    assert gains(host) == SHARPENED
