#!/usr/bin/env python3
"""Arithmetic for reading the scaler's geometry, and for the bench measurements.

**THIS DOES NOT DECIDE ANY REGISTER, AND MUST NOT AGAIN.** The firmware engine
in `src/tv5725/` owns the geometry: it calculates every register from held
state, so a second implementation here is a second owner and a copy that goes
stale. It did: the pad tests asserted the firmware equalled this module and
reported its drift as a firmware fault, and `regpanel.py` solved the placement
here and wrote the answer to the chip. Both now ask the engine.

What survives is what the firmware cannot supply: the arithmetic for reading a
register set back (`geometry.py`), and the prediction the measurement jigs
compare against, which is the whole point of `measure_origin.py` and
`measure_produced.py` -- a deviation needs something to deviate from. Plus the
least-squares fitting, which has no firmware equivalent at all.

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
# RD-5725-1.1 states NO minimum for VDS_HSCALE. It gives only the ratio
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


# --- choosing a register set -------------------------------------------------


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


# --- where to start, for a source nobody has tuned yet -----------------------
