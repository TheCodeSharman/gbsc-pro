"""The menu's Sv-Av InPutSet level, against a live unit.

The AV module's own picture: the decoder's standard, the ADV7391's line doubling
and smoothing, its three picture controls, and the compatibility preference the
RGB inputs share. That whole path is WRITE-ONLY -- the HC32's USART4 TX goes to
the update button rather than back to the ESP -- so no register read can confirm
any of it. What a test can show is that the press reaches the surface it names,
that the held value moves, that the gate on Smooth holds, and that what is kept
lands on flash. docs/osd-menu.md

Reversible by construction: each test puts the value back, and Default is the
level's own way back for the three picture controls.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import (MENU_ROWS, get, get_json, menu_seek, menu_selected, setting,
                      wait_for)

SYSTEM_SETTINGS = "System Settings"
SV_AV = "Sv-Av InPutSet"

def page(host, key=None):
    status, body = get_json(host, "/menu" + (f"?key={key}" if key else ""))
    if status == 404:
        pytest.skip("/menu is behind GBS_DEBUG; this build has no route")
    assert status == 200, f"/menu answered {status}"
    return body


def selected(body):
    return menu_selected(body)


def seek(host, label):
    """The cursor on `label`, whichever way round the level it lies. The walk
    is gbs_unit's, which goes to the top of the level first because the cursor
    does not wrap -- this file's own teardown presses Default, row 7 of 8."""
    got = menu_seek(lambda key=None: page(host, key), label)
    assert got, str(got)


def open_level(host):
    """Into Sv-Av InPutSet from wherever the menu is, by the keys a remote sends."""
    page(host, "exit")
    page(host, "menu")
    seek(host, SYSTEM_SETTINGS)
    page(host, "ok")
    seek(host, SV_AV)
    body = page(host, "ok")
    assert body["depth"] == 3, "Ok did not descend into the Sv-Av level"
    return body


def shown(host, label):
    """A row's value, from wherever the cursor is in this level.

    **THE CURSOR DOES NOT WRAP, SO THE WALK GOES UP FIRST.** The page draws
    three rows about the cursor, so a row is only readable once the cursor is
    near it -- and walking down alone cannot reach one ABOVE. Default is row 7
    of this level's 8 and the teardown and Default cases both leave the cursor
    there, so Bright on row 4 was unreachable and read as absent from the level.
    """
    for _ in range(MENU_ROWS):
        at = menu_selected(page(host))
        if menu_selected(page(host, "up")) == at:
            break

    for _ in range(MENU_ROWS):
        for row in page(host)["rows"]:
            if row["label"] == label:
                return row["value"]
        page(host, "down")
    raise AssertionError(f"{label} is not a row of this level")


def press_until(host, label, want, key, timeout=30.0, interval=0.1):
    """Press until the row reads `want`.

    **ONE PRESS IS QUEUED AT A TIME**, so presses sent faster than loop()
    consumes them coalesce -- and a press whose ACT takes seconds needs an
    interval to match. A row that merely steps a value is consumed within a
    loop pass; Compatibility re-applies the preset, and hammering a TOGGLE
    every 100 ms across one flips it an unknown number of times and lands
    wherever the last consumed press left it.
    """
    seek(host, label)

    def arrived():
        if shown(host, label) == want:
            return True
        page(host, key)
        return None

    return wait_for(arrived, timeout=timeout, interval=interval)


def brightness_on_flash(host):
    stored = setting(host, "av-brightness")
    return None if stored is None else int(stored)


@pytest.fixture
def level(host):
    """The level open at both ends, and its picture controls back to default."""
    open_level(host)
    yield
    seek(host, "Default")
    page(host, "ok")
    page(host, "exit")


def test_the_level_draws_the_chains_eight_rows(host, preset_save, level):
    labels = []
    for _ in range(3):
        labels += [row["label"] for row in page(host)["rows"]]
        page(host, "down")
        page(host, "down")
        page(host, "down")

    assert labels == [
        "Format", "DoubleLine", "Smooth",
        "Bright", "Contrast", "Saturation",
        "Default", "Compatibility",
    ]


def test_a_picture_control_names_the_value_it_steps(host, preset_save, level):
    seek(host, "Bright")
    pressed = page(host, "right")
    assert pressed["queue"] == "tune", f"Right asked for {pressed['queue']!r}"
    assert pressed["asked"] == "bright+"

    assert press_until(host, "Bright", "138", "right"), "Bright never reached 138"
    assert press_until(host, "Bright", "128", "left")


def test_default_returns_the_three_picture_controls_and_keeps_them(
        host, preset_save, level):
    assert press_until(host, "Bright", "140", "right")

    seek(host, "Default")
    pressed = page(host, "ok")
    assert pressed["queue"] == "uc", "Default is a single press, so it is a letter"

    assert wait_for(lambda: shown(host, "Bright") == "128" or None, timeout=5.0)
    assert wait_for(lambda: brightness_on_flash(host) == 128 or None, timeout=10.0)


def test_smoothing_needs_the_doubler_it_smooths(host, preset_save, level):
    # Smoothing is a property of the doubled line, so the row does nothing while
    # the doubler is out -- which is the chain's gate too.
    #
    # SET rather than asserted: the row is a toggle and the level's own teardown
    # does not reach it, so a case that ended with the doubler in left this
    # failing on a precondition it never established.
    assert press_until(host, "DoubleLine", "1X", "ok"), \
        "the doubler would not come out, so Smooth has no gate to test"

    seek(host, "Smooth")
    before = shown(host, "Smooth")
    page(host, "ok")
    assert wait_for(lambda: shown(host, "Smooth") != before or None, timeout=3.0) \
        is None, "Smooth moved with the doubler out"

    seek(host, "DoubleLine")
    page(host, "ok")
    assert wait_for(lambda: shown(host, "DoubleLine") == "2X" or None, timeout=5.0)

    seek(host, "Smooth")
    page(host, "ok")
    assert wait_for(lambda: shown(host, "Smooth") != before or None, timeout=5.0)

    page(host, "ok")
    assert wait_for(lambda: shown(host, "Smooth") == before or None, timeout=5.0)
    seek(host, "DoubleLine")
    page(host, "ok")
    assert wait_for(lambda: shown(host, "DoubleLine") == "1X" or None, timeout=5.0)


def test_the_format_row_rings_round_the_standards(host, preset_save, level):
    seek(host, "Format")
    assert shown(host, "Format") == "Auto"

    pressed = page(host, "right")
    assert pressed["queue"] == "tune"
    assert pressed["asked"] == "format+"

    assert press_until(host, "Format", "NTSC-M", "right")

    # A ring: one step back from the first entry is the last.
    assert press_until(host, "Format", "Auto", "left")
    page(host, "left")
    assert wait_for(lambda: shown(host, "Format") == "SECAM" or None, timeout=5.0)
    assert press_until(host, "Format", "Auto", "right")


def acquired(host):
    status, body = get_json(host, "/geometry")
    return status == 200 and body is not None and body["state"] == "acquired"


def test_compatibility_toggles_and_reloads(host, source, preset_save, preset_load,
                                           level):
    # The preference the RGB inputs share, so changing it re-applies the preset
    # on an RGB source -- and a press made while that load runs waits behind it,
    # which is why the way back presses until the row moves.
    seek(host, "Compatibility")
    before = shown(host, "Compatibility")
    after = "ON" if before == "OFF" else "OFF"

    pressed = page(host, "ok")
    assert pressed["queue"] == "uc"
    # A PRESS A PRESET LOAD APART. The act takes seconds, so a toggle pressed
    # every 100 ms flips an unknown number of times and lands wherever the last
    # consumed press left it.
    assert press_until(host, "Compatibility", after, "ok",
                       timeout=90.0, interval=3.0)
    assert wait_for(lambda: acquired(host) or None, timeout=60.0), \
        "the source never came back"

    assert press_until(host, "Compatibility", before, "ok",
                       timeout=90.0, interval=3.0)
    assert wait_for(lambda: acquired(host) or None, timeout=60.0)


def test_nothing_here_needs_a_source(host, preset_save):
    # The AV module's own picture is not the scaler's, so the level reports and
    # acts with nothing on the input.
    assert get(host, "/menu")[0] == 200
