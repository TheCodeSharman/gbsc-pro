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

**AT ONE OUTPUT AND WITH NO DECLARED SHAPE, ON BOTH SYNC TYPES.** The output
is a variable every case has to share, or the modes are judged against
different rasters. The shape has to be OFF rather than 16:9: a declared shape
narrows the room, the bars it pillarboxes with are OUR blanking, and a strip
taken at the frame's own edge then measures them instead of the picture --
which reported all six modes' horizontal edges as missing. And the sync type is
the dimension nothing crossed: a mode judged on one of them is a framing fitted
to one half of the bench.

Changes the SOURCE mode and its sync type and resets the framing, so it needs
--modeserv and --source, and it puts the bench mode, the sync type, the output
and the shape back however the module ends. Each case costs about forty
seconds: a sync-type change with the re-probe that makes the engine follow it,
a mode change, the engine settling, and a clip off the USB capture.
"""

import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_edges
import gbs_unit
import hdmi_capture

BENCH_MODE = "X320 Y256 C256 F50"

# The output every case is judged at. 1080p because it is the one the acceptance
# suite uses and the one the lower resolutions' own open entries are measured
# against. docs/known-issues.md
OUTPUT = "1080p"

# A sync-type change leaves the engine on the path it held, and the re-probe
# that follows it is a detection sweep -- so a case pays for the source leaving
# and returning before anything it measures means the mode it asked for.
SETTLE_SECONDS = 14.0

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
}

# **1600x600@60 IS NOT HERE: A KEY CLASH CANNOT BE OPTIMISED FOR.** It is one
# `SourceKey` with 800x600@60 -- same field rate, same line rate, both 75.8%
# active -- so ONE solve frames both and no framing can suit the pair better
# than it suits either. Judging both scores that solve twice and reports
# whichever of the two it suits as a pass and the other as a deficit, which
# measures the clash and not the framing. 1600x600 is also an outlier of a
# resolution, so it is not the one to weight the defaults toward. `--all-modes`
# still reaches it. docs/capture-limits.md

# Modes whose picture is NARROWER than the frame rather than misplaced, against
# the deficit measured at 1080p. It grows with the resolution and is a scale
# question rather than a placement one: one value of `OutputMode::HsyncStartPx`
# brings the near edge flush on every mode here, and what is left is span.
# Closing it needs minification, which the VDS cannot do.
# docs/investigations/full-screen-framing-on-the-vesa-modes.md,
# docs/scaling-down-path.md
# How the cases read: the mode, then which sync type carried it.
SYNC_TYPES = ((False, "separate"), (True, "composite"))

# What each case currently gets wrong, measured at 1080p with the shape off and
# the framing reset, over two full runs of the matrix. A case listed here that
# PASSES is the gap having closed; one not listed that fails is a regression.
#
# **800x600@60 IS THE ONLY MODE THE TWO SYNC TYPES AGREE ON**, and it is the
# mode every other measurement has been taken on. That is what this matrix
# exists to catch.
#
# **EVERY ENTRY CARRIES THE SIZE IT IS OUT BY**, `card_edges.Judgement.out_by`,
# because a green column a pixel off the frame and one a hundred pixels off are
# the same fault at two sizes and only the size says which modes are nearly
# right.
#
# The allowance is `card_edges.ALLOWANCE`, the instrument's own column plus the
# pixel or two the solve lands apart between runs: with the shape off the
# picture fills the raster exactly, so rounding at either end moves the
# outermost green column and can take it off the frame. The sink's own window
# placement is inside that margin too -- latched when the link acquires, and
# NOT taken from our blanking, which is refuted. What it keys on is not
# established. docs/known-issues.md
#
# **THE DISPLACEMENT REVERSES SIGN WITH THE SYNC TYPE, AND THAT IS THE FINDING
# THE SIZES CARRY.** Separate sync loses the NEAR edge off the frame and keeps
# the margin at the far one, and composite sync did the opposite. A shift moves
# the two ends together, so the pair is one displacement per sync type rather
# than six modes misframed independently.
#
# **THE COMPOSITE LEG'S SHARE OF IT WAS THE POLARITY CORRECTION**, which
# follows the sync path now rather than the measured bit: 240x352@70 went from
# 14 across to 2, 640x512@50 from 17 down to 1, 320x256@50 from overrunning
# both ends to a bounded 16, and 800x600@60 from 5 to 2, which is why it is no
# longer listed. Measured over three full runs. docs/known-issues.md
# **THE FAR EDGE'S REMAINING SHARE IS THE WIDTH PARITY, NOT THE FIT.** Measured
# A/B on one bench, the fit rounding to nearest against rounding up, three cases
# each twice: the far edge's surviving pixel went 0.08 to 1.00 down and 0.15 to
# 0.56 across at 320x256@50, 0.70 to 0.80 and 0.79 to 1.00 across at 800x600@60.
# VERTICALLY THE FIT WAS THE WHOLE STORY; horizontally about a third of the
# pixel is still lost, which is the parity step-back taking a unit the aperture
# then floors onto. Replacing it needs a knob on the produced width that does
# not move the picture, which `OutputWindow::solve()` states.
# docs/investigations/the-room-is-what-the-encoder-carries.md
NARROW = {
    ("X240 Y352 C256 F70", True): "out by 2 to 5, the near edge across or the "
                                  "top edge -- the 14 across closed, this is "
                                  "what it hid",
    ("X320 Y256 C256 F50", False): "0.56 of the far edge's pixel across, and "
                                   "the near margin 3 to 4 -- the fit closed "
                                   "the vertical outright, the parity holds "
                                   "the horizontal",
    # NOT THE FIT, and not this axis's rounding: the near horizontal border is
    # absent from the emitted frame on BOTH sync types and on both sides of the
    # fit A/B, with the vertical reading 0.50 to 0.54 at one end throughout. A
    # deficit that survives a byte-identical register set is downstream of the
    # board. docs/known-issues.md
    ("X800 Y600 C256 F60", False): "the near edge across is off the frame, and "
                                   "0.50 of the far edge down",
    ("X800 Y600 C256 F60", True): "the near edge across is off the frame, and "
                                  "0.53 of the near edge down",
    ("X320 Y256 C256 F50", True): "out by 16 across, 16 at the near edge",
    ("X640 Y480 C256 F60", False): "out by 15 to 20 across, the near edge off the frame",
    # NOT A FRAMING DEFICIT. The mode never acquires on composite -- 90 s and
    # never acquired when probed directly, and every matrix run reads the state
    # line as PLLAD_MD 2040 against a 2041-unit capture with a frame count of
    # 312 on a mode that sends 525. It reaches here rather than being skipped
    # because acquired_and_settled() cannot tell the asked-for mode from
    # whatever else settled. docs/known-issues.md
    ("X640 Y480 C256 F60", True): "never acquires -- an acquisition entry, not "
                                  "a framing one; both ends overrun on both axes",
    ("X640 Y512 C256 F50", True): "out by 8 across, the near edge off the "
                                  "frame; the vertical reads 1 twice and 29 once",
    # NOT A FRAMING DEFICIT, and not a size to close: --all-modes only, and
    # its solve is 800x600@60's. The two want placements about 5 output pixels
    # apart off that one solve, so this reading moves whenever that mode's
    # framing does -- 2 before it was flush, 6 to 8 after. The entry exists so
    # a sweep does not read it as a regression.
    ("X1600 Y600 C256 F60", True): "shares 800x600@60's solve -- a clash, not a "
                                   "framing; reads 6 to 8 across",
}

# Cases whose VERDICT varies between runs on one firmware, over two full runs of
# the matrix. Either outcome is accepted, because such a case is evidence
# neither way: listed in NARROW it fails the run that lands the better reading,
# and left out it fails the run that lands the worse one.
#
# **THE SPREAD IS NOT IN OUTPUT PIXELS**, which is why no allowance covers it.
# Measured run against run it is about 6 output pixels on the 240-pixel source
# line, 3 on the 640-pixel one, 2 on the 800-pixel one and under 1 on the
# 1600-pixel one -- an ordering that tracks the magnification, so the quantity
# is the source's own pixel or the capture's unit rather than the emitted frame's
# pixel. Which of the two is open, and the green frame's captured width is too
# noisy to settle it: 4 columns at one edge against 8 at the other on one clip
# where the magnification is 4.5. docs/known-issues.md
UNSTABLE = {
    ("X240 Y352 C256 F70", False):
        "out by 10 across in one run, inside the allowance in the next",
    ("X640 Y512 C256 F50", False):
        "7, 4 and 7 at the far edge over three runs, straddling the limit the "
        "width parity carries",
}


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
    cases = [(mode, csync) for csync, _ in SYNC_TYPES for mode in chosen]
    metafunc.parametrize(
        "mode,csync", cases,
        ids=[f"{mode.replace(' ', '')}-{name}"
             for _, name in SYNC_TYPES for mode in chosen])


@pytest.fixture(scope="module")
def capture_device():
    """The dongle's node, by USB id. Skipped where it is unplugged, a missing
    capture being not a framing fault."""
    try:
        return hdmi_capture.device()
    except SystemExit as why:
        pytest.skip(str(why))


@pytest.fixture(scope="module")
def judged_at(request, host):
    """One output and no declared shape for every case, and the bench put back.

    The modes are judged in whatever order the monitor definition lists them,
    so the last one is nobody's choice -- and neither is the output, the shape
    or the sync type a run happens to end on.

    **NO STORED FRAMING MAY ANSWER.** An entry is recalled on acquisition, so
    what a case would judge is the last session's tuning rather than the
    computed default. Each case resets this source's entry, and autosave is off
    so the run cannot write one of its own. It is runtime state with no key in
    the preferences, so it goes back on at the end rather than being restored.
    """
    where = request.config.getoption("--modeserv")
    # THE DECLARED ASPECT IS SAVED SEPARATELY FROM THE SHAPING FLAG. Turning
    # the shape off is Aspect::Fill, and turning it back on does not remember
    # what was declared -- it lands on whatever /uc?G cycles to, so a run left
    # a 4:3 bench showing 16:9 and the source stretched to fill the raster.
    original = {
        "output": gbs_unit.setting(host, "output"),
        "shaping": gbs_unit.shaping(host),
        "aspect": gbs_unit.get_json(host, "/geometry")[1].get("aspect"),
        "sync": gbs_unit.mode_serv(where, "SYNC") if where else "",
    }

    assert gbs_unit.choose_output(host, OUTPUT), (
        f"the output never reached {OUTPUT}, so the modes would be judged "
        f"against different rasters")
    assert gbs_unit.want_shaping(host, False), (
        "the shape is still being applied, so every edge read would measure "
        "the bars it pillarboxes with")
    assert gbs_unit.autosave(host, False), (
        "the framing table would not stop autosaving")

    yield

    if where:
        gbs_unit.mode_serv(where, "SYNC 1"
                           if " SYNC 1 " in original["sync"] + " " else "SYNC 0")
        gbs_unit.mode_serv(where, f"MODE {BENCH_MODE}")
        gbs_unit.show_card(where)
    if original["output"]:
        gbs_unit.restore_output_mode(host, original["output"])
    gbs_unit.want_shaping(host, original["shaping"])
    if original["aspect"]:
        gbs_unit.choose_aspect(host, original["aspect"])
    gbs_unit.autosave(host, True)


def state_of(host):
    """What a verdict has to be read against: the divider the capture was
    sampled at, whether a shape is narrowing the room, and the capture itself.
    docs/known-issues.md"""
    _, geo = gbs_unit.get_json(host, "/geometry")
    divider = gbs_unit.read_named(host, "PLLAD_MD")
    return (f"MD {divider} shaping {geo.get('shaping')} "
            f"aspect {geo.get('aspect')} ch {geo.get('ch')} cv {geo.get('cv')}")


def acquired_or_recovered(host):
    """Acquired, with the one recovery the open entries name if it is not.

    **A CASE THAT LANDS IN THE NON-COUNTING STATE POISONS EVERY CASE AFTER
    IT.** Measured over the matrix: one composite case acquired and the five
    after it reported `never acquired` without being judged at all, which reads
    as five broken modes and is one state nothing cleared. `/sc?~` does not
    clear it where an input bounce does, so a run that cannot recover reports a
    cascade instead of a verdict. docs/known-issues.md

    The bounce goes by way of `ypbpr`, which is the other bench source: nothing
    on the board can see whether anything is plugged into an input, so this
    needs the Wii present as well.
    """
    if gbs_unit.acquired_and_settled(host, limit_s=90.0):
        return True

    gbs_unit.select_input(host, "ypbpr")
    time.sleep(10.0)
    gbs_unit.select_input(host, "vga")
    time.sleep(SETTLE_SECONDS)
    return gbs_unit.acquired_and_settled(host, limit_s=90.0)


@pytest.mark.source_mode
def test_the_default_framing_puts_the_card_flush_to_the_emitted_edges(
        request, host, source, capture_device, judged_at, mode, csync):
    where = request.config.getoption("--modeserv")

    gbs_unit.set_sync_type(where, csync)
    reply = gbs_unit.mode_serv(where, f"MODE {mode}")
    assert gbs_unit.modeserv_ok(reply), f"the source refused {mode!r}: {reply!r}"

    # The arrangement is re-probed rather than trusted: establishSyncType()
    # keys on the input selection, so the engine follows a SYNC command only
    # once something makes it probe again. docs/sync-type-selection.md
    gbs_unit.get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)
    assert acquired_or_recovered(host), \
        "never acquired, or never stopped re-solving, even after an input bounce"
    # AFTER the mode change, which repaints the source's default pattern, and
    # with the animation off so one clip is evidence.
    assert gbs_unit.show_card(where), "the source would not draw the card"
    assert gbs_unit.reset_framing(host) is not None, "the framing never reset"

    # A clip rather than a frame: the card's outermost ring flashes, and in the
    # phase where it is yellow the green smears into it and falls under the hue
    # test, so one capture reports the edge missing.
    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, capture_device)
    found = {name: card_edges.judge(clip, axis, card_edges.ALLOWANCE,
                                    card_edges.source_pixel_px(mode, clip, axis))
             for name, axis in (("across", 1), ("down", 0))}

    whys = [f"{name}: {judged.why}" for name, judged in found.items()
            if judged.why]
    read = "  ".join(f"{name} {card_edges.reading(judged)}"
                     for name, judged in found.items())
    # The state the verdict was taken in, because a margin of hundreds of
    # columns is a SCALE rather than a placement and two things give one: a
    # divider left on another mode's value, and the declared shape coming back
    # under reset_framing(), whose bars are our own blanking.
    read += f"   {state_of(host)}"
    # EVERY CASE, not only the ones that fail. A verdict records nothing a later
    # run can be compared against, and the size is the whole point of out_by:
    # a case that passes at 2 and one that passes at 0 are a drift in progress.
    print(f"\n{mode} {'composite' if csync else 'separate'}: {read}")
    unstable = UNSTABLE.get((mode, csync))
    if unstable is not None:
        pytest.xfail(f"varies between runs -- {unstable}   [{read}]")
    known = NARROW.get((mode, csync))
    if known is not None:
        assert whys, f"known deficit {known} has closed -- take it out of NARROW"
        pytest.xfail(f"{known}: {'; '.join(whys)}   [{read}]")
    assert not whys, f"{'; '.join(whys)}   [{read}]"
