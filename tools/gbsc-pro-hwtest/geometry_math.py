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
# register is 10 bits; 1023 is the practical floor at x1.001 and scaleHorizontal()
# has always clamped the other end at 256 (x4).
HSCALE_UNITY = 1024
HSCALE_MIN = 256
HSCALE_MAX = 1023

# The product fitting inside the memory window is NOT sufficient. The scaler has
# to finish reading the line out of memory before the line period ends, so it
# needs slack -- and with too little, the picture tears exactly as it does on a
# real overflow.
#
# HOW MUCH slack is not known, and this module deliberately stops trying to say.
#
# The measurements that survive scrutiny, all at PLLAD_MD 2553 on a 1445 px
# output line, capture held at 798 units, moving only VDS_HB_ST to the edge:
#
#   HSCALE 1023 (x1.001) -> produced  798.78, edge at VDS_HB_ST  881   33.2 px
#   HSCALE  850 (x1.205) -> produced  961.36, edge at VDS_HB_ST 1095  ~84.6 px
#
# Those disagree with every simple model -- the requirement is NOT monotonic in
# HSCALE -- and the corruption has multiple stable bands, so an edge found by
# creeping down is only the true edge if you creep past all of them. More
# measurement would not fix that.
#
# Everything earlier is retracted. The old rule (13 px, "measured" 2026-08-03 at
# HSCALE 660-670) rests on SOLVED-mode13-fullscreen-clean, whose VDS_DIS_HB_ST of
# 1372 blanked 74 px of a picture ending at 1446 -- and tearing shows worst at
# the right of the line, so its evidence was hidden. Points at HSCALE 993 and 850
# taken the same evening failed the same way, with 24.9 px and 163 px hidden.
# Recorded in docs/photos/2026-08-05-horizontal-geometry/ as measurements not to
# reuse. The lesson is the procedural one: any headroom measurement is worthless
# unless the display window contains the whole picture.
#
# So: do not target a margin, take all of it. The origin pins VDS_?B_SP, leaving
# VDS_?B_ST free -- push it to the last value below the raster total and accept
# whatever headroom results. It costs nothing and cannot land in a false-safe
# band. This is only a floor to warn below.
HEADROOM_WARN_PX = 100


# --- where the picture lands --------------------------------------------------

# The first written pixel sits this far after VDS_?B_SP -- the near edge of the
# frame buffer, NOT of the display window. Measured against TestPat's 1 px green
# frame by creeping VDS_DIS_?B_SP until the frame vanished:
#
#   horizontal   VDS_HB_SP 49 -> origin 127   offset 78    photo 10, ~1:1
#                VDS_HB_SP 35 -> origin 129   offset 94    pixel-perfect, x1.58
#   vertical     VDS_VB_SP 37 -> origin  63   offset 26    photo 13 and 14
#
# The two horizontal numbers do not agree and nothing here explains why. 94 is
# used because it is the one from the state where all four edges were aligned to
# the pixel, so it is consistent with CORNER_H from that same state -- the pair
# has to come from one alignment or the picture lands between two of them.
#
# This offset is what makes the corner reachable: the memory window's near edge
# is placed at corner - offset, so the picture starts on the corner. Inheriting
# whatever VDS_?B_SP happened to be instead is what put the display window 136
# lines before any valid memory on the bench.
#
# RESOLVED 2026-08-05 by measure_origin.py. They do not disagree; they were
# measured at different magnifications, and the offset is not a constant:
#
#     write start = VDS_?B_SP + START_CONST + START_PER_MAG x magnification
#
# The near edge was crept until TestPat's frozen scratch band vanished, three
# VDS_?B_SP values and three magnifications per axis. VDS_?B_SP tracks one for
# one (slope 1.000, residual 0.00 on both axes), so the offset is real; what
# moves is the magnification term.
#
#   horizontal   55.0 + 25.0 x m      residual 0.01 px
#   vertical      0.2 +  0.8 x m      residual 0.11 lines
#
# 25 output px per unit magnification is 25 INPUT samples: a fixed run-up the
# horizontal scaler consumes before it writes anything, which is what an 11-tap
# filter costs (IF_HS_TAP11_BYPS). The vertical reads whole lines from a line
# buffer and costs ~2 lines of the same. Both are PIPELINE LATENCY AT THE START,
# and the old model saw that latency from the far end, where it could only look
# like loss.
#
# Everything the old constants recorded falls out of this:
#
#   78 bypassed, 80 at x1.001, 93 at x1.575   solve_axis's own docstring, which
#                                             said nothing predicted them
#   94 at x1.58                               ORIGIN_OFFSET_H_SCALED
#   129 at x1.58                              CORNER_H, VDS_HB_SP 35
#   26                                        ORIGIN_OFFSET_V -- and 37 + 26 = 63
#                                             is PANEL_VISIBLE_TOP exactly. That
#                                             one was the bezel, not the scaler:
#                                             measured, it is 2 lines.
START_CONST_H = 55.0
START_PER_MAG_H = 25.0
START_CONST_V = 0.2
START_PER_MAG_V = 0.8

# WHAT USED TO BE HERE, and why it is not.
#
# CORNER_H = 129, CORNER_V = 63 -- "the top left corner of the PICTURE, measured
# rather than derived, absolute positions on the output raster, which is why they
# are constants at all". They are not constants. 129 is the write start at x1.58,
# the magnification of the one state it was read from, and 63 is
# PANEL_VISIBLE_TOP -- the bezel, reached by creeping the near edge until
# something vanished, which is the same procedure and the same answer.
#
# CAPTURE_OFFSET_H = -24.67, OUTPUT_LOSS_H = 38.27, OUTPUT_LOSS_V = 24.33 --
# fitted to eleven readings over four magnifications per axis, worst residual
# 0.43 px, every measurement reproduced. A good fit to real data, and wrong: it
# was measuring a fixed span from an origin that moves, so the origin's
# magnification term came out split between an input offset and an output loss.
# -24.67 is START_PER_MAG_H to two decimal places, which is the tell.
#
# NONE OF THE READINGS WERE BAD. Every one of them is still an acceptance test
# above; what changed is what they are readings of. Residuals could never have
# caught this -- the wrong model fitted them to 0.43 px -- and only measuring the
# near edge did. Keep that in mind before trusting the next good fit.

# The panel shows less than the raster: creeping the display window to the bezel
# put its corner at output pixel 127, line 63. Assuming the visible region is
# symmetric in the raster gives the extents below, and "full screen" means
# covering them -- NOT filling the raster, which is 254 px wider than anything
# the TV displays.
#
# TENTATIVE vertically. At produced 795.93 lines the bench looks roughly full
# height, which a 999-line visible region would not; the real figure may be
# nearer 800, i.e. the region is not symmetric. Calibrate against the panel.
PANEL_VISIBLE_LEFT = 127
PANEL_VISIBLE_TOP = 63


class Axis:
    """What differs between the two axes. The arithmetic does not.

    `capture` is in IF units horizontally and HALF-LINES vertically -- the unit
    difference is absorbed by the scale value, so the formula is the same, but it
    is why a vertical capture number looks half the size you expect.
    """

    def __init__(self, name, start_const, start_per_mag, warn_px,
                 visible_edge, registers, margin):
        self.name = name
        self.start_const = start_const
        self.start_per_mag = start_per_mag
        self.warn_px = warn_px
        self.visible_edge = visible_edge
        self.registers = registers
        self.margin = margin

    def origin_offset(self, magnification):
        """How far after VDS_?B_SP the scaler starts writing, at this
        magnification. Not a constant -- see START_PER_MAG_H."""
        return self.start_const + self.start_per_mag * magnification

    def visible_span(self, raster_total):
        """How much of the raster the panel actually shows."""
        return raster_total - 2 * self.visible_edge


# `margin` trims the display window's far edge. produced reproduces every far
# edge ever measured to within 1.01 px and 1.71 lines, and the two costs of being
# wrong are not symmetric: a window 2 px short loses 2 px of picture and nobody
# can see it, while a window 2 px long shows scratch -- which is exactly what was
# on the bench tonight. So each axis gives back its own worst residual, rounded
# up. Not a fudge factor; the measured uncertainty, spent in the safe direction.
AXIS_H = Axis(
    "horizontal", START_CONST_H, START_PER_MAG_H,
    HEADROOM_WARN_PX, PANEL_VISIBLE_LEFT,
    ("VDS_HB_SP", "VDS_HB_ST", "VDS_DIS_HB_SP", "VDS_DIS_HB_ST"),
    margin=2,
)

# No vertical margin: a settled state at -1.9 lines was clean, and the horizontal
# rule is a line-time constraint with no per-frame equivalent. Tentative -- that
# state's display window was clipping, the same flaw that invalidated the
# horizontal evidence, and the VSCALE +1 ambiguity is worth 1.2 lines here.
AXIS_V = Axis(
    "vertical", START_CONST_V, START_PER_MAG_V,
    0, PANEL_VISIBLE_TOP,
    ("VDS_VB_SP", "VDS_VB_ST", "VDS_DIS_VB_SP", "VDS_DIS_VB_ST"),
    margin=3,
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


def place_picture(scale, bypassed, axis):
    """(corner, window_sp) putting the picture at the near edge of what the panel
    shows, at whatever magnification is set.

    THE CORNER IS NOT A CONSTANT. The scaler starts writing origin_offset(m)
    after VDS_?B_SP, so a display window pinned to a corner measured once at
    x1.58 begins before the first written pixel at every higher magnification --
    41 px of last frame's contents at x3.2, which is what was on the bench on
    2026-08-05 and read as garbage down the left.

    So the corner is derived and VDS_?B_SP moves to produce it. Where the offset
    already exceeds the visible edge the picture cannot reach that far -- the
    register would have to go negative -- and it starts at the offset instead.
    That is a real limit of the chip: horizontally the offset is 55 + 25 x m, so
    at x4 nothing can be placed before output pixel 155, and the panel's own left
    edge at 127 is unreachable above about x2.9.
    """
    offset = axis.origin_offset(magnification(scale, bypassed))
    corner = max(axis.visible_edge, offset)
    return round(corner), max(0, round(corner - offset))


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


def scale_step(sp, st, scale, delta, memory_window, wrap_at,
               unity=HSCALE_UNITY):
    """What one press of the scale control should do to an axis.

    Below the ceiling this is just a scale change and the capture is left alone
    -- it is the user's, set by panning, and moving it under them would undo a
    hand alignment.

    AT the ceiling the scale can go no lower, so the zoom continues in the
    capture instead: trim to zoom in, widen to zoom out, with the scale refilling
    the window each time so the picture stays exactly as big as the window
    allows. That makes '+' and '-' inverses, which matters more here than
    anywhere -- an irreversible control is how a hand-aligned picture is lost.

    Returns the capture window, the scale, and whether the capture moved.
    """
    capture = st - sp
    if capture <= 0:
        raise ValueError("capture stop is before its start")

    floor = scale_ceiling(capture, memory_window, unity)
    if floor is None:
        raise ValueError("the memory window is too small to hold any picture")
    floor = max(floor, HSCALE_MIN)

    wanted = min(max(scale + delta, HSCALE_MIN), HSCALE_MAX)
    zooming_in = wanted < floor
    zooming_out_while_pinned = delta > 0 and scale <= floor

    if delta and (zooming_in or zooming_out_while_pinned):
        # The magnification the user asked for, delivered by capturing less
        # source instead of by a scale the register cannot reach.
        step = round(capture * -delta / scale)
        if step:
            new_sp, new_st, new_scale = zoom_capture(
                sp, st, memory_window, step, wrap_at, unity)
            if (new_sp, new_st) != (sp, st):
                return {"sp": new_sp, "st": new_st, "scale": new_scale,
                        "zoomed": True}

    return {"sp": sp, "st": st, "scale": wanted, "zoomed": False}


def solve_axis(capture, scale, bypassed, raster_total, axis, offset=0,
               origin=None, window_sp_of=None):
    """Where one axis's four output registers belong.

    `capture` is IF units horizontally and HALF-LINES vertically. Everything
    returned is in output pixels or lines.

    The origin is now PREDICTED, not guessed. It used to be passed in because
    nothing here could compute it -- 78 bypassed, 80 at x1.001, 93 at x1.575, and
    a caller that used one constant for all three shifted a hand-tuned picture by
    13 px. Those three are one straight line in magnification and
    `axis.origin_offset` is it, so the picture can be placed from the registers
    alone. `origin` is still honoured when a human has aligned by eye and wants
    it left exactly there.

    Without it, the picture is centred on the raster. `offset` nudges that.

    The memory window is always opened as wide as the raster allows: the origin
    pins its near edge, leaving the far edge free, and there is no reason to
    leave margin unclaimed.
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
        # Centre the picture, then let the window's near edge follow from it.
        # Both can be driven off the bottom of the raster by a picture wider than
        # the screen, which is legal -- it simply overscans -- but the registers
        # cannot go negative.
        origin = round((raster_total - produced) / 2) + offset
        window_sp = round(origin - origin_offset)
        if window_sp < 0:
            # VDS_?B_SP cannot go negative, so the picture cannot start earlier
            # than origin_offset. At high magnification that is a real limit and
            # not a rounding detail: the horizontal offset is 55 + 25 x m, so at
            # x4 nothing can be placed before output pixel 155.
            window_sp = 0
            origin = origin_offset
            edge = "left" if axis is AXIS_H else "top"
            clamped.append(
                f"picture overflows the raster; pinned to the {edge} edge")

    # Both ST registers must stay STRICTLY below the raster's own total register
    # -- VDS_HB_ST < VDS_HSYNC_RST, VDS_VB_ST < VDS_VSYNC_RST -- and they wrap
    # rather than clamp, a wrapped VDS_VB_ST rolling the frame. The total
    # register is one below `raster_total`, so the last usable value is two below.
    last_usable = raster_total - 2
    window_st = last_usable

    margin_given = (window_st - window_sp) - produced
    if margin_given < axis.warn_px:
        clamped.append(
            f"only {margin_given:.1f} px of headroom, under the {axis.warn_px} px "
            f"floor -- the picture may tear"
        )

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

    return {
        "produced": produced,
        "origin": origin,
        "window_sp": window_sp,
        "window_st": window_st,
        "display_sp": display_sp,
        "display_st": display_st,
        "margin_given": margin_given,
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
