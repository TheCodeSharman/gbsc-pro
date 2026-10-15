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

**ACQUIRING IS HALF THE QUESTION. THE OTHER HALF IS THE PICTURE**, and a run
that asked only the first reported green on arrangements emitting a picture
short at two edges. Each leg is read off the USB capture as well: `PATTERN CARD`
draws a one-pixel green frame on the source's outermost pixels, so a framing is
right when that frame is flush to all four emitted edges with no black between.
`docs/bench-output-capture.md`, card_edges.py.

**WHAT ACCEPTANCE MEANS HERE**, on the VGA sources:

  the card's green border on all four emitted edges
  a stable acquisition, with no shake
  the same framing chosen on every acquisition of the same source
  no difference at all between composite and separate sync

**AT 1080p, AND SHAPING OFF.** The output is one variable every leg must share,
and lower resolutions have known issues and are out of scope. The shape is
turned OFF rather than set to 16:9: a declared shape narrows the room, the bars
it pillarboxes with are OUR blanking, and a strip taken at the frame's own edge
then measures them instead of the picture. `Aspect::Fill` is the absence of a
shape, so the picture takes the whole raster whatever the user has stored --
which cycling /uc?G to 16:9 cannot promise, the stored shape being per source.
"""

import os
import sys
import time

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_edges
import hdmi_capture
import picture_jitter
from gbs_unit import (
    SOURCES,
    acquired_and_settled,
    autosave,
    choose_output,
    get,
    get_json,
    mode_serv,
    read_fields,
    freeze,
    reset_framing,
    restore_output_mode,
    select_input,
    setting,
    show_card,
    wait_for_acquisition,
    write_named,
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

# One output for every leg, so the raster is not a variable between them.
OUTPUT = "1080p"

# What each arrangement's picture is currently known to get wrong, measured at
# 800x600@60 into 1080p at 16:9 with the framing reset. A leg here that PASSES
# is the gap having closed; one NOT here that fails is a regression.
#
# Both are a placement rather than a scale: separate sync fills the width and
# loses the source's first column off the left, composite sync carries both
# horizontal edges but inset, and cuts the source's first and last rows.
# docs/investigations/full-screen-framing-on-the-vesa-modes.md
DEFICITS = {
    # ACCEPTED rather than chased: it is one column at one edge, and it varies
    # between acquisitions of the same source -- 2 columns of margin with both
    # green edges on screen in one, the left column lost in another.
    "separate sync, scaled":
        "a column or two at the left, varying between acquisitions",
    # THE ONE THAT MATTERS. The capture window runs 155 samples past the end of
    # the source's picture, so what fills the right of the frame is the
    # source's own blanking, lifted out of the question with VDS_Y_OFST.
    # docs/investigations/the-composite-capture-window-is-too-wide.md
    "composite sync, scaled":
        "inset 20 at the left and 214 at the right, and the top and bottom "
        "rows are cut",
    # The open entry in docs/known-issues.md: it acquires, holds, and the card
    # is nowhere on the emitted frame.
    "composite sync, passed through":
        "neither edge on either axis -- it acquires and emits no picture",
}


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


def shaping(host):
    """Whether the solve is narrowing the room to the declared shape.

    **ASKED OF THE ENGINE, NOT OF THE PREFERENCE.** `apply-aspect` lags a toggle
    behind: measured with the engine shaping it reads 0, after one /uc?S the
    engine stops and it still reads 0, and after a second it reads 1 with the
    engine shaping again. docs/known-issues.md
    """
    return get_json(host, "/geometry")[1].get("shaping") is True


def want_shaping(host, on, tries=3):
    """Narrow the room to the declared shape, or do not. /uc?S toggles it."""
    for _ in range(tries):
        if shaping(host) == on:
            return True
        get(host, "/uc?S")
        time.sleep(2.0)
        acquired_and_settled(host)
    return shaping(host) == on


def show_the_default_framing(host, where, scaled):
    """The card up, and on the scaling route the framing the solve computes.

    A stored framing answers before the computed default, so the reset is what
    makes this the engine's own answer rather than the last session's tuning.
    The module turns autosave off, so nothing the run does is stored either.

    **PASS-THROUGH HAS NO FRAMING.** The source's own timing reaches the
    encoder there, so the reset has nothing to act on.
    """
    assert show_card(where), "the source would not draw the card"
    if not scaled:
        return
    assert reset_framing(host) is not None, "the framing never reset"
    assert acquired_and_settled(host), "the engine never stopped re-solving"


# What the Wii is judged by instead of the card. It draws whatever it draws, so
# the green frame is not available there -- what is, is whether the emitted
# picture reaches the frame's edges.
#
# **AND WHICH REGISTER SEPARATES THE TWO BLACKS DEPENDS ON THE ROUTE.** A
# source's own border is black ACTIVE video and ours is blanking, and a lit-box
# reading cannot tell them apart: the Wii's 480p carries black rows at the
# bottom that are in its signal. Scaled, VDS_Y_OFST raises the picture's luma,
# captured border with it, while the blanking interval keeps its value. Passed
# through, video routes around the VDS and that offset reaches nothing --
# HD_MATRIX_BYPS is the one there, which leaves the picture green and the
# source's border dark magenta at G exactly 0 while our blanking stays black on
# every channel.
FILLED_PX = 8
LIFT = 40

# Our blanking against a border that has been coloured rather than lifted, so
# the threshold is any channel rather than luma, and lower than hdmi_capture's
# because dark magenta is dark.
COLOURED = 8


def judge_the_lit_box(host, dev, passed_through):
    """Where the emitted picture stops, with the source's own border taken out
    of the question by whichever register the route leaves in circuit."""
    assert freeze(host, True), "the engine would not freeze for the lift"
    held = "HD_MATRIX_BYPS" if passed_through else "VDS_Y_OFST"
    try:
        if passed_through:
            assert write_named(host, held, 1) == 1, "the matrix stayed in"
        else:
            assert write_named(host, "VDS_DYN_BYPS", 0) == 0, "the lift was bypassed"
            assert write_named(host, held, LIFT) == LIFT, "the lift never landed"
        time.sleep(1.5)
        frame = hdmi_capture.frames(4, dev, warmup=20)[0]
    finally:
        write_named(host, held, 0)
        freeze(host, False)

    if passed_through:
        found = hdmi_capture.borders(frame.max(axis=2), threshold=COLOURED)
    else:
        found = hdmi_capture.borders(hdmi_capture.luma(frame))
    over = [f"{found[edge]} at the {edge}" for edge in
            ("left", "right", "top", "bottom") if found[edge] > FILLED_PX]
    return ([f"our blanking at the edges: {', '.join(over)}"] if over else [],
            f"borders {found}")


# What a steady picture reads, in emitted columns. A quiet band sits at 0.004
# to 0.022 and the bench floor is 0.05; a shaking one reads 2.1 to 4.0.
STEADY_PX = 0.25
BANDS = 6
BAND_SPAN = 120


def unsteady(clip):
    """Bands of the picture that MOVE between frames.

    **PER BAND, AND THE EDGE TEST CANNOT SEE THIS AT ALL.** The green frame is
    read at each column's greenest moment across the clip, so a wobbling edge
    still registers at its extreme -- and a whole-picture figure averages away
    a shake confined to the rows just after the vertical interval, which is
    where the one on composite sync lives.
    """
    grey = hdmi_capture.luma(clip)
    box = picture_jitter.lit_box(grey[0])
    if box is None:
        return ["all black, so nothing can be said about steadiness"]
    left, right, top, bottom = box
    moved = []
    for at in picture_jitter.band_starts(top, bottom, BANDS, BAND_SPAN):
        profiles = [frame[at:at + BAND_SPAN, left:right].mean(axis=0)
                    for frame in grey]
        gain = picture_jitter.gain_for(profiles[0])
        spread = float(np.std([picture_jitter.displacement(profiles[0], p)[0] / gain
                               for p in profiles]))
        if spread > STEADY_PX:
            moved.append(f"rows {at}..{at + BAND_SPAN} move by sd {spread:.2f} px")
    return moved


def judge_the_picture(host, dev, carries_card, passed_through):
    """What the emitted frame says about its four edges, and whether it is
    still between frames.

    A clip rather than a frame: the card's green frame is one source pixel wide
    and lands on a fraction of an output one, so each position's greenest
    moment across the clip is what finds it -- and steadiness needs a clip by
    its nature.
    """
    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, dev)
    if not carries_card:
        whys, margins = judge_the_lit_box(host, dev, passed_through)
        return whys + unsteady(clip), margins

    whys, margins = [], []
    for name, axis in (("across", 1), ("down", 0)):
        _, found, why = card_edges.judge(clip, axis, card_edges.FLUSH)
        margins.append(f"{name} {found if found else '-'}")
        if why:
            whys.append(f"{name}: {why}")
    return whys + unsteady(clip), "  ".join(margins)


@pytest.fixture(scope="module")
def capture_device():
    """The dongle's node, by USB id. A missing capture is not an acquisition
    fault, so the picture half skips where the acquisition half still runs."""
    try:
        return hdmi_capture.device()
    except SystemExit as why:
        pytest.skip(str(why))


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
        "shaping": shaping(host),
        "input": setting(host, "input"),
        "output": setting(host, "output"),
        "sync": mode_serv(where, "SYNC"),
    }

    assert choose_output(host, OUTPUT), (
        f"the output never reached {OUTPUT}, so the legs would be judged "
        f"against different rasters")
    assert want_shaping(host, False), (
        "the shape is still being applied, so every edge read would measure "
        "the bars it pillarboxes with")

    # NO STORED FRAMING MAY ANSWER FOR THE COMPUTED DEFAULT. A stored entry is
    # recalled on acquisition -- the console says `framing recalled` -- so what
    # a run judges would be the last session's tuning. Each leg forgets this
    # source's entry with /sc?B, and autosave is off so the run cannot write
    # one of its own in the meantime. It is runtime state with no key in the
    # preferences, so it goes back on at the end rather than being restored.
    assert autosave(host, False), "the framing table would not stop autosaving"

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
            if original["output"]:
                restore_output_mode(host, original["output"])
            if original["input"]:
                select_input(host, original["input"])
            want_passed_through(host, original["scale-rgbhv"] == "0")
            want_shaping(host, original["shaping"])
            autosave(host, True)
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


@pytest.mark.source_mode
@pytest.mark.parametrize("source,csync,passed_through,what", ARRANGEMENTS,
                         ids=[entry[3].replace(", ", "-").replace(" ", "-")
                              for entry in ARRANGEMENTS])
def test_the_picture_fills_the_frame(bench, host, capture_device, preset_save,
                                     source, csync, passed_through, what):
    where = bench
    if csync is not None:
        set_sync_type(where, csync)

    want_passed_through(host, passed_through)
    select_input(host, source)
    get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)

    took = wait_for_acquisition(host, source, limit_s=ROUTE_SECONDS + 40.0)
    assert took is not None, (
        f"{source} on {what} never acquired, so its picture cannot be judged")
    show_the_default_framing(host, where, not passed_through)

    whys, margins = judge_the_picture(host, capture_device, source == "vga",
                                      passed_through)
    known = DEFICITS.get(what)
    if known is not None:
        assert whys, (
            f"{what} no longer has the deficit recorded for it ({known}) -- "
            f"take it out of DEFICITS   [{margins}]")
        pytest.xfail(f"{known}: {'; '.join(whys)}   [{margins}]")
    assert not whys, f"{source} on {what}: {'; '.join(whys)}   [{margins}]"


# What the two sync types currently differ by, measured at 800x600@60 into
# 1080p at 16:9: the composite path counts four lines fewer, and every number
# downstream follows it -- the line rate, the divider, the capture, both scales
# and the framing. Remove these as the gap closes.
SYNC_TYPE_DEFICITS = {
    "count": "composite counts 623 against separate's 627",
    "picture": "37.5% of pixels differ, the count carried through the solve",
}

# How far apart two captures of the SAME state sit, as a fraction of pixels
# differing by more than this many levels. The control is shot each run rather
# than assumed, because a dongle capture is not noise-free and an absolute
# threshold would be a guess.
LEVELS = 16


def differing(first, second, levels=LEVELS):
    """The fraction of pixels the two frames disagree about."""
    apart = np.abs(first.astype(np.int16) - second.astype(np.int16))
    return float((apart.max(axis=2) > levels).mean())


def settled_on(host, where, csync):
    """The vga source acquired on `csync`, framed at the computed default."""
    set_sync_type(where, csync)
    select_input(host, "vga")
    get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)
    assert wait_for_acquisition(host, "vga", limit_s=ROUTE_SECONDS + 40.0), (
        f"vga never acquired on {'composite' if csync else 'separate'} sync")
    show_the_default_framing(host, where, True)


def shot(dev):
    """One frame of the emitted picture, the median of a clip so a single
    dropped or torn capture cannot be the reading."""
    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, dev)
    return np.median(clip.astype(np.float32), axis=0).astype(np.uint8)


@pytest.mark.source_mode
def test_one_source_counts_the_same_on_both_sync_types(bench, host, preset_save):
    """The sync type is how the source carries sync, not how many lines it has.

    This is the root of the picture difference below rather than a second
    symptom: the count sets the line rate, the line rate sets the divider, and
    the divider sets the capture, both scales and the framing.
    """
    where = bench
    want_passed_through(host, False)

    counted = {}
    for name, csync in (("separate", False), ("composite", True)):
        settled_on(host, where, csync)
        counted[name] = read_fields(host, PATH)

    apart = (counted["separate"]["STATUS_SYNC_PROC_VTOTAL"]
             - counted["composite"]["STATUS_SYNC_PROC_VTOTAL"])
    known = SYNC_TYPE_DEFICITS.get("count")
    if known is not None and apart:
        pytest.xfail(f"{known}: {apart} lines apart, {counted}")
    assert apart == 0, f"the same source counted two ways: {counted}"


@pytest.mark.source_mode
def test_one_source_looks_the_same_on_both_sync_types(bench, host, capture_device,
                                                      preset_save):
    """One source, one cable, one raster -- so one picture.

    The same state is shot twice as the control, so what the two sync types are
    judged against is this run's own capture noise rather than a constant.
    """
    where = bench
    want_passed_through(host, False)

    settled_on(host, where, False)
    separate, control = shot(capture_device), shot(capture_device)
    settled_on(host, where, True)
    composite = shot(capture_device)

    floor = differing(separate, control)
    apart = differing(separate, composite)
    said = (f"{apart:.1%} of pixels differ between the sync types, against "
            f"{floor:.1%} between two captures of the same one")
    known = SYNC_TYPE_DEFICITS.get("picture")
    if known is not None and apart > floor + 0.01:
        pytest.xfail(f"{known}: {said}")
    assert apart <= floor + 0.01, said


# How many times the same source is acquired when the question is whether the
# answer repeats. Two says whether it can differ at all; more would say how
# often, which is a survey rather than an acceptance.
ACQUISITIONS = 3


@pytest.mark.source_mode
def test_one_source_is_framed_the_same_on_every_acquisition(bench, host,
                                                            capture_device,
                                                            preset_save):
    """The default framing is computed from the source, so the same source
    acquired twice must come out the same.

    **BOTH HALVES, BECAUSE THEY FAIL SEPARATELY.** What the engine solved is in
    /geometry and what reached the screen is in the capture, and the emitted
    frame can move while every register holds: the sink's window is latched when
    the link acquires, downstream of the board.
    docs/investigations/the-shown-window-is-latched-at-lock.md
    """
    where = bench
    want_passed_through(host, False)

    solved, frames = [], []
    for _ in range(ACQUISITIONS):
        settled_on(host, where, False)
        got = get_json(host, "/geometry")[1]
        solved.append(tuple(got.get(key) for key in
                            ("ch", "cv", "oh", "eh", "ov", "ev")))
        frames.append(shot(capture_device))

    floor = differing(frames[0], shot(capture_device))
    apart = [differing(frames[0], later) for later in frames[1:]]
    said = (f"solved {solved}, emitted differ by "
            f"{['%.1f%%' % (v * 100) for v in apart]} against a {floor:.1%} "
            f"floor between two captures of one acquisition")

    assert len(set(solved)) == 1, f"the engine solved it two ways: {said}"
    assert max(apart) <= floor + 0.01, (
        f"the same solve reached the screen differently: {said}")
