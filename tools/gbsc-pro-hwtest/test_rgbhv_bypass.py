"""Whether an RGBHV source is scaled or bypassed, and what decides it.

The line count used to decide: a source above 535 lines was pushed into bypass
and had no branch to leave by, so it stayed there for the life of the boot. The
preference decides now, and the count decides nothing.

    pytest test_rgbhv_bypass.py --host=<ip> --modeserv=<ip> -v

`--preset-save` opts into the half that toggles the preference, because the
toggle writes flash. It puts it back.
"""

import time

import pytest

from gbs_unit import get, mode_serv, read_fields

# VTOTAL 627, comfortably above the count that used to force bypass, and a mode
# the bench display accepts as a passthrough -- which is what made the trap
# survivable, and therefore easy to read as working.
TALL_MODE = "MODE X800 Y600 C256 F60"
TALL_LINES = 627
BENCH_MODE = "MODE X320 Y256 C256 F50"
BENCH_LINES = 311

# A mode change is followed by seconds of readings inside no standard at all.
SETTLE_SECONDS = 14.0

# BYPS2DAC and OUT_SYNC_SEL are 1 in bypass and 0 on the scaling path. The scale
# registers are NOT the tell: bypass leaves them on the last scaled load's.
PATH = ("DAC_RGBS_BYPS2DAC", "OUT_SYNC_SEL",
        "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HTOTAL", "PLLAD_MD")




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


# DAC_RGBS_BYPS2DAC, not DAC_RGBS_ADC2DAC. The second is 0 on BOTH paths, so a
# predicate keyed on it being 1 can never hold and the bypass half of every
# test using it is unreachable -- which is how a bypass fault reached the bench
# with these tests reported green. docs/rgbhv-bypass-trap.md
def in_bypass(at):
    return at["DAC_RGBS_BYPS2DAC"] == 1 and at["OUT_SYNC_SEL"] == 1


def scaling(at):
    # Whether scaling RGBHV is in force is FIRMWARE state, held in PresetLoad.
    # It used to be a bit at s1_2c, and asserting that bit here outlived the
    # firmware writing it: every one of these tests failed on the marker alone
    # while the route bits read correct. What the chip can still answer is which
    # route the video takes, which is the question these tests ask.
    return at["DAC_RGBS_BYPS2DAC"] == 0 and at["OUT_SYNC_SEL"] == 0


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

        # LATCHED, not a particular number. The channel sizes its divider from
        # the measured line rate through the same chooser the scaling path
        # uses, so asserting the switch's old hardcoded 1856 pinned a value
        # nothing writes any more. HTOTAL counts real ADC clocks, so it is the
        # only witness that the divider reached the PLL.
        assert abs(off["STATUS_SYNC_PROC_HTOTAL"] - off["PLLAD_MD"]) <= 2, (
            f"HTOTAL {off['STATUS_SYNC_PROC_HTOTAL']} against PLLAD_MD "
            f"{off['PLLAD_MD']}: the bypass divider never latched: {off}")
    finally:
        get(host, "/uc?x")
    back = settled(host)
    assert scaling(back), (
        f"a {TALL_LINES}-line source could not leave bypass: {back}")


@pytest.mark.source_mode
def test_the_permission_survives_being_let_go_and_takes_the_source_back(
        request, host, preset_save, tall_source):
    """The ROUND TRIP, with the permission left on throughout.

    Letting a slowed source go is half the behaviour; taking it back when it
    speeds up again is the other half, and nothing walked it. The two are not
    the same branch -- leaving is a measurement that stops qualifying, and
    returning is the channel being sized a second time from a rate that did
    qualify. A permission spent on the way out would strand every later source
    on the scaling path with the row still reading Pass Through.
    """
    where = request.config.getoption("--modeserv")
    get(host, "/uc?x")
    try:
        assert in_bypass(settled(host)), (
            "the preference did not reach bypass, so the round trip is untested")

        slowed = at_mode(host, where, BENCH_MODE, BENCH_LINES)
        assert scaling(slowed), (
            f"a bypassed source that slowed to {BENCH_LINES} lines stayed in "
            f"bypass: {slowed}")

        back = at_mode(host, where, TALL_MODE, TALL_LINES)
        assert in_bypass(back), (
            f"the source sped up again and the permission did not take it "
            f"back, so leaving spent it: {back}")
        assert abs(back["STATUS_SYNC_PROC_HTOTAL"] - back["PLLAD_MD"]) <= 2, (
            f"re-entered bypass on an unlatched divider: {back}")
    finally:
        get(host, "/uc?x")
        mode_serv(where, BENCH_MODE)
        time.sleep(SETTLE_SECONDS)


@pytest.mark.source_mode
def test_a_bypassed_source_that_slows_leaves_bypass_on_its_own(
        request, host, preset_save, tall_source):
    """A source that drops below the floor WHILE BYPASSED has to be let go.

    The rate is measured on the way into bypass and nothing measures again
    there, so a re-ask that consults the held rate keeps answering with the mode
    bypass was entered on. The source then slows to 15 kHz underneath it, the
    branch that would leave never fires, and the panel shows no signal at all --
    with every register self-consistent and the count plainly reading 311.
    """
    where = request.config.getoption("--modeserv")
    get(host, "/uc?x")
    try:
        assert in_bypass(settled(host)), (
            "the preference did not reach bypass, so the slowing half is untested")

        at = at_mode(host, where, BENCH_MODE, BENCH_LINES)
        assert scaling(at), (
            f"a bypassed source that slowed to {BENCH_LINES} lines stayed in "
            f"bypass, which the display shows as no signal: {at}")
    finally:
        get(host, "/uc?x")
        mode_serv(where, BENCH_MODE)
        time.sleep(SETTLE_SECONDS)
