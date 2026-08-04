"""Fixtures for the live-unit tests. Without --host (or GBSC_HOST) every test
here skips, so a bare `pytest` at the repo root stays useful with no hardware."""

import os

import pytest

from gbs_unit import Console, get, read_reg, write_reg


def pytest_addoption(parser):
    group = parser.getgroup("gbsc-pro")
    group.addoption(
        "--host",
        action="store",
        default=os.environ.get("GBSC_HOST"),
        help="running unit to test against, e.g. gbscontrol.local or 192.168.1.20. "
        "Defaults to $GBSC_HOST; without either, the hardware tests skip.",
    )
    group.addoption(
        "--source",
        action="store_true",
        default=False,
        help="a video source is connected and expected to lock, so run the sync "
        "tests. Without it they skip: an unplugged input and a firmware that "
        "cannot see the input look identical from over here.",
    )
    group.addoption(
        "--preset-save",
        action="store_true",
        default=False,
        help="also run the destructive preset-save test: it overwrites the stored "
        "preset for the current video mode and switches the unit to custom "
        "presets. Have a flash backup.",
    )
    group.addoption(
        "--no-sync",
        action="store_true",
        default=False,
        help="the source is DISCONNECTED and the unit has no lock, so run the "
        "tests about surviving that. They are the opposite of --source and "
        "cannot both be true.",
    )
    group.addoption(
        "--freeze",
        action="store_true",
        default=False,
        help="run the freeze test that proves a frozen firmware stops writing "
        "TV5725 registers. Needs a build with /freeze support; opt-in because a "
        "failure means a preset load corrupted the picture.",
    )
    group.addoption(
        "--pllad-hostile",
        action="store_true",
        default=False,
        help="drive PLLAD_MD to values known to break sync, to prove the unit "
        "stays reachable. Corrupts the picture for about half a minute and puts "
        "the divider back afterwards; it writes no flash. Opt-in because it is "
        "disruptive to watch, not because it is dangerous.",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "preset_save: writes to the unit's flash; needs --preset-save"
    )
    config.addinivalue_line(
        "markers", "no_sync: needs the source disconnected; needs --no-sync"
    )
    config.addinivalue_line(
        "markers", "pllad_hostile: corrupts the picture while it runs; needs --pllad-hostile"
    )
    config.addinivalue_line(
        "markers", "freeze: needs a build with /freeze support; needs --freeze"
    )


def pytest_collection_modifyitems(config, items):
    gates = [
        ("preset_save", "--preset-save", "destructive; pass --preset-save to run it"),
        ("no_sync", "--no-sync", "needs the source unplugged; pass --no-sync to run it"),
        ("pllad_hostile", "--pllad-hostile",
         "corrupts the picture while it runs; pass --pllad-hostile to run it"),
        ("freeze", "--freeze",
         "needs a build with /freeze support; pass --freeze to run it"),
    ]
    for keyword, option, reason in gates:
        if config.getoption(option):
            continue
        skip = pytest.mark.skip(reason=reason)
        for item in items:
            if keyword in item.keywords:
                item.add_marker(skip)


@pytest.fixture(scope="session")
def host(request):
    """The unit's address, once it has answered. Skips the run if it has not."""
    address = request.config.getoption("--host")
    if not address:
        pytest.skip("needs a running unit: pass --host, or set GBSC_HOST")
    status, body = get(address, "/wifi/status")
    if status != 200:
        pytest.skip(f"{address} did not answer /wifi/status (status {status}: {body})")
    return address


@pytest.fixture(scope="session")
def console(host):
    """A WebSocket console client, held open for the whole session.

    Not "the one permitted client": the server caps at 5
    (3rdparty/WebSockets/src/WebSocketsServer.h:31) and nothing crashes. The
    old claim came from SerialMirror hanging up on *every* client whenever heap
    dipped, which read from outside as a one-client limit. Still worth closing
    the web UI for a long capture, because it costs heap -- but two clients are
    not forbidden.
    """
    try:
        connection = Console(host)
    except Exception as e:  # noqa: BLE001 - any failure here is just "no console"
        pytest.skip(f"no WebSocket console on {host}: {e}")
    yield connection
    connection.close()


@pytest.fixture
def source(request):
    """Asserts the operator's promise that a source is plugged in and should be
    locking. Skips the sync tests without it, so a bench unit with nothing on its
    input does not report the no-sync fault it does not have."""
    if not request.config.getoption("--source"):
        pytest.skip("needs a connected source: pass --source")


@pytest.fixture
def register_guard(host):
    """Note a register's value, and put it back when the test ends — including
    when the test fails part way through, which is when it matters."""
    saved = []

    def guard(segment, register):
        value = read_reg(host, segment, register)
        assert value is not None, f"could not read segment {segment} register {register:#04x}"
        saved.append((segment, register, value))
        return value

    yield guard

    for segment, register, value in reversed(saved):
        write_reg(host, segment, register, value)
