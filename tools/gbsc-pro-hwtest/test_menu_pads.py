"""The menu's Move and Scale pads, against a live unit.

A pad is the one menu item that reaches the picture rather than a letter: Ok
hands the four arrows to the geometry engine and Menu takes them back. What the
host suite cannot show is that the press arrives -- the nudge is queued for
`loop()` and acted on by `Tv5725::Controls`, so only the unit says whether the
capture moved. docs/osd-menu.md

Reversible by construction: each test presses the opposite arrow and asserts the
framing came back, so a run leaves the picture where it found it.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get_json, wait_for

SCREEN_SETTINGS = "Screen Settings"


def page(host, key=None):
    status, body = get_json(host, "/menu" + (f"?key={key}" if key else ""))
    if status == 404:
        pytest.skip("/menu is behind GBS_DEBUG; this build has no route")
    assert status == 200, f"/menu answered {status}"
    return body


def labels(body):
    return [row["label"] for row in body["rows"]]


def selected(body):
    return next(row["label"] for row in body["rows"] if row["selected"])


def open_pad(host, which):
    """From wherever the menu is, into `which`'s pad, by the keys a remote sends."""
    page(host, "exit")
    page(host, "menu")
    while selected(page(host)) != SCREEN_SETTINGS:
        page(host, "down")
    body = page(host, "ok")
    assert "Move" in labels(body) and "Scale" in labels(body)
    while selected(page(host)) != which:
        page(host, "down")
    body = page(host, "ok")
    assert body["adjusting"], f"Ok on {which} did not take the arrows"
    return body


def framing(host):
    status, body = get_json(host, "/geometry")
    assert status == 200
    return body["oh"], body["ov"], body["eh"], body["ev"]


def settles(host, away_from):
    """The framing once the queued nudge has reached the engine."""
    def moved():
        now = framing(host)
        return now if now != away_from else None

    return wait_for(moved, timeout=5.0)


@pytest.fixture
def closed(host):
    yield
    page(host, "exit")


def test_a_pad_takes_the_arrows_and_gives_them_back(host, source, closed):
    open_pad(host, "Move")

    body = page(host, "menu")
    assert not body["adjusting"]
    assert "Move" in labels(body), "Menu left the level as well as the pad"


@pytest.mark.pan
def test_a_move_arrow_pans_the_capture_and_the_opposite_one_puts_it_back(
        host, source, closed):
    open_pad(host, "Move")
    before = framing(host)

    body = page(host, "left")
    assert body["queue"] == "nudge", f"Left asked for {body['queue']!r}"
    assert body["asked"] == "hpan+"
    panned = settles(host, before)
    assert panned is not None, "the capture did not move"
    assert panned[0] > before[0], f"{panned} is not right of {before}"
    assert panned[1:] == before[1:], "a horizontal pan moved something else"

    page(host, "right")
    assert settles(host, panned) == before


@pytest.mark.zoom
def test_a_scale_arrow_crops_the_capture_and_the_opposite_one_puts_it_back(
        host, source, closed):
    open_pad(host, "Scale")
    before = framing(host)

    body = page(host, "right")
    assert body["queue"] == "nudge"
    assert body["asked"] == "hzoom+"
    cropped = settles(host, before)
    assert cropped is not None, "the capture did not move"
    assert cropped[2] < before[2], f"{cropped} is no narrower than {before}"

    page(host, "left")
    assert settles(host, cropped) == before


def shape(host):
    status, body = get_json(host, "/geometry")
    assert status == 200
    return body["aspect"]


def walk_to(host, label):
    """The cursor on `label` of the level it is in, whichever page holds it."""
    for _ in range(12):
        if selected(page(host)) == label:
            return
        page(host, "down")
    raise AssertionError(f"{label} is not on this level")


@pytest.mark.pan
def test_reset_puts_the_framing_and_the_shape_back(host, source, closed):
    """Both are stored against the source, so both are what the row undoes."""
    open_pad(host, "Move")
    default = framing(host)
    page(host, "left")
    panned = settles(host, default)
    assert panned is not None, "the capture did not move"
    page(host, "menu")

    walk_to(host, "Aspect")
    defaulted = shape(host)
    page(host, "ok")
    assert wait_for(lambda: shape(host) != defaulted, timeout=5.0), \
        "Ok on Aspect did not change the shape"

    walk_to(host, "Reset")
    page(host, "ok")

    assert wait_for(lambda: framing(host) == default, timeout=5.0), \
        f"the framing stayed at {framing(host)} rather than {default}"
    assert shape(host) == defaulted
