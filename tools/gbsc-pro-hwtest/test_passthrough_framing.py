"""The card's border on the emitted frame with the source PASSED THROUGH.

    pytest test_passthrough_framing.py --host=<ip> --modeserv=<ip> \
        --source --preset-save -v

Pass-through hands the source's own timing to the encoder, so the picture is
placed by the channel's counters and not by a solve. That is a second path to
the question the scaled matrix asks, and the one no acceptance covered.

**ONLY A MODE THE DISPLAY CAN SHOW IS TESTABLE HERE.** Bypass emits the
source's raster, so a 15 kHz mode reaches a sink that refuses it and the
capture comes back black -- which reads as the bypass having failed rather than
as the raster being unshowable. The modes below are ones the bench chain
carries. docs/rgbhv-bypass-trap.md

**AND THE EMITTED SIZE IS THE SOURCE'S, NOT THE OUTPUT SETTING'S**, so the
dongle is opened at whatever it reports being sent. Opening it at 1920x1080
reshapes the frame from the wrong stride and the picture reads as corrupt.

`--preset-save` is required because the route is `preferScalingRgbhv` and
`/uc?x` writes flash. The fixture puts it back.
"""

import time

import pytest

import card_edges
import gbs_unit
import hdmi_capture

# Modes the bench display accepts as a pass-through. 800x600@60 is the one every
# bypass measurement has been taken on; 640x480@60 is the other DMT raster the
# chain carries, and the two differ in sync polarity.
MODES = ("X800 Y600 C256 F60", "X640 Y480 C256 F60")

SYNC_TYPES = ((False, "separate"), (True, "composite"))

# A mode change is followed by seconds of readings inside no standard at all,
# and the encoder needs a further moment to lock to a raster that just moved.
SETTLE_SECONDS = 16.0

# BYPS2DAC and OUT_SYNC_SEL are both 1 in bypass. DAC_RGBS_ADC2DAC is NOT the
# tell, being 0 on both paths. docs/rgbhv-bypass-trap.md
PATH = ("DAC_RGBS_BYPS2DAC", "OUT_SYNC_SEL")

# What each case currently gets wrong. A case listed here that PASSES is the gap
# having closed; one not listed that fails is a regression.
#
# The vertical deficit is an OPEN bypass defect rather than a framing one: the
# channel clips the top and leaves a bar at the bottom, and the amount varies
# between acquisitions, so it cannot anchor a vertical measurement.
# docs/known-issues.md
KNOWN = {
    ("X800 Y600 C256 F60", False): "the bypass channel's vertical placement",
    ("X800 Y600 C256 F60", True): "the bypass channel's vertical placement",
    ("X640 Y480 C256 F60", False): "the bypass channel's vertical placement",
    ("X640 Y480 C256 F60", True): "the bypass channel's vertical placement",
}

# Cases whose SOURCE may not acquire at all, where the verdict is about the
# acquisition rather than the framing.
#
# **IT IS NOT THE CHANNEL.** Measured by hand on 800x600@60 composite: the
# engine sits at `state: absent` with STATUS_SYNC_PROC_VTOTAL 0 and PLLAD_MD on
# the detection sweep's value, on the SCALING path, before the route is touched
# -- and the same mode acquired on composite earlier the same session at
# PLLAD_MD 1342. So this is the composite acquisition instability the scaled
# matrix also carries, and reading it as a pass-through fault would file the
# wrong defect. docs/known-issues.md
UNACQUIRED = {
    ("X800 Y600 C256 F60", True): "composite acquisition is unstable",
    ("X640 Y480 C256 F60", True): "never acquires on composite",
}


def pytest_generate_tests(metafunc):
    if "mode" not in metafunc.fixturenames:
        return
    if not metafunc.config.getoption("--modeserv"):
        metafunc.parametrize("mode,csync", [])
        return
    cases = [(mode, csync) for csync, _ in SYNC_TYPES for mode in MODES]
    metafunc.parametrize(
        "mode,csync", cases,
        ids=[f"{mode.replace(' ', '')}-{name}"
             for _, name in SYNC_TYPES for mode in MODES])


def on_route(host, bypass, limit_s=75.0):
    """Wait until the route is the one asked for. True if it got there.

    Polled rather than slept on: the preference decides the route and the solve
    that reads it runs on the engine's own tick, so a fixed delay is either a
    stall or too short.
    """
    deadline = time.time() + limit_s
    while time.time() < deadline:
        at = gbs_unit.read_fields(host, PATH)
        if at and all(at[name] == (1 if bypass else 0) for name in PATH):
            return True
        time.sleep(3.0)
    return False


def acquired_or_recovered(host, where, mode):
    """Acquired, with the recovery the open acquisition entries name.

    **A ROUTE READ ON AN UNACQUIRED SOURCE SAYS NOTHING**, and it reads exactly
    like a toggle that did not take: measured with the engine at `state:
    absent` on a healthy 800x600@60 source, `STATUS_SYNC_PROC_VTOTAL` 911 and
    every window zero, the route stayed on the scaling path through 75 s of
    polling. Re-issuing the source's MODE and re-detecting cleared it.

    An input bounce is NOT the recovery here: it comes back on the SCALING path
    with the preference still asking for bypass. docs/known-issues.md
    """
    if gbs_unit.acquired_and_settled(host, limit_s=75.0):
        return True
    gbs_unit.mode_serv(where, f"MODE {mode}")
    gbs_unit.get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)
    return gbs_unit.acquired_and_settled(host, limit_s=75.0)


@pytest.fixture(scope="module")
def capture_device():
    """The dongle's node. Skipped where it is unplugged, a missing capture
    being not a framing fault."""
    try:
        return hdmi_capture.device()
    except SystemExit as why:
        pytest.skip(str(why))


@pytest.fixture(scope="module")
def passed_through(request, host):
    """The channel, for the whole module, with the preference put back after.

    Entered once rather than per case: the toggle writes flash, and a case that
    re-entered it would pay a boot's worth of settling for a route it already
    had. That is also why the flag is read here rather than through the
    function-scoped `preset_save` fixture, which a module fixture cannot hold.
    """
    if not request.config.getoption("--preset-save"):
        pytest.skip("--preset-save is needed: /uc?x writes flash")
    where = request.config.getoption("--modeserv")
    assert gbs_unit.want_shaping(host, False), (
        "the shape is still being applied, so every edge read would measure "
        "the bars it pillarboxes with")

    # A SHOWABLE MODE BEFORE THE TOGGLE, and acquired before the route is
    # judged: bypass hands the source's raster to the encoder, so entering it
    # on the 15 kHz bench mode puts the chain on a timing the display refuses.
    assert gbs_unit.modeserv_ok(gbs_unit.mode_serv(where, f"MODE {MODES[0]}")), \
        "the source would not take a mode the display can pass through"
    time.sleep(SETTLE_SECONDS)
    assert acquired_or_recovered(host, where, MODES[0]), \
        "the source never acquired, so the route could not be moved or read"

    gbs_unit.get(host, "/uc?x")
    entered = on_route(host, bypass=True)

    yield entered

    gbs_unit.get(host, "/uc?x")
    on_route(host, bypass=False)
    if where:
        gbs_unit.mode_serv(where, "SYNC 0")
        gbs_unit.mode_serv(where, "MODE X320 Y256 C256 F50")
        gbs_unit.show_card(where)
    gbs_unit.want_shaping(host, True)


@pytest.mark.source_mode
def test_the_passed_through_card_reaches_every_emitted_edge(
        request, host, source, capture_device, passed_through, mode, csync):
    assert passed_through, (
        "the preference never moved the route to pass-through, so there is no "
        "channel to measure")
    where = request.config.getoption("--modeserv")
    expected = KNOWN.get((mode, csync))

    gbs_unit.set_sync_type(where, csync)
    reply = gbs_unit.mode_serv(where, f"MODE {mode}")
    assert gbs_unit.modeserv_ok(reply), f"the source refused {mode!r}: {reply!r}"

    # The arrangement is re-probed with /sc?~, which forgets the sync type:
    # establishSyncType() keys on the input selection, so a SYNC command alone
    # leaves the engine on the path it held and a measurement taken through the
    # wrong path is evidence about nothing. It leaves bypass on its way and the
    # preference puts the route back, which on_route() waits for.
    # docs/sync-type-selection.md
    gbs_unit.get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)
    if not acquired_or_recovered(host, where, mode):
        unacquired = UNACQUIRED.get((mode, csync))
        assert unacquired, "never acquired, or never stopped re-solving"
        pytest.xfail(f"{unacquired}: the source never acquired, so the framing "
                     f"is not what this measured")
    assert on_route(host, bypass=True), (
        "the re-detect did not come back to pass-through, so this would "
        f"measure the scaling path: {gbs_unit.read_fields(host, PATH)}")

    assert gbs_unit.show_card(where), "the source would not draw the card"
    time.sleep(2.0)

    size = hdmi_capture.incoming(capture_device)
    assert size is not None, (
        "the dongle reports no incoming format, so the encoder is sending "
        "nothing it can lock to -- ask s0_49's sync pad before the connector")
    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, capture_device,
                               size[0], size[1])

    # One source pixel is one emitted pixel on this path: the channel passes the
    # raster rather than magnifying it, so the border is a single column and the
    # wholeness measure reads it directly.
    whys = []
    for name, axis in (("across", 1), ("down", 0)):
        judged = card_edges.judge(clip, axis, card_edges.ALLOWANCE)
        for end, surviving in zip(card_edges.ENDS, judged.whole):
            if surviving < card_edges.WHOLE_ENOUGH:
                whys.append(f"{name} {end}: {surviving:.2f} of the border, "
                            f"margins {judged.margins}")
    print(f"\n{mode} {'composite' if csync else 'separate'}: emitted "
          f"{size[0]}x{size[1]}  " + ("; ".join(whys) if whys else "every edge"))

    if expected:
        if not whys:
            pytest.fail(f"{mode} {'composite' if csync else 'separate'} now "
                        f"frames the card -- {expected} has closed, so remove "
                        f"the entry")
        pytest.xfail(f"{expected}: " + "; ".join(whys))
    assert not whys, "; ".join(whys)
