"""Acceptance for the pass-through path, mode by mode.

Pass-through hands the source's own timing to the encoder, so a mode either
arrives intact or it does not -- there is no framing for a user to tune and
nothing to trade off. That makes it the one path with an unambiguous pass
mark, and this is it: for every mode the bench source can emit, the picture
reaches all four edges, the finest grating still resolves, and nothing moves
between frames.

Three questions, three instruments, because no one of them can see the others:

- **edges**, on `PATTERN CARD`, whose one-pixel green frame lands on the
  source's outermost row and column. Black at an edge is picture that was
  never sent; every other marking on the card stops short of the edge, so a
  measurement against one of those cannot tell a correct edge from a cropped
  one.
- **clarity**, on `PM5544`'s frequency wedge. Below two samples per source
  pixel the finest block sits above the sampler's Nyquist, and a sampling
  phase that lands badly collapses its contrast. A frame that is steady and
  correctly framed can still be mush here.
- **stability**, how far each pixel of that block moves across a clip. The
  card's animation is off, so a static source makes all of it the system's.

Captured at the dongle's native 1920x1080 whatever the source is emitting.
Asking for the source's own size gets a resampled frame, and the resampler's
own filtering then answers the clarity question.

`--modeserv` drives the source and `--source` says one is plugged in. The
run disturbs the picture and leaves the last mode up.
"""

import numpy as np
import pytest

from gbs_unit import read_fields, wait_for
from hdmi_capture import device, frames, luma

pytestmark = pytest.mark.source_mode

WIDTH, HEIGHT = 1920, 1080
FRAMES = 40

# x0, y0, x1, y1 as fractions of the lit picture: the LEFT third of PM5544's
# frequency wedge, which is the finest grating on the card. Fractions rather
# than pixels because the card scales with the mode while its layout does not.
FINE_WEDGE = (0.20, 0.54, 0.38, 0.64)

# Columns of black allowed at an edge before the picture is judged clipped.
# A correctly framed mode measures 0: 1920x1080 lit, margins 0/0, on every
# mode that was right on the bench. The tolerance is for the capture's own
# outermost column, not for missing picture.
EDGE_BLACK_TOLERANCE = 2

# Below limited-range black plus sensor noise, which `hdmi_capture` uses for
# the same question.
BLACK = 24

# A pixel is the card's green frame when green leads both other channels by
# this much. The frame is drawn at full green against a black surround, so the
# margin is wide; it exists to reject the grey border checks, not to be tight.
GREEN_LEAD = 40

# Grey levels of spatial spread the fine wedge must still carry. Measured at
# 1280x720@60, where the sampling phase moves it between 45.4 and 56.4 on one
# acquisition -- repeatable to 0.06 across repeats. A grating the sampler has
# aliased into flat grey collapses toward the capture's own noise, under 2.
WEDGE_CONTRAST_FLOOR = 25.0

# How far a pixel of the wedge may move across the clip, in grey levels,
# against the contrast it carries. The card's animation is off, so a static
# source makes every bit of this the system's.
#
# PROVISIONAL. Measured 0.084..0.115 across four sampling phases at
# 1280x720@60, and no state is yet measured that a reader would call unstable,
# so this bar is set to catch gross movement rather than at a boundary anyone
# has seen. Tighten it when a bad state has been measured.
WEDGE_MOVEMENT_CEILING = 0.20

# name, ModeServ request, the picture's own line count.
#
# NO FRAME TOTAL IS STATED HERE. The monitor definition loaded on the machine
# decides the raster, and `RetroScaler-Acorn.mdf` carries no VESA DMT set --
# so a total taken from DMT is a total the source does not emit, and a test
# waiting for it fails on a mode that acquired perfectly. The count is read
# back instead, and only bounded: a frame carries at least its own picture and
# not half as much again in blanking.
MODES = [
    ("640x480@60", "MODE X640 Y480 C256 F60", 480),
    ("800x600@60", "MODE X800 Y600 C256 F60", 600),
    ("1024x768@60", "MODE X1024 Y768 C256 F60", 768),
    ("1280x720@60", "MODE X1280 Y720 C256 F60", 720),
    ("1280x1024@60", "MODE X1280 Y1024 C256 F60", 1024),
]

# How many identical readings in a row count as settled, and the widest the
# frame may be against its picture before the count is not this mode's.
SETTLED_READINGS = 4
BLANKING_HEADROOM = 1.5


def _pattern(modeserv, name):
    modeserv(f"PATTERN {name}")
    # The cards animate by default -- PM5544 flips its corner squares and CARD
    # flashes its border -- so a feature caught in one phase and absent in the
    # other reads exactly like a crop.
    modeserv("ANIM OFF")


def _settle(host, modeserv, request, picture_lines):
    """Put the source in `request`, wait for the engine to count it, and return
    the count it settled on."""
    reply = modeserv(request)
    # VIDC20 with no field rate to report answers the right resolution while
    # driving no raster the scaler can acquire, and everything downstream of it
    # reads as a scaler fault.
    assert "F-1" not in reply, (
        f"the source read back {reply!r}: it is not driving a usable raster, so "
        f"nothing measured here would be about the scaler")

    # Poll the COUNT, not /geometry's state, which is stale across a change:
    # it answers `acquired` from the source that just went away while the
    # counts still read 0.
    seen = []

    def settled():
        seen.append(read_fields(host, ["STATUS_SYNC_PROC_VTOTAL"])
                    .get("STATUS_SYNC_PROC_VTOTAL"))
        run = seen[-SETTLED_READINGS:]
        return (len(run) == SETTLED_READINGS and run[0]
                and len(set(run)) == 1 and run[0] >= picture_lines)

    assert wait_for(settled, timeout=45.0, interval=0.5), (
        f"the sync processor never held a count of at least {picture_lines} "
        f"for {request!r}; it read {seen[-8:]}")

    count = seen[-1]
    assert count < picture_lines * BLANKING_HEADROOM, (
        f"{request!r} settled on {count} lines against a picture of "
        f"{picture_lines}: that is not this mode's frame")
    return count


def _capture(colour=False):
    taken = frames(FRAMES, device(), WIDTH, HEIGHT, warmup=12)
    return taken if colour else luma(taken)


def _wait_for_picture(timeout=40.0):
    """The dongle re-locks on its own schedule, seconds after the engine has
    finished. A clip taken before that is all black and reads as a dead board.

    It WAITS rather than skipping: a run that skipped here would be green
    without having looked at anything.
    """
    def lit():
        return float(luma(frames(2, device(), WIDTH, HEIGHT,
                                 warmup=12)).mean()) > BLACK

    assert wait_for(lit, timeout=timeout, interval=0.0), (
        f"the dongle delivered nothing but black for {timeout:.0f}s. Ask the "
        f"board before the connector -- hdmi_capture.py")


def _lit_box(grey):
    """Where the emitted picture starts and stops, as (left, right, top,
    bottom) inclusive."""
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), (
        "the capture is entirely black. Ask the board before the connector: "
        "s0_46's resets, then s0_45 and s0_49, then the console's frame time "
        "lock line. hdmi_capture.py")
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def _black_at_edges(grey):
    left, right, top, bottom = _lit_box(grey)
    return dict(left=left, right=WIDTH - 1 - right,
                top=top, bottom=HEIGHT - 1 - bottom)


def _assert_framed(name, grey):
    """The wedge is found at a fraction of the LIT picture, so a clipped mode
    puts the box over whatever took the picture's place. Neither the clarity
    nor the movement reading means anything until the framing is right, and a
    run that reported all three as separate faults would be counting one fault
    three times."""
    clipped = {k: v for k, v in _black_at_edges(grey).items()
               if v > EDGE_BLACK_TOLERANCE}
    assert not clipped, (
        f"{name}: black at {clipped}, so the wedge cannot be located and this "
        f"says nothing about the grating. The framing is the finding -- see "
        f"test_passthrough_reaches_every_edge.")


def _wedge(grey):
    """The fine wedge, as (contrast, movement).

    `contrast` is how much of the grating survived sampling: the spatial
    spread of the time-averaged block, in grey levels. `movement` is how much
    each pixel changes across the clip, which on a card with its animation off
    is the system's and nothing else's.

    NO CELL GRID. The first attempt scored the worst 64px cell, and a grating
    this fine is sensitive to where the cell boundaries fall on it -- two runs
    of the same states ranked the sampling phases in opposite orders purely by
    where the grid landed. Per pixel there is no grid to align.
    """
    left, right, top, bottom = _lit_box(grey[0])
    width, height = right - left, bottom - top
    block = grey[:,
                 top + int(FINE_WEDGE[1] * height):top + int(FINE_WEDGE[3] * height),
                 left + int(FINE_WEDGE[0] * width):left + int(FINE_WEDGE[2] * width)]
    block = block.astype(float)
    return float(block.mean(axis=0).std()), float(block.std(axis=0).mean())


def _green_frame_at_edges(rgb):
    """How far the card's green frame sits from each edge, in pixels."""
    red, green, blue = (rgb[:, :, 0].astype(int), rgb[:, :, 1].astype(int),
                        rgb[:, :, 2].astype(int))
    green_here = (green - np.maximum(red, blue)) > GREEN_LEAD

    def first(along):
        found = np.where(along.any(axis=1 if along.ndim > 1 else 0))[0]
        return int(found[0]) if len(found) else None

    left = first(green_here.T)
    right = first(green_here.T[::-1])
    top = first(green_here)
    bottom = first(green_here[::-1])
    return dict(left=left, right=right, top=top, bottom=bottom)


@pytest.fixture
def passing_through(host):
    """The path under test, asserted rather than assumed.

    Bypass and the scaling path are told apart by DAC_RGBS_BYPS2DAC and
    OUT_SYNC_SEL, both 1 in bypass. The scale registers are not the tell:
    bypass leaves them on the last scaled load's values.
    """
    def check(where):
        path = read_fields(host, ["DAC_RGBS_BYPS2DAC", "OUT_SYNC_SEL"])
        assert path["DAC_RGBS_BYPS2DAC"] == 1 and path["OUT_SYNC_SEL"] == 1, (
            f"{where} is on the scaling path ({path}), so this says nothing "
            f"about pass-through. Turn preferScalingRgbhv off with /uc?x.")
    return check


@pytest.mark.parametrize("name,request_,picture_lines", MODES, ids=[m[0] for m in MODES])
def test_passthrough_reaches_every_edge(host, source, modeserv, passing_through,
                                        name, request_, picture_lines):
    """The card's green frame lands on all four edges, with no black outside it."""
    _settle(host, modeserv, request_, picture_lines)
    passing_through(name)
    _wait_for_picture()
    _pattern(modeserv, "CARD")

    rgb = _capture(colour=True)[0]
    black = _black_at_edges(luma(rgb[None, ...])[0])
    edges = _green_frame_at_edges(rgb)

    clipped = {k: v for k, v in black.items() if v > EDGE_BLACK_TOLERANCE}
    assert not clipped, (
        f"{name}: black at {clipped} (all edges {black}). Pass-through emits "
        f"the source's own raster, so black at an edge is picture that was "
        f"never sent. The card's green frame sits at {edges}.")

    missing = [k for k, v in edges.items() if v is None]
    assert not missing, (
        f"{name}: the card's green frame never appears at {missing} -- "
        f"the source's outermost row or column is not arriving. "
        f"Black margins read {black}.")


@pytest.mark.parametrize("name,request_,picture_lines", MODES, ids=[m[0] for m in MODES])
def test_passthrough_resolves_the_finest_grating(host, source, modeserv,
                                                 passing_through, name,
                                                 request_, picture_lines):
    """PM5544's finest wedge block still carries contrast."""
    _settle(host, modeserv, request_, picture_lines)
    passing_through(name)
    _wait_for_picture()
    _pattern(modeserv, "PM5544")

    grey = _capture()
    _assert_framed(name, grey[0])
    contrast, _ = _wedge(grey)
    assert contrast >= WEDGE_CONTRAST_FLOOR, (
        f"{name}: the fine wedge carries {contrast:.2f} grey levels of "
        f"contrast, under {WEDGE_CONTRAST_FLOOR}. The grating has been aliased "
        f"into flat grey -- below two samples per source pixel it sits above "
        f"the sampler's Nyquist, and the sampling phase decides what survives.")


@pytest.mark.parametrize("name,request_,picture_lines", MODES, ids=[m[0] for m in MODES])
def test_passthrough_holds_the_grating_still(host, source, modeserv,
                                             passing_through, name,
                                             request_, picture_lines):
    """The wedge's contrast does not breathe between frames."""
    _settle(host, modeserv, request_, picture_lines)
    passing_through(name)
    _wait_for_picture()
    _pattern(modeserv, "PM5544")

    grey = _capture()
    _assert_framed(name, grey[0])
    contrast, movement = _wedge(grey)
    assert contrast > 0, f"{name}: the fine wedge is flat, so nothing can be judged"
    ratio = movement / contrast

    assert ratio <= WEDGE_MOVEMENT_CEILING, (
        f"{name}: the fine wedge moves {movement:.2f} grey levels against "
        f"{contrast:.2f} of contrast ({ratio:.4f}), over "
        f"{WEDGE_MOVEMENT_CEILING}. The card's animation is off, so none of "
        f"that is the source's.")
