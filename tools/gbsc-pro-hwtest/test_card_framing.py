"""The default framing, judged off the emitted frame, one test per source mode.

THE GOAL: a default framing puts the source's outermost pixel on the emitted
frame's outermost pixel. `PATTERN CARD` draws a one-pixel green frame on exactly
those pixels, so the goal is readable off one capture and needs no reference --
the capture IS the emitted frame.
docs/investigations/full-screen-framing-on-the-vesa-modes.md

**A MODE IS NOT A PATH.** The engine sees a `SourceKey` -- frame count, field
rate, sync duty and both polarities -- so the stock AKF50's 28 modes at 256
colours reach far fewer distinct solves, and seven of them are one key. `MODES`
is what the monitor definition offers, not what the engine branches on. The set
below is one mode per behaviour the solve actually has; `--all-modes` sweeps the
rest, for when the question is the monitor definition rather than the engine.

The measurement is `card_edges`', shared with the tool that sweeps
interactively, so there is one statement of what flush means. What is here that
the tool has not is the standing record: a mode in `NARROW` is a known deficit
with its measured size, so a NEW mode failing is a regression and a listed one
passing is the gap having closed.

Changes the SOURCE mode and resets the framing, so it needs --modeserv and
--source, and it puts the bench mode back however the module ends. Each mode
costs about twenty seconds: a mode change, the engine settling, and a clip off
the USB capture.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_edges
import gbs_unit
import hdmi_capture

BENCH_MODE = "X320 Y256 C256 F50"

# One mode per behaviour the solve has, with what each is here for. Adding a
# mode that shares every entry in this table with one already here buys a second
# run of the same path.
COVERS = {
    # The line doubler, the only 15 kHz key, and the one raster with no
    # published equivalent -- its capture comes from the envelope rather than
    # from a standard's own numbers.
    "X320 Y256 C256 F50": "doubled, 50 Hz, Acorn raster, positive sync",
    # VESA DMT, which states where active video begins, and the mode the
    # placement constants were measured against.
    "X640 Y480 C256 F60": "undoubled, 60 Hz, DMT raster, negative sync",
    # The other sync polarity, the highest line rate the file carries, and the
    # widest capture relative to the raster.
    "X800 Y600 C256 F60": "undoubled, 60 Hz, DMT raster, positive sync",
    # 50 Hz on an undoubled line, which no other entry reaches: the capture
    # window's default is selected on field rate. docs/vesa-gtf.md
    "X640 Y512 C256 F50": "undoubled, 50 Hz, Acorn raster",
    # The shortest source line in the file, 300 pixels, and mixed polarity.
    "X240 Y352 C256 F70": "undoubled, 70 Hz, shortest line, mixed polarity",
    # The widest source line, 2112 pixels, where the divider ceiling and the
    # eleven-bit line counter bind -- and a KEY COLLISION with 800x600@60, which
    # the engine cannot tell it apart from. docs/capture-limits.md
    "X1600 Y600 C256 F60": "widest line, key collision with 800x600@60",
}

# Modes whose picture is NARROWER than the frame rather than misplaced, against
# the deficit measured at 1080p. It grows with the resolution and is a scale
# question rather than a placement one: one value of `OutputMode::HsyncStartPx`
# brings the near edge flush on every mode here, and what is left is span.
# Closing it needs minification, which the VDS cannot do.
# docs/investigations/full-screen-framing-on-the-vesa-modes.md,
# docs/scaling-down-path.md
NARROW = {}


def pytest_generate_tests(metafunc):
    """One case per mode under test.

    The full set is asked of the SOURCE at collection, because the monitor
    definition loaded there is what states it and nothing else does. No
    --modeserv means no cases rather than a guess.
    """
    if "mode" not in metafunc.fixturenames:
        return
    where = metafunc.config.getoption("--modeserv")
    if not where:
        metafunc.parametrize("mode", [])
        return
    offered = gbs_unit.list_modes(where)
    if metafunc.config.getoption("--all-modes"):
        chosen = offered
    else:
        chosen = [m for m in offered if m in COVERS]
    metafunc.parametrize("mode", chosen, ids=lambda m: m.replace(" ", ""))


@pytest.fixture(scope="module")
def capture_device():
    """The dongle's node, by USB id. Skipped where it is unplugged, a missing
    capture being not a framing fault."""
    try:
        return hdmi_capture.device()
    except SystemExit as why:
        pytest.skip(str(why))


@pytest.fixture(scope="module")
def bench_mode_restored(request):
    """The source back on the bench mode when the module ends, however it ends.
    The modes are judged in whatever order the monitor definition lists them, so
    the last one is nobody's choice."""
    yield
    where = request.config.getoption("--modeserv")
    if where:
        gbs_unit.mode_serv(where, f"MODE {BENCH_MODE}")
        gbs_unit.show_card(where)


@pytest.mark.source_mode
def test_the_default_framing_puts_the_card_flush_to_the_emitted_edges(
        request, host, source, capture_device, bench_mode_restored, mode):
    where = request.config.getoption("--modeserv")

    reply = gbs_unit.mode_serv(where, f"MODE {mode}")
    assert gbs_unit.modeserv_ok(reply), f"the source refused {mode!r}: {reply!r}"
    assert gbs_unit.acquired_and_settled(host), \
        "never acquired, or never stopped re-solving"
    # AFTER the mode change, which repaints the source's default pattern, and
    # with the animation off so one clip is evidence.
    assert gbs_unit.show_card(where), "the source would not draw the card"
    assert gbs_unit.reset_framing(host) is not None, "the framing never reset"

    # A clip rather than a frame: the card's outermost ring flashes, and in the
    # phase where it is yellow the green smears into it and falls under the hue
    # test, so one capture reports the edge missing.
    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, capture_device)
    found = {}
    for name, axis in (("across", 1), ("down", 0)):
        shown, margins, why = card_edges.judge(clip, axis, card_edges.FLUSH)
        found[name] = (shown, margins, why)

    whys = [f"{name}: {v[2]}" for name, v in found.items() if v[2]]
    margins = "  ".join(f"{name} {v[1]}" if v[1] else f"{name} -"
                        for name, v in found.items())
    known = NARROW.get(mode)
    if known is not None:
        assert whys, f"known deficit {known} has closed -- take it out of NARROW"
        pytest.xfail(f"{known}: {'; '.join(whys)}   [{margins}]")
    assert not whys, f"{'; '.join(whys)}   [{margins}]"
