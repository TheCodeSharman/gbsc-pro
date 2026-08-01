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
        "--preset-save",
        action="store_true",
        default=False,
        help="also run the destructive preset-save test: it overwrites the stored "
        "preset for the current video mode and switches the unit to custom "
        "presets. Have a flash backup.",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "preset_save: writes to the unit's flash; needs --preset-save"
    )


def pytest_collection_modifyitems(config, items):
    if config.getoption("--preset-save"):
        return
    skip = pytest.mark.skip(reason="destructive; pass --preset-save to run it")
    for item in items:
        if "preset_save" in item.keywords:
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
    """The one permitted WebSocket client, held open for the whole session.

    A second connection crashes the ESP, so close the web UI before running.
    """
    try:
        connection = Console(host)
    except Exception as e:  # noqa: BLE001 - any failure here is just "no console"
        pytest.skip(f"no WebSocket console on {host}: {e}")
    yield connection
    connection.close()


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
