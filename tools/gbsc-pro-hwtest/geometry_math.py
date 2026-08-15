#!/usr/bin/env python3
"""The scaler's horizontal geometry, in both directions.

`geometry.py` answers "what is this register set doing?". This module also
answers "what registers produce the picture I want?", which is what an auto-detect
path needs. Both directions live here so they cannot disagree about a constant or
a formula -- the diagnostic tool and the generator must agree, or one of them is
lying about the hardware.

The arithmetic, in one place:

    magnification = 1024 / VDS_HSCALE          (VDS_HSCALE_BYPS means 1:1)
    produced      = capture_units x magnification
    headroom      = memory_window - produced

Everything else is rearrangement. `capture_units` are IF units
(IF_HB_ST2 - IF_HB_SP2); `produced`, the windows and the line are output pixels.

No hardware, no I/O: pure arithmetic so it can be tested against the recorded
snapshots without a unit on the bench.
"""

import math

# VDS_HSCALE is a divisor of 1024, so smaller means more magnification. The
# register is 10 bits: 1023 is the practical floor at x1.001, 256 the ceiling at
# x4.
HSCALE_UNITY = 1024
# **DERIVED, NOT SWEPT.** The floor is the unity divisor over how far the axis
# will magnify, in output pixels per captured ADC sample:
#
#     scale_min = HSCALE_UNITY / max magnification
#
# RD-5725-1.1 states NO minimum for VDS_HSCALE. regdef.txt gives only the ratio
# -- HSCALE = 1024 x input resolution / output resolution -- and the field is 10
# bits, so the part accepts 1..1023 and there is no hardware floor to derive.
#
# **A FIXED FLOOR COLLAPSES THE ZOOM WHEN THE RASTER WIDENS.** The clamp is
# raster / max magnification while the DEFAULT capture is a property of the input
# line alone, so the two do not track: a floor of 500 leaves 307 units of travel
# at a 1436 raster and 73 at 1916, which clamps before the picture reaches full
# screen. Deriving it from the magnification keeps the floor at raster / 4.
#
# Where interpolation starts to look bad is perceptual and cannot be computed, so
# it is the USER'S to find by zooming.
MAX_MAGNIFICATION = 4
HSCALE_MIN = HSCALE_UNITY // MAX_MAGNIFICATION
VSCALE_MIN = HSCALE_UNITY // MAX_MAGNIFICATION
HSCALE_MAX = 1023

# The product fitting inside the memory window is NOT sufficient. The scaler has
# to finish reading the line out of memory before the line period ends, so it
# needs slack -- and with too little, the picture tears exactly as it does on a
# real overflow.
#
# **HOW MUCH SLACK IS NOT KNOWN, AND THIS MODULE DOES NOT GUESS.** The two
# measurements that survive scrutiny, both at PLLAD_MD 2553 on a 1445 px output
# line, capture held at 798 units, moving only VDS_HB_ST to the edge:
#
#   HSCALE 1023 (x1.001) -> produced  798.78, edge at VDS_HB_ST  881   33.2 px
#   HSCALE  850 (x1.205) -> produced  961.36, edge at VDS_HB_ST 1095  ~84.6 px
#
# The requirement is NOT monotonic in HSCALE and the corruption comes in multiple
# stable bands, so an edge found by creeping down is only the true edge if you
# creep past all of them. More measurement does not fix that.
#
# **ANY HEADROOM MEASUREMENT IS WORTHLESS UNLESS THE DISPLAY WINDOW CONTAINS THE
# WHOLE PICTURE.** Tearing shows worst at the right of the line, so a window that
# blanks the right of the picture hides its own evidence.
#
# So: do not target a margin, take all of it. The origin pins VDS_?B_SP, leaving
# VDS_?B_ST free -- push it to the last value below the raster total and accept
# whatever headroom results. This is only a floor to warn below.
HEADROOM_WARN_PX = 100


# --- where the picture lands --------------------------------------------------

# The first written pixel sits this far after VDS_?B_SP -- the near edge of the
# frame buffer, not of the display window:
#
#     write start = VDS_?B_SP + START_CONST + START_PER_MAG x magnification
#
# 25 output px per unit magnification is 25 input samples, the run-up an 11-tap
# horizontal filter consumes before it writes (IF_HS_TAP11_BYPS); the vertical
# costs ~2 lines of line buffer. Both are pipeline latency at the start.
# docs/scaler-geometry-model.md, docs/investigations/moving-write-origin.md
START_CONST_H = 55.0
START_PER_MAG_H = 25.0
START_CONST_V = 0.2
START_PER_MAG_V = 0.8

# What one display shows, measured on the bench TV 2026-08-06 with the picture
# deliberately overrunning the raster on all four sides, so every reading had
# live video either side of the decision:
#
#     horizontal    90 .. 1351  of 1445    margins  90 / 94    span 1261
#     vertical      41 .. 1121  of 1126    margins  41 /  5    span 1080
#
# THESE DO NOT PLACE THE PICTURE, and must not. Where a display stops showing is
# a property of the display; place_picture centres on the raster and the user
# finds their own edges with pan and scale.
#
# The two axes are different kinds of limit. 1121 - 41 = 1080 exactly, so the
# vertical visible region is the active lines of a 1080-line output and is
# DERIVABLE from the raster -- any display fed the same raster shows the same
# lines. The horizontal is real overscan, this TV discarding 6% of the line, and
# the next TV discards something else. docs/scaler-geometry-model.md
PANEL_VISIBLE_LEFT = 90
PANEL_VISIBLE_RIGHT = 1351
PANEL_VISIBLE_TOP = 41
PANEL_VISIBLE_BOTTOM = 1121


class Axis:
    """What differs between the two axes. The arithmetic does not.

    `capture` is in IF units horizontally and HALF-LINES vertically -- the unit
    difference is absorbed by the scale value, so the formula is the same, but it
    is why a vertical capture number looks half the size you expect.
    """

    def __init__(self, name, start_const, start_per_mag, warn_px,
                 visible_edge, visible_far, registers, margin, window_sp_min,
                 scale_min):
        self.name = name
        self.start_const = start_const
        self.start_per_mag = start_per_mag
        self.warn_px = warn_px
        self.visible_edge = visible_edge
        self.visible_far = visible_far
        self.registers = registers
        self.margin = margin
        self.window_sp_min = window_sp_min
        # How far this axis will magnify -- see HSCALE_MIN / VSCALE_MIN.
        self.scale_min = scale_min

    def origin_offset(self, magnification):
        """How far after VDS_?B_SP the scaler starts writing, at this
        magnification. Not a constant -- see START_PER_MAG_H."""
        return self.start_const + self.start_per_mag * magnification

    def visible_span(self, raster_total):
        """How much of the raster the bench display shows. A hint for the
        "fills screen" indicator, nothing more -- it is one display's overscan.
        The far edge is MEASURED, not assumed symmetric with the near one: 90 and
        94 are close, 41 and 5 are not."""
        return self.visible_far - self.visible_edge


# `margin` trims the display window's far edge. produced reproduces every far
# edge measured to within 1.01 px and 1.71 lines, and the two costs of being
# wrong are not symmetric: a window 2 px short loses 2 px of picture nobody can
# see, while a window 2 px long shows scratch. So each axis gives back its own
# worst residual, rounded up -- the measured uncertainty, spent in the safe
# direction.
AXIS_H = Axis(
    "horizontal", START_CONST_H, START_PER_MAG_H,
    HEADROOM_WARN_PX, PANEL_VISIBLE_LEFT, PANEL_VISIBLE_RIGHT,
    ("VDS_HB_SP", "VDS_HB_ST", "VDS_DIS_HB_SP", "VDS_DIS_HB_ST"),
    margin=2,
    # Below VDS_HB_SP 8 the display corrupts -- measured on the bench 2026-08-06
    # by creeping it down until the picture broke. A hard floor, and it costs 8 px
    # on top of the write offset, so the leftmost the picture can start is
    # 8 + 55 + 25 x m. Against this panel's left edge at 90 that means the left of
    # the screen is only reachable below about x1.08.
    #
    # Whether 8 is absolute or relative to the output hsync is not established.
    # VDS_HS_ST is 10, and 10 - 2 = 8, which may be the whole story or may be a
    # coincidence; moving VDS_HS_ST and seeing whether the floor follows would
    # settle it in one creep.
    window_sp_min=8,
    scale_min=HSCALE_MIN,
)

# No vertical margin: a settled state at -1.9 lines was clean, and the horizontal
# rule is a line-time constraint with no per-frame equivalent. Tentative -- that
# state's display window was clipping, the same flaw that invalidated the
# horizontal evidence, and the VSCALE +1 ambiguity is worth 1.2 lines here.
AXIS_V = Axis(
    "vertical", START_CONST_V, START_PER_MAG_V,
    0, PANEL_VISIBLE_TOP, PANEL_VISIBLE_BOTTOM,
    ("VDS_VB_SP", "VDS_VB_ST", "VDS_DIS_VB_SP", "VDS_DIS_VB_ST"),
    margin=3,
    # UNMEASURED. The horizontal floor of 8 was found by creeping until the
    # picture broke; nobody has done the vertical. 0 is what the code assumed
    # before, so it is no worse than the status quo -- but it is an assumption,
    # not a measurement, and it should not be read as one.
    window_sp_min=0,
    scale_min=VSCALE_MIN,
)


# --- reading a register set --------------------------------------------------


def magnification(hscale, bypassed=False):
    """How much the scaler stretches a captured line. None if HSCALE reads 0,
    which is a dropped read rather than a legal setting -- the caller has to
    decide what to do about that, because silently treating it as 1:1 produces a
    plausible-looking width that is wrong."""
    if bypassed:
        return 1.0
    if not hscale:
        return None
    return HSCALE_UNITY / hscale


def produced_px(capture_units, hscale, bypassed=False, axis=None):
    """Output pixels the scaler makes from a capture window of this width.

        produced = capture x 1024 / scale

    It is a simple multiply, on both axes, with no loss term at either end.

    Measuring it from a corner taken to be CONSTANT makes it look lossy, with a
    deficit that changes sign with magnification -- ~14 px short at 1:1, ~40 px
    long at x3.2. The corner is not constant: the scaler starts writing
    START_CONST + START_PER_MAG x m after VDS_?B_SP.
    docs/investigations/moving-write-origin.md

    Confirmed against all eleven far-edge readings, four magnifications per axis,
    captures 200..798, to within 1.01 px and 1.71 lines -- readings older than
    the origin measurement that explains them, so this is not circular.
    """
    factor = magnification(hscale, bypassed)
    if factor is None:
        return None
    return capture_units * factor


def blanking_each_end(axis, active_start=0):
    """What must stay blank at BOTH ends of the raster, in output units.

    Two floors, and the picture clears whichever is higher:

    THE WRITE FLOOR is physical. The scaler cannot begin writing before
    `window_sp_min + start_const`, so nothing can be placed there whatever the
    timing says.

    THE BACK PORCH is the output raster's own, `sync + back porch` from the
    standard -- see OutputRaster.h. It is reserved at the FAR end too: that
    guarantees a front porch rather than assuming the encoder tolerates none, it
    is derived rather than measured off one display, and it lands within 30 px of
    the 110..2189 state confirmed working on the bench at a 2301 raster.

    Passing 0 -- the default -- reserves the write floor alone, with the picture
    centred on the whole raster.
    """
    return max(axis.window_sp_min + axis.start_const, active_start)


def max_display_window(raster_total, axis, active_start=0):
    """The biggest picture this raster can hold, before magnification is known.

    DEFINED, not read. Nothing here consults VDS_?B_SP or any other register that
    happens to be set: inheriting the current geometry is how a picture got frozen
    at 620 lines that no zoom step could grow.

    The bound applies at BOTH ends, because the picture is centred. A picture run
    from the floor to the far rail is bigger, but it cannot be centred -- tried on
    2026-08-06, it gave margins of 105 and 5 and hung 89 px of picture off the
    right of the screen. Equal margins mean whatever overscans is lost equally,
    which is the only kind of loss a user can reason about.
    """
    return (raster_total - 2) - 2 * blanking_each_end(axis, active_start)


def fit_to_raster(capture, raster_total, axis, unity=HSCALE_UNITY, active_start=0):
    """(scale, produced) making the picture as big as the raster allows.

    The picture is always as large as it can be, and the scale is whatever maps
    this capture onto it. So the user's control is the capture alone -- crop
    harder and the scale takes up the slack, crop less and it gives it back --
    and the picture never changes size underneath them.

    The magnification term makes this implicit -- the write offset grows with
    magnification, which itself depends on the size we are solving for -- so it is
    solved rather than iterated:

        produced = room - START_PER_MAG x produced / capture
                 = room x capture / (capture + START_PER_MAG)

    A capture too small to fill the raster is bounded by the scale register
    instead, at x4. That is a limit, not a failure.
    """
    room = max_display_window(raster_total, axis, active_start)
    if capture <= 0 or room <= 0:
        raise ValueError("nothing to fit")

    # Centred, so the magnification term costs at both ends:
    #     produced = room - 2 x START_PER_MAG x produced / capture
    produced = room * capture / (capture + 2 * axis.start_per_mag)
    scale = min(max(round(unity * capture / produced), axis.scale_min),
                HSCALE_MAX)
    produced = capture * unity / scale

    # Rounding the scale down makes the picture a shade larger than solved for,
    # which can push the far edge past the last usable value. Give one step back
    # rather than overflow -- a pixel of unused raster costs nothing, an overflow
    # shows scratch.
    while (scale < HSCALE_MAX
           and round((raster_total - produced) / 2)
           < max(axis.window_sp_min + axis.origin_offset(unity / scale),
                 active_start)):
        scale += 1
        produced = capture * unity / scale
    return scale, produced


def place_picture(produced, raster_total, magnification, axis, active_start=0):
    """(corner, window_sp) centring the picture on the output raster.

    CENTRED RATHER THAN PINNED TO A PANEL EDGE. Where a display stops showing is
    a property of that display: PANEL_VISIBLE_LEFT was carried here as 127 and
    measured 90 on the bench TV, and the next panel will differ again. A scaler
    cannot know it and should not pretend to. Centring needs no such number, and
    it leaves the user free to find their own usable edges with pan and scale --
    which is also the only way anyone has ever actually found them.

    THE CORNER IS NOT A CONSTANT EITHER. The scaler starts writing
    `origin_offset(m)` after VDS_?B_SP, so the memory window has to move to put
    the picture where it belongs. Holding a corner fixed while the scale changed
    is what put 41 px of last frame's contents down the left of the screen on
    2026-08-05.

    VDS_?B_SP cannot go negative, so a picture too big to centre starts at the
    write floor instead and overscans off the far end. Horizontally that floor is
    55 + 25 x m -- at x4 nothing can be placed before output pixel 155.
    """
    offset = axis.origin_offset(magnification)
    corner = round((raster_total - produced) / 2)
    window_sp = round(corner - offset)
    if window_sp < axis.window_sp_min:
        window_sp = axis.window_sp_min
        corner = round(window_sp + offset)

    # The back porch is applied on top of the write floor rather than folded into
    # one `max()` with it. The two forms are equivalent -- measured, with zero
    # disagreements over both axes, six rasters, six active starts, hundreds of
    # picture widths and eight magnifications -- so this is a readability choice:
    # `active_start=0` is the write floor alone, by construction.
    if corner < active_start:
        corner = active_start
        window_sp = max(round(corner - offset), axis.window_sp_min)
    return corner, window_sp


def headroom_px(memory_window_px, produced):
    """Slack between what the scaler produces and the memory window it reads
    from. This is the quantity that governs tearing, not the ratio."""
    return memory_window_px - produced


def is_safe(memory_window_px, produced, minimum=HEADROOM_WARN_PX):
    """Whether this window clears the warn floor. Not a measured boundary -- see
    HEADROOM_WARN_PX for why there isn't one."""
    return headroom_px(memory_window_px, produced) >= minimum


def covers_panel(produced, raster_total, axis):
    """Whether the picture reaches the edges of what the TV actually shows.

    Full screen is covering the visible region, not filling the raster: the
    panel's corner is 254 px in horizontally, so a picture can fill the screen
    while leaving a quarter of the raster unused as overscan.
    """
    return produced >= axis.visible_span(raster_total)


# --- choosing a register set -------------------------------------------------


def hscale_for(capture_units, target_active_px):
    """The VDS_HSCALE that maps this capture onto this output width, unclamped
    and unrounded checks left to the caller."""
    if target_active_px <= 0:
        raise ValueError("target_active_px must be positive")
    return HSCALE_UNITY * capture_units / target_active_px


def smallest_safe_hscale(capture_units, memory_window_px, headroom=HEADROOM_WARN_PX):
    """The least magnification-heavy HSCALE whose product still leaves `headroom`
    inside this memory window. Larger HSCALE means a smaller picture, so this is
    the *smallest* value that is safe. None if the window cannot hold anything."""
    usable = memory_window_px - headroom
    if usable <= 0:
        return None
    return math.ceil(HSCALE_UNITY * capture_units / usable)


def pan_capture(sp, st, delta, wrap_at):
    """Shift a capture window without changing how wide it is.

    Panning chooses which part of the source is grabbed; the width is what
    `produced` is computed from, so a pan that changed it would resize the
    picture as a side effect. Clamps rather than wraps: IF_VB_ST rolls at 624 and
    IF_HB_ST2 at IF_HSYNC_RST+1, and crossing either makes the picture jump --
    which has been misread as losing the capture window.
    """
    width = st - sp
    if width < 0:
        raise ValueError("capture stop is before its start")
    lowest, highest = 0, wrap_at - 1 - width
    if highest < lowest:
        raise ValueError("capture is wider than the space it must wrap inside")
    start = min(max(sp + delta, lowest), highest)
    return start, start + width


def zoom_capture(sp, st, max_produced, step, wrap_at=None, unity=HSCALE_UNITY):
    """Zoom past the scale ceiling by capturing less of the source.

    Once `produced` fills the memory window the scaler cannot magnify further --
    asking for more overflows and shreds the picture. But capturing LESS source
    and scaling it to the same output size is still a zoom, and it keeps the
    output pinned at the ceiling instead of overrunning it.

    Trims symmetrically so the centre of the picture stays put, and returns the
    scale that refills the window.

    A NEGATIVE step widens instead, which is how a zoom is undone -- at the
    ceiling the scale is pinned, so the capture is the only thing left to move.
    `wrap_at` bounds that: IF_VB_ST rolls at 624 and IF_HB_ST2 at IF_HSYNC_RST+1,
    and crossing either makes the picture jump.

    The refill scale is a CEILING, never a round: rounding down produces a
    fraction more than the window holds, which is the overflow this exists to
    avoid.
    """
    width = st - sp
    new_width = width - step
    if new_width <= 0:
        raise ValueError(f"a {step} unit step would leave nothing of a {width} "
                         f"unit capture")
    # Split the magnitude, then re-sign it. `step // 2` floors, so a trim of 11
    # takes 5 off one edge and a widen of 11 puts 6 back on it -- and the capture
    # walks a unit sideways on every round trip.
    near = abs(step) // 2
    far = abs(step) - near
    direction = 1 if step > 0 else -1
    new_sp, new_st = sp + direction * near, st - direction * far
    if wrap_at is not None:
        new_sp = max(0, new_sp)
        new_st = min(new_st, wrap_at - 1)
        new_width = new_st - new_sp
        if new_width <= 0:
            raise ValueError("widening left no capture inside the line")
    scale = min(max(math.ceil(new_width * unity / max_produced),
                    HSCALE_MIN), HSCALE_MAX)
    return new_sp, new_st, scale


def probe_park(expected_start, visible_edge, band=60):
    """Where to park the display window's near edge before creeping it up.

    The measurement is: park before where the scaler is expected to start
    writing, so a band of frozen scratch shows between the display edge and the
    picture, then creep the edge up until the band goes. That value is the write
    start.

    It only works if the parked edge is on screen. PANEL_VISIBLE_LEFT is 127 and
    CORNER_H is 129, so at the bench's usual placement there are two pixels to
    park in, and a band there vanishes at the bezel rather than at the write
    start.

    That may already have happened. ORIGIN_OFFSET_H's two irreconcilable values
    give 49 + 78 = 127 and 35 + 94 = 129 -- the corner barely moved while
    VDS_HB_SP moved 14, which is what measuring one fixed screen edge twice
    would look like. Unconfirmed, and measure_origin.py is what settles it; the
    refusal below is not evidence for it, only insurance against repeating it.

    So this refuses instead of returning an unreadable park. Move the picture
    right -- raise VDS_?B_SP -- and ask again.
    """
    park = expected_start - band
    if park <= visible_edge:
        raise ValueError(
            f"parking at {park} falls off the left of what the panel shows "
            f"({visible_edge}), so the band would vanish at the bezel rather "
            f"than at the write start -- move the picture right first")
    return park


def fit_line(xs, ys):
    """Least squares. Returns (slope, intercept) for y = slope x + intercept.

    Every geometry question that has been asked on this bench reduces to one of
    these -- is `produced` linear in magnification, does the write start move
    with the memory window -- so the fitting lives in one place and the meaning
    lives with the caller.

    Raises if every x is the same value: slope and intercept are then not
    separable, and a fit that cannot fail has not tested anything.
    """
    if len(xs) < 2:
        raise ValueError("need at least two measurements")
    if len(set(round(x, 9) for x in xs)) < 2:
        raise ValueError("every point is at the same x, so the slope and the "
                         "intercept cannot be separated -- vary it")
    n = len(xs)
    sx, sy = sum(xs), sum(ys)
    denominator = n * sum(x * x for x in xs) - sx * sx
    slope = (n * sum(x * y for x, y in zip(xs, ys)) - sx * sy) / denominator
    return slope, (sy - slope * sx) / n


def line_residuals(xs, ys, slope, intercept):
    """How far each measurement sits off the fitted line, in y's units."""
    return [y - (slope * x + intercept) for x, y in zip(xs, ys)]


def fit_loss(points):
    """Fit the two loss terms to measurements of `produced`.

    `points` is [(capture, scale, produced)]. Returns (c, k) where

        produced = (capture - c) x 1024 / scale - k

    c is in the axis's input units, k in output units. The fit is a straight
    line, because the deficit is linear in magnification:

        capture x m - produced = c x m + k        with m = 1024/scale

    so the slope is c and the intercept is k.

    TWO POINTS ARE NOT EVIDENCE. Any two land exactly on some line, so a
    two-point fit cannot disconfirm the model -- and three separate readings this
    evening looked confirmed on exactly that basis and were later refuted. Use
    three or more distinct magnifications and read `loss_residuals`.
    """
    if len(points) < 2:
        raise ValueError("need at least two measurements")
    xs = [HSCALE_UNITY / scale for _, scale, _ in points]
    ys = [capture * HSCALE_UNITY / scale - produced
          for capture, scale, produced in points]
    if len(set(round(x, 9) for x in xs)) < 2:
        raise ValueError(
            "every point is at the same magnification, so c and k cannot be "
            "separated -- vary the scale, not just the capture")
    return fit_line(xs, ys)


def loss_residuals(points, c, k):
    """How far each measurement sits from the fitted line, in output units.

    This is what says whether the model is right. Residuals within the +-1 that
    floor() and an eye judgement can account for mean the line holds; a
    systematic pattern in them means it does not, however well the fit reads.
    """
    return [produced - ((capture - c) * HSCALE_UNITY / scale - k)
            for capture, scale, produced in points]


def scale_ceiling(capture, memory_window, unity=HSCALE_UNITY):
    """The smallest scale -- the biggest picture -- this capture may be given
    before `produced` overruns the memory window.

    **NO MARGIN IS HELD BACK, AND THE HEADROOM RULE IS RETRACTED.** Its evidence
    was measured through display windows that hid up to 163 px of the picture,
    and the thresholds that survive are non-monotonic in HSCALE and come in
    multiple stable bands. Take the whole window -- 1104 lines of picture in a
    1104-line window is what a hand alignment produces too. HEADROOM_WARN_PX is a
    floor to warn below, not a budget to reserve.
    """
    if memory_window <= 0:
        return None
    return math.ceil(capture * unity / memory_window)


def scale_step(sp, st, delta, raster_total, axis, wrap_at,
               unity=HSCALE_UNITY, minimum_capture=16):
    """What one press of the zoom control should do to an axis.

    IT CHANGES THE CROP, AND ONLY THE CROP. The picture is always as big as the
    raster allows -- `fit_to_raster` says how big and what scale gets it there --
    so zooming in captures less source and magnifies harder, zooming out captures
    more and magnifies less, and the picture never changes size underneath the
    user. One pad, and the two windows are managed for them.

    IT DOES NOT READ THE CURRENT SCALE. There is no `scale` parameter, which is
    deliberate: every size fault this evening came from deriving the new state
    from whatever the registers happened to hold. Reading the size froze a picture
    at 620 lines that nothing could grow; reading the corner put 41 px of the
    previous frame down the left of the screen. The state is computed from the
    capture and the raster, and nothing else.

    Trimming is symmetric about the centre, so out and back is exactly
    reversible -- an irreversible control is how a hand-aligned picture is lost.

    Lower scale means a bigger picture, so the caller's '+' arrives as a negative
    delta: negative crops in, positive opens out.
    """
    capture = st - sp
    if capture <= 0:
        raise ValueError("capture stop is before its start")

    # The step is the delta, in input units, NOT a proportion of the current
    # capture. Proportional steps are not reversible -- out and back use different
    # capture widths and so different step sizes, and the window walks. The
    # panel's own step box is what makes a press coarse or fine.
    step = -delta

    new_sp, new_st = sp, st
    if step:
        near = abs(step) // 2
        far = abs(step) - near
        direction = 1 if step > 0 else -1
        new_sp, new_st = sp + direction * near, st - direction * far
        new_sp = max(0, new_sp)
        if wrap_at is not None:
            new_st = min(new_st, wrap_at - 1)
        if new_st - new_sp < minimum_capture:
            new_sp, new_st = sp, st

    scale, produced = fit_to_raster(new_st - new_sp, raster_total, axis, unity)
    return {"sp": new_sp, "st": new_st, "scale": scale, "produced": produced,
            "zoomed": (new_sp, new_st) != (sp, st)}


def solve_axis(capture, scale, bypassed, raster_total, axis, offset=0,
               origin=None, window_sp_of=None):
    """Where one axis's four output registers belong.

    `capture` is IF units horizontally and HALF-LINES vertically. Everything
    returned is in output pixels or lines.

    **THE ORIGIN IS PREDICTED, NOT GUESSED.** 78 bypassed, 80 at x1.001 and 93 at
    x1.575 are one straight line in magnification, and `axis.origin_offset` is it,
    so the picture is placed from the registers alone. One constant for all three
    shifts a hand-tuned picture by 13 px. `origin` is honoured when a human has
    aligned by eye and wants it left exactly there; without it the picture is
    centred on the raster, and `offset` nudges that.

    The memory window is exactly the display window: only the memory the picture
    occupies is allocated. Anything past it is memory the playback stage still
    walks, and it shows as artefacts down the left-hand edge.
    """
    if capture <= 0:
        raise ValueError("capture must be positive")

    clamped = []
    produced = produced_px(capture, scale, bypassed, axis)
    if produced is None:
        raise ValueError("scale reads 0, which is a dropped read, not a setting")

    origin_offset = axis.origin_offset(magnification(scale, bypassed))

    if origin is not None:
        # The caller knows where the picture is, so nothing here may move it.
        # window_sp keeps whatever position produced that origin: deriving it
        # from the constant would move the picture by however wrong the constant
        # is, which is exactly the 13 px shift this argument exists to prevent.
        window_sp = window_sp_of if window_sp_of is not None else origin - origin_offset
    else:
        origin, window_sp = place_picture(produced, raster_total,
                                          magnification(scale, bypassed), axis)
        origin += offset          # `offset` nudges the centred position
        window_sp = max(0, window_sp + offset)
        if window_sp == 0 and origin <= round(origin_offset):
            edge = "left" if axis is AXIS_H else "top"
            clamped.append(
                f"picture overflows the raster; pinned to the {edge} edge")

    # Both ST registers must stay STRICTLY below the raster's own total register
    # -- VDS_HB_ST < VDS_HSYNC_RST, VDS_VB_ST < VDS_VSYNC_RST -- and they wrap
    # rather than clamp, a wrapped VDS_VB_ST rolling the frame. The total
    # register is one below `raster_total`, so the last usable value is two below.
    last_usable = raster_total - 2

    # Floor, never round: VDS_DIS_?B_ST is where blanking starts, so it may sit
    # at most one past the last written pixel. Rounding up exposes a line of
    # unwritten memory, which shows as a band of scratch at the bottom edge.
    #
    # `axis.margin` gives back the model's own worst residual on top of that. The
    # two errors are not equally bad: a window short by 2 loses 2 px of picture
    # invisibly, while a window long by 2 shows scratch. Tonight's bench had the
    # long kind -- a frozen band down the left, every register reading exactly
    # what was asked for.
    display_sp = math.ceil(origin)
    display_st = min(display_sp + math.floor(produced) - axis.margin, last_usable)

    # **THE MEMORY WINDOW IS THE DISPLAY WINDOW. ALLOCATE NOTHING SPARE.**
    #
    # Allocate only the memory needed to display the image, which is how the
    # chip is meant to be used. The VDS_?B_* pair is the memory extent and
    # VDS_DIS_?B_* the displayed extent, and there is no reason for them to
    # differ.
    #
    # **DO NOT TAKE THE RASTER'S LAST USABLE VALUE.** The far end is not free
    # once the origin is pinned: memory past the picture is memory the playback
    # stage still walks, and it shows as artefacts down the LEFT edge.
    # docs/firmware-geometry-engine.md
    window_st = display_st

    return {
        "produced": produced,
        "origin": origin,
        "window_sp": window_sp,
        "window_st": window_st,
        "display_sp": display_sp,
        "display_st": display_st,
        "covers_panel": covers_panel(produced, raster_total, axis),
        "clamped": clamped,
    }


def solve_geometry(capture_h, capture_v, hscale, vscale, line_px, frame_lines,
                   hscale_bypassed=False, vscale_bypassed=False,
                   offset_h=0, offset_v=0,
                   origin_h=None, window_sp_h=None,
                   origin_v=None, window_sp_v=None):
    """The eight output registers for a given capture and scale.

    The output raster is an input and is never returned: FrameSync owns
    VDS_VSYNC_RST and runAutoBestHTotal owns VDS_HSYNC_RST, so writing either
    fights the firmware and changes the mode the TV locks to.
    """
    h = solve_axis(capture_h, hscale, hscale_bypassed, line_px, AXIS_H, offset_h,
                   origin=origin_h, window_sp_of=window_sp_h)
    v = solve_axis(capture_v, vscale, vscale_bypassed, frame_lines, AXIS_V, offset_v,
                   origin=origin_v, window_sp_of=window_sp_v)

    registers = {}
    for axis, solved in ((AXIS_H, h), (AXIS_V, v)):
        sp, st, dis_sp, dis_st = axis.registers
        registers[sp] = solved["window_sp"]
        registers[st] = solved["window_st"]
        registers[dis_sp] = solved["display_sp"]
        registers[dis_st] = solved["display_st"]

    return {"registers": registers, "horizontal": h, "vertical": v}


def ensure_memory_window(current_px, minimum_px, output_line_px):
    """Widen a memory window to the minimum the headroom rule needs, never
    narrow it.

    The rule constrains the *difference* between the window and the product, so
    arithmetic alone permits a window tight to `produced + headroom`. Every state
    recorded clean used far more than that -- 1371..1381 px of a 1445 px line,
    against a minimum of ~1292 -- so tightening to the bound would be
    extrapolating well outside anything measured, for no gain. A window that is
    already wide enough is left exactly where it is.
    """
    return min(output_line_px, max(current_px, minimum_px))


# --- where to start, for a source nobody has tuned yet -----------------------

# Active area as a fraction of the total, from VESA DMT and CEA:
#
#   640x480@60    640/800  = 0.800    480/525  = 0.914
#   800x600@60    800/1056 = 0.758    600/628  = 0.955
#   1024x768@60  1024/1344 = 0.762    768/806  = 0.953
#   1280x1024@60 1280/1688 = 0.758   1024/1066 = 0.961
#   1920x1080@60 1920/2200 = 0.873   1080/1125 = 0.960
#
# Horizontal barely moves; the NES sits in the same place at 256/341 = 0.751.
# Vertical splits hard on field rate, because a 50 Hz source carries the same
# active height in a longer frame:
#
#   NES NTSC   240/262 = 0.916      NES PAL   240/312 = 0.769
#   RiscPC 50  256/311 = 0.823
#
# So one constant horizontally, two vertically -- select on field rate, no curve.
# A starting point for a source with no stored timings, not a derivation, which
# is why the result below is biased toward over-capture. docs/vesa-gtf.md
DEFAULT_H_ACTIVE_FRACTION = 0.76
DEFAULT_V_ACTIVE_FRACTION_60HZ = 0.95
DEFAULT_V_ACTIVE_FRACTION_50HZ = 0.82

# Err toward capturing blanking rather than cropping picture. Black edges are
# visible and adjustable; a cropped edge looks like a tuning fault and sends you
# hunting for a problem that is not there.
OVER_CAPTURE = 1.04


def default_active_fractions(field_rate_hz):
    """Starting guesses at how much of the raster is picture, by field rate."""
    vertical = (
        DEFAULT_V_ACTIVE_FRACTION_50HZ
        if field_rate_hz < 55.0
        else DEFAULT_V_ACTIVE_FRACTION_60HZ
    )
    return DEFAULT_H_ACTIVE_FRACTION, vertical


def default_capture_window(line_units, field_rate_hz, over_capture=OVER_CAPTURE):
    """A first capture window for an untuned source, centred on the line.

    `line_units` is the IF line length (IF_HSYNC_RST + 1). Returns
    (start, stop) in the same units, deliberately a little wider than the
    estimate so the first picture shows black edges rather than a cropped one.
    """
    h_fraction, _ = default_active_fractions(field_rate_hz)
    width = min(line_units, round(line_units * h_fraction * over_capture))
    start = (line_units - width) // 2
    return start, start + width
