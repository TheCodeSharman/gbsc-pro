"""Which tree holds the panel, against a live unit.

Two trees draw the 128x64 panel: the described menu through `Osd::Panel`, and
the icon tree behind the rotary encoder. Nothing on the board reports what the
panel shows, so what the unit can answer is the gate -- whether the icon tree's
tick() is suppressed while a described page is on the panel. A regression there
is two trees painting over each other, which no host test can see.
docs/osd-menu.md
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get_json, wait_for


@pytest.fixture(autouse=True)
def route(host):
    """The route, waited for rather than skipped on: a unit still coming back
    from a flash answers 404 for a few seconds, and a skip taken then reads as a
    pass."""
    def answers():
        status, _ = get_json(host, "/menu")
        return status == 200

    if not wait_for(answers, timeout=20.0):
        pytest.skip("/menu is behind GBS_DEBUG; this build has no route")


def page(host, key=None):
    status, body = get_json(host, "/menu" + (f"?key={key}" if key else ""))
    assert status == 200, f"/menu answered {status}"
    return body


def holder(host):
    return page(host).get("panel")


def settles_to(host, want):
    """Which tree holds the panel once loop() has set the gate. The press is
    queued, so the answer to the request that made it still shows the old
    holder."""
    if wait_for(lambda: holder(host) == want, timeout=5.0):
        return want
    return holder(host)


@pytest.fixture
def closed(host):
    yield
    page(host, "exit")
    settles_to(host, "icons")


def test_the_icon_tree_holds_the_panel_while_the_menu_is_closed(host, closed):
    page(host, "exit")

    assert settles_to(host, "icons") == "icons"


def test_an_open_described_page_takes_the_panel_from_the_icon_tree(host, closed):
    page(host, "exit")
    assert settles_to(host, "icons") == "icons"

    page(host, "menu")

    assert settles_to(host, "described") == "described"


def test_leaving_the_menu_gives_the_panel_back(host, closed):
    page(host, "menu")
    assert settles_to(host, "described") == "described"

    page(host, "exit")

    assert settles_to(host, "icons") == "icons"
