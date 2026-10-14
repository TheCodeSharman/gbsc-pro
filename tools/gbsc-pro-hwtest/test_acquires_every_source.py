"""Every source on the bench acquires, on both routes.

    pytest test_acquires_every_source.py --host=<ip> -v

Six combinations: the three sync arrangements the bench carries -- separate
sync, composite sync and sync on green -- each scaled and each passed through.
Two of them are the ones under repair and four are the regression cover, and
they are one test so a run says which combinations still work rather than only
that something broke.

**THE PRECONDITIONS FAIL RATHER THAN SKIP.** A bench without ModeServ or with
the Wii unplugged cannot answer the question, and a skip there reports green on
a suite that tested a third of what it names. Each failure says what to plug in.

The Wii's output mode is not scriptable, so 480p is assumed and checked: it is
the mode `SOURCES` holds a line rate for.

It changes the source's sync type, the selected input and the route, and puts
all three back in teardown -- so it leaves the bench where it found it rather
than at a default nobody chose.
"""

import time

import pytest

from gbs_unit import (
    SOURCES,
    get,
    get_json,
    mode_serv,
    read_fields,
    select_input,
    setting,
    wait_for_acquisition,
)

# 800x600@60 is what SOURCES holds the `vga` line rate for, and it is above the
# line doubler so both routes are reachable on it.
BENCH_MODE = "MODE X800 Y600 C256 F60"

# A mode or arrangement change is followed by seconds of readings inside no
# standard at all, and the sync-type probe spends its whole window on a
# composite source.
SETTLE_SECONDS = 14.0

# How long a route change is given to re-solve. Longer than an acquisition
# because the switch re-runs detection underneath it.
ROUTE_SECONDS = 20.0

PATH = ("DAC_RGBS_BYPS2DAC", "OUT_SYNC_SEL", "SP_SOG_MODE",
        "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HTOTAL", "PLLAD_MD")


def in_bypass(at):
    return at["DAC_RGBS_BYPS2DAC"] == 1 and at["OUT_SYNC_SEL"] == 1


def scaling(at):
    return at["DAC_RGBS_BYPS2DAC"] == 0 and at["OUT_SYNC_SEL"] == 0


def set_sync_type(where, csync):
    """The source's sync type, and the re-probe that makes the engine follow it.

    establishSyncType() keys on the input selection, so a SYNC command alone
    leaves the engine on the path it held -- and a measurement taken through the
    wrong path is evidence about nothing. docs/sync-type-selection.md
    """
    reply = mode_serv(where, "SYNC 1" if csync else "SYNC 0")
    assert reply.startswith("OK"), f"the source refused the sync type: {reply}"
    wanted = " SYNC 1 " if csync else " SYNC 0 "
    assert wanted in mode_serv(where, "SYNC") + " ", (
        f"the source did not take {wanted.strip()}: {mode_serv(where, 'SYNC')}")


def want_passed_through(host, passed_through):
    """`preferScalingRgbhv` is what decides the route, and /uc?x toggles it."""
    if (setting(host, "scale-rgbhv") == "0") == passed_through:
        return
    get(host, "/uc?x")
    time.sleep(2.0)


@pytest.fixture(scope="module")
def bench(request, host):
    """Both sources present and answering, or a failure saying which is not.

    Checked once for the module: each probe costs an input change, and a bench
    that cannot answer will not start answering between tests.
    """
    where = request.config.getoption("--modeserv")
    if not where:
        pytest.fail(
            "no ModeServ host. The RISC PC drives the sync arrangements this "
            "tests, and nothing else on the bench can: pass --modeserv <host>.")

    try:
        version = mode_serv(where, "VERSION")
    except OSError as unreachable:
        pytest.fail(
            f"ModeServ at {where} is not answering ({unreachable}). Start it on "
            f"the RISC PC -- tools/video-source/ in the RiscPc repo -- before "
            f"running this.")
    assert version.startswith("OK ModeServ"), (
        f"ModeServ at {where} answered {version!r} rather than a version. Both "
        f"halves tokenise separately, so a build that landed one and not the "
        f"other leaves a server whose halves disagree.")

    # F-1 is VIDC20 with no field rate to report: the reply names the right
    # resolution while the machine drives no raster the scaler can acquire, and
    # everything downstream of it reads as a scaler fault.
    reply = mode_serv(where, BENCH_MODE)
    assert reply.startswith("OK"), f"the source refused {BENCH_MODE}: {reply}"
    assert "F-1" not in reply, (
        f"the source read back {reply}: VIDC20 has no field rate, so it is not "
        f"driving a usable raster. Re-issue the mode before judging anything.")

    original = {
        "scale-rgbhv": setting(host, "scale-rgbhv"),
        "input": setting(host, "input"),
        "sync": mode_serv(where, "SYNC"),
    }

    # The Wii is checked FIRST and by acquiring, not by a register: nothing on
    # the board can see whether anything is plugged into an input, so a source
    # that acquires is the only proof there is one.
    select_input(host, "ypbpr")
    took = wait_for_acquisition(host, "ypbpr")
    if took is None:
        _, payload = get_json(host, "/geometry")
        pytest.fail(
            f"the Wii did not acquire on ypbpr within the limit, so the "
            f"component and sync-on-green half of this cannot run. Check it is "
            f"plugged in, powered, and set to 480p -- that is the mode whose "
            f"line rate {SOURCES['ypbpr']} Hz is being waited for. "
            f"/geometry reads {payload}")

    try:
        yield where
    finally:
        try:
            mode_serv(where, "SYNC 1"
                      if " SYNC 1 " in original["sync"] + " " else "SYNC 0")
            mode_serv(where, BENCH_MODE)
            mode_serv(where, "PATTERN CARD")
            mode_serv(where, "ANIM OFF")
            if original["input"]:
                select_input(host, original["input"])
            want_passed_through(host, original["scale-rgbhv"] == "0")
            get(host, "/sc?~")
        except Exception:  # noqa: BLE001 - teardown must not mask a failure
            pass


# Grouped by input so the run pays for one input change rather than six.
ARRANGEMENTS = [
    ("vga", False, False, "separate sync, scaled"),
    ("vga", False, True, "separate sync, passed through"),
    ("vga", True, False, "composite sync, scaled"),
    ("vga", True, True, "composite sync, passed through"),
    ("ypbpr", None, False, "sync on green, scaled"),
    ("ypbpr", None, True, "sync on green, passed through"),
]


@pytest.mark.source_mode
@pytest.mark.parametrize("source,csync,passed_through,what", ARRANGEMENTS,
                         ids=[entry[3].replace(", ", "-").replace(" ", "-")
                              for entry in ARRANGEMENTS])
def test_the_source_acquires(bench, host, preset_save, source, csync,
                             passed_through, what):
    where = bench
    if csync is not None:
        set_sync_type(where, csync)

    want_passed_through(host, passed_through)
    select_input(host, source)

    # The arrangement is re-probed rather than trusted: a sync type changed on
    # one cable is not followed, and the route switch re-runs detection.
    get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)

    took = wait_for_acquisition(host, source, limit_s=ROUTE_SECONDS + 40.0)
    at = read_fields(host, PATH)
    assert took is not None, (
        f"{source} on {what} never acquired at {SOURCES[source]} Hz. {at}")

    assert (in_bypass(at) if passed_through else scaling(at)), (
        f"{source} on {what} acquired on the wrong route. "
        f"DAC_RGBS_BYPS2DAC and OUT_SYNC_SEL are 1 in bypass and 0 scaled: {at}")

    # STATUS_SYNC_PROC_HTOTAL counts real ADC clocks, so it is the only witness
    # on the chip that the divider reached the PLL rather than just the register.
    assert abs(at["STATUS_SYNC_PROC_HTOTAL"] - at["PLLAD_MD"]) <= 2, (
        f"{source} on {what} acquired on an unlatched divider: {at}")
