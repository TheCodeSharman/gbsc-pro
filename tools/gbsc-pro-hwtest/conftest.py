"""Fixtures for the live-unit tests. Without --host (or GBSC_HOST) every test
here skips, so a bare `pytest` at the repo root stays useful with no hardware."""

import os

import pytest

from gbs_unit import Console, get, read_reg, reset_framing, write_reg


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
        "--preset-load",
        action="store_true",
        default=False,
        help="run the tests that force a preset load to prove the geometry is "
        "recomputed from scratch afterwards. They leave the unit in the preset "
        "they loaded (pal_768x576), because restoring the output raster is the "
        "one thing the geometry guard must not do, so the output mode is wrong "
        "until the next real mode change or a power cycle. Writes no flash.",
    )
    group.addoption(
        "--preset-save",
        action="store_true",
        default=False,
        help="run the tests that write flash -- the framing table, saved to "
        "/framing.txt when a tuning settles. Opt-in because it spends flash "
        "write cycles and leaves a file on the unit, not because it is risky: "
        "the framing it stores is the one the user tuned.",
    )
    group.addoption(
        "--reboot",
        action="store_true",
        default=False,
        help="restart the unit, to prove the boot restore brings the saved input "
        "back. Costs a few seconds of black screen and a re-detection; writes no "
        "flash. Opt-in because it takes the unit away mid-run.",
    )
    group.addoption(
        "--modeserv",
        action="store",
        default=os.environ.get("GBSC_MODESERV"),
        help="the ModeServ host driving the SOURCE, e.g. 192.168.88.10. Tests "
        "that need a particular source mode change it there and put it back; "
        "without this they skip, because nothing else can put a VESA raster on "
        "the input.",
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
        "markers", "no_sync: needs the source disconnected; needs --no-sync"
    )
    config.addinivalue_line(
        "markers", "pllad_hostile: corrupts the picture while it runs; needs --pllad-hostile"
    )
    config.addinivalue_line(
        "markers", "freeze: needs a build with /freeze support; needs --freeze"
    )
    config.addinivalue_line(
        "markers", "reboot: restarts the unit while it runs; needs --reboot"
    )
    config.addinivalue_line(
        "markers", "source_mode: changes the SOURCE mode while it runs; needs --modeserv"
    )
    # SELECTORS, not gates -- absent from the `gates` list below, so they never
    # skip anything. Assigned by which pad the test presses, because -k matches
    # words in the NAME: -k "pan" also catches a zoom test that "moves" the
    # capture, which is the opposite of the split you wanted.
    #   pytest ... -m pan     the pan and OSD-move pads
    #   pytest ... -m zoom    the zoom pads
    config.addinivalue_line(
        "markers", "pan: presses a pan or move pad; selector only, gates nothing"
    )
    config.addinivalue_line(
        "markers", "zoom: presses a zoom pad; selector only, gates nothing"
    )


def pytest_collection_modifyitems(config, items):
    gates = [
        ("no_sync", "--no-sync", "needs the source unplugged; pass --no-sync to run it"),
        ("pllad_hostile", "--pllad-hostile",
         "corrupts the picture while it runs; pass --pllad-hostile to run it"),
        ("freeze", "--freeze",
         "needs a build with /freeze support; pass --freeze to run it"),
        ("reboot", "--reboot",
         "restarts the unit while it runs; pass --reboot to run it"),
        ("source_mode", "--modeserv",
         "changes the source mode; pass --modeserv <host> to run it"),
    ]
    for keyword, option, reason in gates:
        if config.getoption(option):
            continue
        skip = pytest.mark.skip(reason=reason)
        for item in items:
            if keyword in item.keywords:
                item.add_marker(skip)


@pytest.fixture(scope="session", autouse=True)
def leave_the_bench_usable(request):
    """Put the framing back when the run ends, however it ends.

    Every register the geometry tests derange is one the engine OWNS, so the
    next solve recomputes it and nothing needs restoring. The framing is the
    exception: it is the engine's own state, the pad tests move it by design,
    and nothing puts it back -- so a run ends with the picture panned and zoomed
    wherever the last test left it, and the bench looks broken when it is only
    reframed. Measured after one run: cropped and panned off the default on
    both axes.

    Registers written OUTSIDE the engine's ownership already restore themselves
    and should keep doing so at the test that writes them, not here: the
    EXT_SYNC_SEL test takes its baseline through `register_guard`, and the
    hostile-PLLAD test restores in a `finally`. A test that deranges something
    is responsible for undoing it; this fixture is for the state no single test
    owns.

    Not a snapshot-and-restore of registers, deliberately: tests SET what they
    need rather than saving and restoring. That rule is about preconditions;
    this is teardown, which is a different thing and does not weaken it.

    Reset rather than put back what the run started with, because the control a
    user has is a reset and the suite may only reach for what a user can. The
    default framing is a better place to leave a unit than whatever framing a
    run happened to begin at.

    Also unfreezes, because a run interrupted inside the freeze test leaves
    automation off and the unit looks dead.
    """
    address = request.config.getoption("--host")
    if not address:
        yield
        return

    yield

    try:
        get(address, "/freeze?on=0")
        reset_framing(address)
    except Exception:  # noqa: BLE001 - teardown must not turn a pass into an error
        pass


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


@pytest.fixture
def console(host):
    """A WebSocket console client, opened fresh for each test that asks.

    Not "the one permitted client": the server caps at 5
    (3rdparty/WebSockets/src/WebSocketsServer.h:31) and nothing crashes. The
    old claim came from SerialMirror hanging up on *every* client whenever heap
    dipped, which read from outside as a one-client limit. Still worth closing
    the web UI for a long capture, because it costs heap -- but two clients are
    not forbidden.

    This was scope="session" until 2026-08-05, and that was the whole cause of
    test_the_console_delivers_anything_at_all failing in full-directory runs
    while passing on its own. One socket was opened at the first console test
    and held for the rest of the run; if it died in between -- and SerialMirror
    does drop clients -- every later console test read silence from a dead
    socket and blamed the firmware. Measured while a run was failing: free heap
    21984 (the gate is 8000), and a freshly opened console delivered 70 messages
    in 25 s. The firmware was never the problem, so no retry could have fixed
    it. A per-test connection cannot go stale, and four tests taking a socket
    each is well inside the cap.
    """
    try:
        connection = Console(host)
    except Exception as e:  # noqa: BLE001 - any failure here is just "no console"
        pytest.skip(f"no WebSocket console on {host}: {e}")
    yield connection
    connection.close()


@pytest.fixture
def source(request, host):
    """Asserts the operator's promise that a source is plugged in and should be
    locking. Skips the sync tests without it, so a bench unit with nothing on its
    input does not report the no-sync fault it does not have.

    Suppresses the framing auto-save for the duration. The framing table writes
    itself once the framing has held still, gated by neither --preset-save nor
    anything else, so a test that pans the framing and ends leaves that framing
    on flash as the source's remembered one."""
    if not request.config.getoption("--source"):
        pytest.skip("needs a connected source: pass --source")

    # A build without GBS_DEBUG answers 404 and the tests still run; they just
    # do not get the protection.
    get(host, "/framing/autosave?on=0", timeout=8)
    try:
        yield
    finally:
        get(host, "/framing/autosave?on=1", timeout=8)


@pytest.fixture
def preset_save(request):
    """Opt-in for the tests that write flash. A run that is only checking the
    picture should not spend write cycles."""
    if not request.config.getoption("--preset-save"):
        pytest.skip("writes flash: pass --preset-save")


@pytest.fixture
def preset_load(request):
    """Opt-in for the tests that force a preset load. They leave the output in
    whatever preset they loaded, which a plain --source run must not do."""
    if not request.config.getoption("--preset-load"):
        pytest.skip("forces a preset load and leaves the output mode changed: "
                    "pass --preset-load")


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
