"""The menu's R, G, B and Y gain rows, against a live unit.

The balance is held by `Tv5725::ColourBalance` in the basis the rows show, and
written as the three YUV offsets and the luma gain. The chain derived it by
reading those registers back, which lost a step to rounding every time and lost
the lot whenever a colour space change rewrote them -- so what only the unit can
show is that a press reaches the offsets, that the arithmetic lands where the
host suite says, and that the set survives the boot it never used to.
docs/osd-menu.md

Reversible by construction: each test ends on Default colour, which is the
chain's own way back to neutral.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get, get_json, read_fields, read_settings, wait_for

PICTURE_SETTINGS = "Picture Settings"

OFFSETS = ["VDS_Y_OFST", "VDS_U_OFST", "VDS_V_OFST"]
NEUTRAL = 128


def page(host, key=None):
    status, body = get_json(host, "/menu" + (f"?key={key}" if key else ""))
    if status == 404:
        pytest.skip("/menu is behind GBS_DEBUG; this build has no route")
    assert status == 200, f"/menu answered {status}"
    return body


def selected(body):
    return next(row["label"] for row in body["rows"] if row["selected"])


def on_row(host, label):
    """The cursor on `label` under Picture Settings, from wherever it is."""
    page(host, "exit")
    page(host, "menu")
    while selected(page(host)) != PICTURE_SETTINGS:
        page(host, "down")
    page(host, "ok")
    while selected(page(host)) != label:
        page(host, "down")


def shown(host, label):
    return int(next(r for r in page(host)["rows"] if r["label"] == label)["value"])


def offsets(host):
    """The three offsets as the signed bytes the chip takes them for, or None.

    None where the read did not come back: /getfields is answered inside loop(),
    so a unit that is still booting or inside a long search has nothing to say
    yet.
    """
    values = read_fields(host, OFFSETS)
    if values is None:
        return None
    return tuple(v - 256 if v > 127 else v for v in (values[n] for n in OFFSETS))


def step_to(host, label, want):
    """Press until the row reads `want`.

    One press is queued for loop() at a time, so presses sent faster than it
    consumes them coalesce -- 20 rapid ones landed 4. The row is what says how
    far it actually got, so this presses until it arrives rather than counting.
    """
    key = "right" if want > shown(host, label) else "left"

    def arrived():
        if shown(host, label) == want:
            return True
        page(host, key)
        return None

    return wait_for(arrived, timeout=30.0, interval=0.1)


@pytest.fixture
def neutral(host):
    """Default colour at both ends, which is the chain's own way back."""
    get(host, "/uc?U", timeout=8)
    yield
    get(host, "/uc?U", timeout=8)
    page(host, "exit")


def test_red_lifts_luma_and_v_and_drops_u(host, source, preset_save, neutral):
    # The matrix the sketch used to mix by hand: per unit of red, Y 0.299,
    # U -0.169, V 0.500.
    on_row(host, "R")
    assert offsets(host) == (0, 0, 0), "an RGB source does not rest at zero"

    # A row names the value it moves rather than carrying a letter.
    pressed = page(host, "right")
    assert pressed["queue"] == "tune", f"Right asked for {pressed['queue']!r}"
    assert pressed["asked"] == "red+"

    assert step_to(host, "R", NEUTRAL + 20), "the row never reached 148"
    assert wait_for(lambda: offsets(host) == (5, -3, 10) or None, timeout=5.0)


def test_each_row_moves_its_own_colour(host, source, preset_save, neutral):
    on_row(host, "G")
    assert step_to(host, "G", NEUTRAL + 20)
    assert wait_for(lambda: offsets(host) == (11, -6, -8) or None, timeout=5.0)

    assert shown(host, "R") == NEUTRAL, "green moved the red row"
    assert shown(host, "B") == NEUTRAL


def test_the_luma_gain_steps_its_own_register(host, source, preset_save, neutral):
    on_row(host, "Y gain")
    assert step_to(host, "Y gain", NEUTRAL + 16)

    def raised():
        values = read_fields(host, ["VDS_Y_GAIN"])
        return values is not None and values["VDS_Y_GAIN"] == 0x90 or None

    assert wait_for(raised, timeout=5.0)
    assert offsets(host) == (0, 0, 0), "the gain moved an offset"


def test_default_colour_returns_every_row_to_neutral(
        host, source, preset_save, neutral):
    on_row(host, "B")
    assert step_to(host, "B", NEUTRAL - 20)
    assert wait_for(lambda: offsets(host) != (0, 0, 0) or None, timeout=5.0)

    get(host, "/uc?U", timeout=8)
    assert wait_for(lambda: offsets(host) == (0, 0, 0) or None, timeout=5.0)
    assert shown(host, "B") == NEUTRAL


BALANCE_KEYS = ("colour-red", "colour-green", "colour-blue", "luma-gain")


def stored(host):
    """The four balance values as the settings file holds them."""
    values = read_settings(host)
    if values is None or not all(key in values for key in BALANCE_KEYS):
        return None
    return tuple(int(values[key]) for key in BALANCE_KEYS)


def test_ok_keeps_the_balance_and_left_and_right_do_not(
        host, source, preset_save, neutral):
    # Left and Right are held keys: a save per step would write flash a hundred
    # times for one adjustment.
    on_row(host, "R")
    assert step_to(host, "R", NEUTRAL + 14)
    assert stored(host) == (NEUTRAL, NEUTRAL, NEUTRAL, NEUTRAL), \
        "a step wrote the file"

    page(host, "ok")
    assert wait_for(
        lambda: stored(host) == (NEUTRAL + 14, NEUTRAL, NEUTRAL, NEUTRAL) or None,
        timeout=10.0), "Ok did not keep the balance"


@pytest.mark.reboot
def test_a_kept_balance_comes_back_after_a_restart(
        host, source, preset_save, neutral):
    # The chain's Ok saved the preferences and the balance was not in them, so a
    # colour setting never survived a boot.
    on_row(host, "R")
    assert step_to(host, "R", NEUTRAL + 14)
    page(host, "ok")

    # BEFORE the restart. /uc? holds one queued letter, so asking for the reset
    # while the save is still pending drops the save -- and the test then proves
    # nothing about the boot.
    kept = (NEUTRAL + 14, NEUTRAL, NEUTRAL, NEUTRAL)
    assert wait_for(lambda: stored(host) == kept or None, timeout=10.0), \
        "Ok did not keep the balance"

    get(host, "/uc?a", timeout=8)
    assert wait_for(lambda: get(host, "/geometry", timeout=1)[0] != 200 or None,
                    timeout=30.0), "the unit never went away"
    assert wait_for(lambda: get(host, "/geometry", timeout=2)[0] == 200 or None,
                    timeout=60.0), "the unit never came back"

    assert wait_for(lambda: offsets(host) == (4, -2, 7) or None, timeout=30.0)

    # The menu reopens at the top, so the row has to be walked back to.
    on_row(host, "R")
    assert shown(host, "R") == NEUTRAL + 14
