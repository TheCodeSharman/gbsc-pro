"""Screen Settings against a live unit, on both output paths.

Pass-through hands the source's own timing to the encoder, so the scaler is out
of the circuit and Move, Scale and Aspect have nothing to act on. The engine
already refuses all three there and no register distinguishes that refusal from
the press never arriving -- which is what made them read as broken controls.

What only the unit can show is that the rows follow the path the picture is
actually taking, and that a press on one reaches nothing: the host suite proves
the tree and the drawing, and neither can see the engine change route.
docs/osd-menu.md, docs/rgbhv-bypass-trap.md

Needs a source bypass can carry -- the bench 15 kHz one cannot reach the sink --
so it drives the SOURCE to 800x600@60 and puts the bench mode back however it
ends. Leaves the unit on the scaling path.
"""

import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit
from gbs_unit import acquired_and_settled, get, get_json, mode_serv, modeserv_ok

SCREEN_SETTINGS = "Screen Settings"
TRANSFORMS = ("Move", "Scale", "Aspect")

BYPASSABLE_MODE = "X800 Y600 C256 F60"
BENCH_MODE = "X320 Y256 C256 F50"

# /sc?K is queued for loop() and the route only says it was understood, and the
# switch itself takes the sync pad away and gives it back.
PATH_CHANGE_S = 40.0

# A press the engine refuses leaves nothing to wait for, so the only honest
# check is that the held value stays put across the window a press would land
# in. Several reads rather than one, since loop() consumes the letter on its own
# schedule.
REFUSAL_WINDOW_S = 8.0


def page(host, key=None):
    status, body = get_json(host, "/menu" + (f"?key={key}" if key else ""))
    if status == 404:
        pytest.skip("/menu is behind GBS_DEBUG; this build has no route")
    assert status == 200, f"/menu answered {status}"
    return body


def selected(body):
    return next(row["label"] for row in body["rows"] if row["selected"])


def on_screen_settings(host):
    """The cursor on the first row of Screen Settings, from wherever it is."""
    page(host, "exit")
    page(host, "menu")
    while selected(page(host)) != SCREEN_SETTINGS:
        page(host, "down")
    page(host, "ok")
    return page(host)


def availability(body):
    return {row["label"]: row.get("available") for row in body["rows"]}


def passing_through(host):
    body = get_json(host, "/geometry")[1]
    assert body is not None, "/geometry did not answer"
    return body


@pytest.fixture
def bypassable_source(request, host, source):
    """The source on a mode the display can show passed through, and the unit on
    the SCALING path, which is where it is left too."""
    where = request.config.getoption("--modeserv")
    assert modeserv_ok(mode_serv(where, f"MODE {BYPASSABLE_MODE}")), \
        f"the source refused {BYPASSABLE_MODE}"
    assert acquired_and_settled(host), f"{BYPASSABLE_MODE} never settled"
    if is_passing_through(host):
        toggle_pass_through(host, False)
    try:
        yield
    finally:
        if is_passing_through(host):
            toggle_pass_through(host, False)
        mode_serv(where, f"MODE {BENCH_MODE}")
        gbs_unit.show_card(where)
        acquired_and_settled(host)


def is_passing_through(host):
    """Read off the row that reports it, which is the one place the firmware
    says which route the picture is taking."""
    page(host, "exit")
    page(host, "menu")
    while selected(page(host)) != "Output Resolution":
        page(host, "down")
    page(host, "ok")
    while selected(page(host)) != "Pass Through":
        page(host, "down")
    value = next(r for r in page(host)["rows"] if r["label"] == "Pass Through")["value"]
    assert value != "N/A", "this source cannot be passed through at all"
    return value == "ON"


def toggle_pass_through(host, want):
    """Flip the route and wait for the row that reports it to agree.

    The row rather than a fixed wait: /sc?K is queued for loop(), the switch
    takes the sync pad away and gives it back, and how long that takes is the
    engine's business.
    """
    assert get(host, "/sc?K")[0] == 200, "/sc?K was not accepted"
    reached = gbs_unit.wait_for(
        lambda: is_passing_through(host) == want, PATH_CHANGE_S, 1.0)
    assert reached, f"the unit never reached pass-through {'ON' if want else 'OFF'}"
    assert acquired_and_settled(host), "the unit never settled after the path change"


@pytest.mark.source_mode
def test_the_picture_transforms_are_live_while_the_scaler_is_in_the_path(
        host, bypassable_source):
    live = availability(on_screen_settings(host))

    for row in TRANSFORMS:
        assert live.get(row) is True, f"{row} is not available on the scaling path"
    assert live.get("Reset") is not False, "Reset is never unavailable"


@pytest.mark.source_mode
def test_pass_through_greys_the_three_rows_the_scaler_would_serve(
        host, bypassable_source):
    toggle_pass_through(host, True)

    greyed = availability(on_screen_settings(host))
    for row in TRANSFORMS:
        assert greyed.get(row) is False, f"{row} is still live in pass-through"

    # Reset puts the stored framing and shape back, and both outlive the route
    # the picture is currently taking.
    page(host, "down")
    page(host, "down")
    page(host, "down")
    assert selected(page(host)) == "Reset"
    assert availability(page(host)).get("Reset") is True


@pytest.mark.source_mode
def test_a_press_on_a_greyed_row_asks_for_nothing(host, bypassable_source):
    toggle_pass_through(host, True)

    on_screen_settings(host)
    assert selected(page(host)) == "Move"

    # Ok on a pad hands it the four arrows. Refused, the cursor keeps them, so
    # Up moves the cursor instead of asking the picture for a granule it cannot
    # have -- and nothing is queued on any surface.
    after_ok = page(host, "ok")
    assert after_ok["adjusting"] is False, "a greyed pad took the arrows"
    assert after_ok["queue"] == "", f"Ok queued {after_ok['queue']!r}"

    after_up = page(host, "up")
    assert after_up["queue"] == "", f"Up queued {after_up['queue']!r}"

    while selected(page(host)) != "Aspect":
        page(host, "down")
    for key in ("ok", "left", "right"):
        asked = page(host, key)
        assert asked["queue"] == "", f"{key} queued {asked['queue']!r}"


@pytest.mark.source_mode
def test_the_shape_the_engine_holds_does_not_move_in_pass_through(
        host, bypassable_source):
    """The row is greyed because the engine refuses the shape, not the other way
    round: the letter behind it is /uc?G, which the web and the console still
    send."""
    toggle_pass_through(host, True)

    before = passing_through(host)["aspect"]
    assert get(host, "/uc?G")[0] == 200
    deadline = time.monotonic() + REFUSAL_WINDOW_S
    while time.monotonic() < deadline:
        assert passing_through(host)["aspect"] == before, \
            "the shape moved while the scaler was out of the path"
        time.sleep(1.0)
