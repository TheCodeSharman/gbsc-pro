"""Whether an RGBHV source is scaled or bypassed, and what decides it.

The line count used to decide: a source above 535 lines was pushed into bypass
and had no branch to leave by, so it stayed there for the life of the boot. The
preference decides now, and the count decides nothing.

    pytest test_rgbhv_bypass.py --host=<ip> --modeserv=<ip> -v

`--preset-save` opts into the half that toggles the preference, because the
toggle writes flash. It puts it back.
"""

import socket
import time

import pytest

from gbs_unit import get, read_fields

# VTOTAL 627, comfortably above the count that used to force bypass, and a mode
# the bench display accepts as a passthrough -- which is what made the trap
# survivable, and therefore easy to read as working.
TALL_MODE = "MODE X800 Y600 C256 F60"
TALL_LINES = 627
BENCH_MODE = "MODE X320 Y256 C256 F50"
BENCH_LINES = 311

# A mode change is followed by seconds of readings inside no standard at all.
SETTLE_SECONDS = 14.0

# What bypassModeSwitch_RGBHV() hardcodes. Reading it means the switch ran,
# whatever the path bits say.
BYPASS_DIVIDER = 1856

# ADC2DAC and OUT_SYNC_SEL are 1 in bypass and 0 on the scaling path. The scale
# registers are NOT the tell: bypass leaves them on the last scaled load's.
PATH = ("DAC_RGBS_ADC2DAC", "OUT_SYNC_SEL", "GBS_OPTION_SCALING_RGBHV",
        "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HTOTAL", "PLLAD_MD")


def mode_serv(where, command):
    """One command per connection: the close is the end of the reply."""
    with socket.create_connection((where, 6502), 10) as link:
        link.sendall((command + "\n").encode())
        return link.recv(200).decode(errors="replace").strip()


def settled(host, attempts=10, interval=4.0):
    """The path fields once two consecutive reads agree.

    One read taken mid-excursion catches the state on its way somewhere, which
    is neither of the two answers this asks about.
    """
    previous = None
    for _ in range(attempts):
        at = read_fields(host, PATH)
        if at and at == previous:
            return at
        previous = at
        time.sleep(interval)
    return previous


def in_bypass(at):
    return at["DAC_RGBS_ADC2DAC"] == 1 and at["OUT_SYNC_SEL"] == 1


def scaling(at):
    return (at["DAC_RGBS_ADC2DAC"] == 0 and at["OUT_SYNC_SEL"] == 0
            and at["GBS_OPTION_SCALING_RGBHV"] == 1)


def at_mode(host, where, command, lines):
    reply = mode_serv(where, command)
    if not reply.startswith("OK"):
        pytest.skip(f"the source refused {command}: {reply}")
    time.sleep(SETTLE_SECONDS)
    at = settled(host)
    if not at or at["STATUS_SYNC_PROC_VTOTAL"] != lines:
        pytest.skip(f"the source is counting "
                    f"{(at or {}).get('STATUS_SYNC_PROC_VTOTAL')} lines, not {lines}")
    return at


@pytest.fixture
def tall_source(request, host):
    where = request.config.getoption("--modeserv")
    try:
        yield at_mode(host, where, TALL_MODE, TALL_LINES)
    finally:
        mode_serv(where, BENCH_MODE)
        time.sleep(SETTLE_SECONDS)


@pytest.mark.source_mode
def test_a_source_too_tall_for_the_old_gate_is_scaled(tall_source):
    assert scaling(tall_source), (
        f"a {TALL_LINES}-line RGBHV source is not on the scaling path: {tall_source}")


@pytest.mark.source_mode
def test_a_tall_sources_divider_is_measured_and_latched(tall_source):
    # STATUS_SYNC_PROC_HTOTAL counts real ADC clocks, so it is the only witness
    # on the chip that the divider reached the PLL rather than just the register.
    assert tall_source["PLLAD_MD"] != BYPASS_DIVIDER, (
        f"the divider is the bypass switch's hardcoded {BYPASS_DIVIDER}, not a measured one")
    assert abs(tall_source["STATUS_SYNC_PROC_HTOTAL"] - tall_source["PLLAD_MD"]) <= 2, (
        f"HTOTAL {tall_source['STATUS_SYNC_PROC_HTOTAL']} against PLLAD_MD "
        f"{tall_source['PLLAD_MD']}: written but never latched")


@pytest.mark.source_mode
def test_crossing_the_old_gate_in_both_directions_keeps_scaling(request, host):
    # The trap was one-way, so a test that only ever goes up finds nothing.
    where = request.config.getoption("--modeserv")
    try:
        for command, lines in ((BENCH_MODE, BENCH_LINES), (TALL_MODE, TALL_LINES),
                               (BENCH_MODE, BENCH_LINES), (TALL_MODE, TALL_LINES)):
            at = at_mode(host, where, command, lines)
            assert scaling(at), f"{lines} lines left the scaling path: {at}"
    finally:
        mode_serv(where, BENCH_MODE)
        time.sleep(SETTLE_SECONDS)


@pytest.mark.source_mode
def test_the_preference_still_reaches_bypass_and_still_leaves_it(
        request, host, preset_save, tall_source):
    # Bypass is offered, not imposed -- and the leaving half is what no line
    # count above the old gate could do at all.
    get(host, "/uc?x")
    off = settled(host)
    try:
        assert in_bypass(off), f"the preference did not reach bypass: {off}"
        assert off["PLLAD_MD"] == BYPASS_DIVIDER, (
            f"in bypass the divider should be the switch's {BYPASS_DIVIDER}: {off}")
    finally:
        get(host, "/uc?x")
    back = settled(host)
    assert scaling(back), (
        f"a {TALL_LINES}-line source could not leave bypass: {back}")
