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
interactively, so there is one statement of what flush means.

**IT ASSERTS THE GOAL AND IT FAILS UNTIL THE GOAL IS MET.** Every edge of every
case carries green, and no edge carries black between that green and the frame's
own edge. A list of known deficits that granted an xfail is what let the
composite vertical go from whole at both ends to a tenth of its top border
without one case turning red -- the entries were prose nothing compared against,
so a regression INSIDE a known deficit was invisible. The deficits are recorded
in `docs/known-issues.md`, where they cannot pass a test, and every case prints
its four edges so a drift shows up as a size rather than as a verdict.

**AT ONE OUTPUT ASKED FOR, AT 16:9, ON BOTH SYNC TYPES.** The output is a
variable every case has to share, or the modes are judged against different
rasters. And the sync type is a dimension of its own: a mode judged on one of
them is a framing fitted to one half of the bench, which is why every mode runs
on separate sync and on composite.

**BUT THE OUTPUT ASKED FOR IS NOT ALWAYS THE ONE EMITTED, AND A CASE IS JUDGED
AT WHAT IT EMITS.** The MS9288A cannot carry 1080p above about 66 Hz -- its
raster at 70 Hz wants 173.25 MHz against a 165 MHz ceiling -- so a 70 Hz source
falls back to 1024p and no preference records it: `setting(host, "output")`
still reads 1920x1080 while the frame is 1066 lines. Each case therefore reads
the mode off VDS_VSYNC_RST and captures at ITS emitted size, and prints that
size beside the reading.

**THE DONGLE RESCALES RATHER THAN REFUSING**, which is what makes that worth
doing: a 1024p frame asked for at 1920x1080 comes back as a plausible 1905x1076
picture with every absolute figure 1.5x and ALLOWANCE, stated in capture
columns, tightened by the same factor. Measured both ways on one state, the far
margin reads 9 natively and 14 upscaled, and a one-row margin smears from 1,3
to 2,2. docs/investigations/the-encoder-ceiling-is-the-raster-floor.md

**16:9 NARROWS NOTHING AT 1080p, SO IT IS NOT THE 4:3 TRAP.** A declared shape
narrows the room and the bars it pillarboxes with are OUR blanking, so a strip
taken at the frame's own edge measures them instead of the picture -- which is
what 4:3 does, reading 258 columns at the near edge against 26. 1920/1080 IS
16:9, so declaring it leaves the room whole: measured on one state, shape off
and 16:9 give the same four edges to the pixel, `across 0.86|0.00 down 3|1
0.89|0.89`, at one `oh`, `eh` and `ch`. So the shape the bench runs in is the
shape the acceptance is taken in, and no reading here is of our own bars.

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
import framing_decomposition
import gbs_unit
import hdmi_capture
import picture_stability

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

# How the cases read: the mode, then which sync type carried it. Every mode runs
# on both, because the two place the picture differently and a mode judged on one
# is a framing fitted to half the bench.
SYNC_TYPES = ((False, "separate"), (True, "composite"))


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
    """One output and one declared shape for every case, and the bench put back.

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
    assert gbs_unit.want_shaping(host, True), (
        "the shape is not being applied, so a case would be judged in a shape "
        "the bench does not run in")
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
    # The WINDOW as well as the capturable region, because a displacement read
    # off the emitted frame is only a capture displacement once it is divided by
    # the magnification, and the window's extent is what sets that.
    return (f"MD {divider} shaping {geo.get('shaping')} "
            f"aspect {geo.get('aspect')} ch {geo.get('ch')} cv {geo.get('cv')} "
            f"oh {geo.get('oh')} eh {geo.get('eh')} "
            f"ov {geo.get('ov')} ev {geo.get('ev')}")


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
    # AFTER the reset, which puts the declared shape back to the raster's
    # default along with the framing -- 4:3 on every mode here, whose bars are
    # our own blanking and read as 258 columns at the near edge.
    assert gbs_unit.choose_aspect(host, gbs_unit.SIXTEEN_NINE), (
        "the shape never reached 16:9, so the edges read would be the bars a "
        "4:3 declaration pillarboxes with")

    # AT THE FRAME THE ENGINE ACTUALLY EMITS, which is not always the one the
    # module asked for: the encoder cannot carry 1080p above about 66 Hz, so a
    # 70 Hz source falls back to 1024p and the registers are the only record of
    # it. docs/investigations/the-encoder-ceiling-is-the-raster-floor.md
    emitted = framing_decomposition.carried_frame(
        gbs_unit.read_fields(host, ["VDS_VSYNC_RST"]))
    assert emitted is not None, "the registers carry no output mode this knows"

    # A clip rather than a frame: the card's outermost ring flashes, and in the
    # phase where it is yellow the green smears into it and falls under the hue
    # test, so one capture reports the edge missing.
    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, capture_device,
                               width=emitted[0], height=emitted[1])
    # **AN UNSTABLE PICTURE IS NOT A FRAMING, AND card_edges CANNOT SAY SO.** It
    # reduces the clip to its greenest frame, which is what the flashing ring
    # needs and what collapses a rolling picture into a plausible set of edges.
    # The roll is judged first, on the clip already taken, so a case that cannot
    # be framed reports that rather than a size. Vertical displacement only --
    # picture_jitter.py is what resolves a sub-pixel shimmer, and the share
    # stands without a span this clip cannot supply.
    held = picture_stability.judge(picture_stability.row_luma(clip), None)
    found = {name: card_edges.judge(clip, axis, card_edges.ALLOWANCE,
                                    card_edges.source_pixel_px(mode, clip, axis))
             for name, axis in (("across", 1), ("down", 0))}

    whys = [f"{name}: {judged.why}" for name, judged in found.items()
            if judged.why]
    if held.why:
        whys.insert(0, f"not holding still: {held.why}")
    read = "  ".join(f"{name} {card_edges.reading(judged)}"
                     for name, judged in found.items())
    # The state the verdict was taken in, because a margin of hundreds of
    # columns is a SCALE rather than a placement and two things give one: a
    # divider left on another mode's value, and the declared shape coming back
    # under reset_framing(), whose bars are our own blanking.
    read += (f"   still {held.displaced}+{held.lost}/{held.frames} "
             f"worst {held.worst}")
    read += f"   {emitted[0]}x{emitted[1]} {state_of(host)}"
    # EVERY CASE, not only the ones that fail. A verdict records nothing a later
    # run can be compared against, and the size is the whole point of out_by:
    # a case that passes at 2 and one that passes at 0 are a drift in progress.
    print(f"\n{mode} {'composite' if csync else 'separate'}: {read}")
    # THE GOAL, asserted. No list of known deficits grants a pass here: one is
    # what let a case keep its xfail while an edge of it got worse.
    # docs/known-issues.md carries each case's sizes.
    assert not whys, f"{'; '.join(whys)}   [{read}]"
