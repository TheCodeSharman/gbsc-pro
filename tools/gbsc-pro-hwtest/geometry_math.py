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

# Measured 2026-08-05: the first written pixel sits a fixed distance after
# VDS_?B_SP, and that distance does not move with the capture window. Horizontal
# confirmed constant over VDS_HB_SP 49..170; the 2 px difference is the scaler
# stage in the path, which a bypassed scaler does not add.
ORIGIN_OFFSET_H_BYPASSED = 78
ORIGIN_OFFSET_H_SCALED = 80
ORIGIN_OFFSET_V = 26

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

    def __init__(self, name, offset_bypassed, offset_scaled, warn_px,
                 visible_edge, registers):
        self.name = name
        self.offset_bypassed = offset_bypassed
        self.offset_scaled = offset_scaled
        self.warn_px = warn_px
        self.visible_edge = visible_edge
        self.registers = registers

    def origin_offset(self, bypassed):
        return self.offset_bypassed if bypassed else self.offset_scaled

    def visible_span(self, raster_total):
        """How much of the raster the panel actually shows."""
        return raster_total - 2 * self.visible_edge


AXIS_H = Axis(
    "horizontal", ORIGIN_OFFSET_H_BYPASSED, ORIGIN_OFFSET_H_SCALED,
    HEADROOM_WARN_PX, PANEL_VISIBLE_LEFT,
    ("VDS_HB_SP", "VDS_HB_ST", "VDS_DIS_HB_SP", "VDS_DIS_HB_ST"),
)

# No vertical margin: a settled state at -1.9 lines was clean, and the horizontal
# rule is a line-time constraint with no per-frame equivalent. Tentative -- that
# state's display window was clipping, the same flaw that invalidated the
# horizontal evidence, and the VSCALE +1 ambiguity is worth 1.2 lines here.
AXIS_V = Axis(
    "vertical", ORIGIN_OFFSET_V, ORIGIN_OFFSET_V,
    0, PANEL_VISIBLE_TOP,
    ("VDS_VB_SP", "VDS_VB_ST", "VDS_DIS_VB_SP", "VDS_DIS_VB_ST"),
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


def produced_px(capture_units, hscale, bypassed=False):
    """Output pixels the scaler makes from a capture window of this width."""
    factor = magnification(hscale, bypassed)
    return None if factor is None else capture_units * factor


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


def solve_axis(capture, scale, bypassed, raster_total, axis, offset=0):
    """Where one axis's four output registers belong.

    `capture` is IF units horizontally and HALF-LINES vertically. Everything
    returned is in output pixels or lines.

    The picture is centred on the raster and the memory window is opened as wide
    as the raster allows: the origin pins the window's near edge, leaving the far
    edge free, and there is no reason to leave margin unclaimed. `offset` nudges
    the centred position for a caller doing manual placement.
    """
    if capture <= 0:
        raise ValueError("capture must be positive")

    clamped = []
    produced = produced_px(capture, scale, bypassed)
    if produced is None:
        raise ValueError("scale reads 0, which is a dropped read, not a setting")

    origin_offset = axis.origin_offset(bypassed)

    # Centre the picture, then let the window's near edge follow from it. Both
    # can be driven off the bottom of the raster by a picture wider than the
    # screen, which is legal -- it simply overscans -- but the registers cannot
    # go negative.
    origin = round((raster_total - produced) / 2) + offset
    window_sp = origin - origin_offset
    if window_sp < 0:
        window_sp = 0
        origin = origin_offset
        clamped.append("picture is wider than the raster; pinned to the left edge")

    # The last value below the raster total. Both ST registers wrap rather than
    # clamp, and a wrapped VDS_VB_ST rolls the frame.
    window_st = raster_total - 1

    margin_given = (window_st - window_sp) - produced
    if margin_given < axis.warn_px:
        clamped.append(
            f"only {margin_given:.1f} px of headroom, under the {axis.warn_px} px "
            f"floor -- the picture may tear"
        )

    display_sp = origin
    display_st = min(origin + round(produced), raster_total - 1)

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
                   offset_h=0, offset_v=0):
    """The eight output registers for a given capture and scale.

    The output raster is an input and is never returned: FrameSync owns
    VDS_VSYNC_RST and runAutoBestHTotal owns VDS_HSYNC_RST, so writing either
    fights the firmware and changes the mode the TV locks to.
    """
    h = solve_axis(capture_h, hscale, hscale_bypassed, line_px, AXIS_H, offset_h)
    v = solve_axis(capture_v, vscale, vscale_bypassed, frame_lines, AXIS_V, offset_v)

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
