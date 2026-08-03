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
# real overflow. Bracketed empirically on 2026-08-03 at PLLAD_MD 2553, output
# line 1445 px, by moving VDS_HSCALE with everything else held fixed:
#
#   capture 877, memory 1363, HSCALE 660 -> 1360.68 px,  2.3 px slack  ARTEFACTS
#   capture 877, memory 1363, HSCALE 665 -> 1350.40 px, 12.6 px slack  ARTEFACTS
#   capture 882, memory 1375, HSCALE 665 -> 1358.15 px, 16.9 px slack  CLEAN
#   capture 877, memory 1363, HSCALE 670 -> 1340.37 px, 22.6 px slack  CLEAN
#
# The middle pair is the decisive one: same HSCALE, so same ratio and the same
# interpolation -- only the slack differs, and only that changed the outcome.
# So the governing quantity is (memory window - produced), not the ratio.
#
# UNVERIFIED beyond one clock and one output preset. A timing budget should
# scale with the line period, so on the 2600 px output preset the threshold may
# be a similar *fraction* of the line rather than the same pixel count. Re-measure
# before trusting these numbers there.
HEADROOM_MIN_PX = 13    # 12.6 px artefacted
HEADROOM_SAFE_PX = 20   # 16.9 px was clean; round up for margin


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


def is_safe(memory_window_px, produced, minimum=HEADROOM_MIN_PX):
    return headroom_px(memory_window_px, produced) >= minimum


# --- choosing a register set -------------------------------------------------


def hscale_for(capture_units, target_active_px):
    """The VDS_HSCALE that maps this capture onto this output width, unclamped
    and unrounded checks left to the caller."""
    if target_active_px <= 0:
        raise ValueError("target_active_px must be positive")
    return HSCALE_UNITY * capture_units / target_active_px


def smallest_safe_hscale(capture_units, memory_window_px, headroom=HEADROOM_SAFE_PX):
    """The least magnification-heavy HSCALE whose product still leaves `headroom`
    inside this memory window. Larger HSCALE means a smaller picture, so this is
    the *smallest* value that is safe. None if the window cannot hold anything."""
    usable = memory_window_px - headroom
    if usable <= 0:
        return None
    return math.ceil(HSCALE_UNITY * capture_units / usable)


def solve_horizontal(
    capture_units,
    target_active_px,
    output_line_px,
    headroom=HEADROOM_SAFE_PX,
):
    """Registers that display `capture_units` at `target_active_px` wide.

    Returns widths, not positions. Where the windows sit within the line is a
    separate question -- the write origin has never been measured (the geometry
    handover records it as assumed to be VDS_HB_SP), so callers should preserve
    the existing left edges rather than trust a computed placement.

    The headroom constraint is applied here rather than checked afterwards: if
    the requested width cannot be produced with enough slack, the returned
    `hscale` is raised until it can and `clamped` says why. A caller that ignores
    the result still gets a safe register set.
    """
    if capture_units <= 0:
        raise ValueError("capture_units must be positive")
    if output_line_px <= headroom:
        raise ValueError("output_line_px leaves no room for headroom")

    clamped = []

    hscale = round(hscale_for(capture_units, target_active_px))

    if hscale < HSCALE_MIN:
        hscale = HSCALE_MIN
        clamped.append("hit the x4 magnification limit")
    if hscale > HSCALE_MAX:
        hscale = HSCALE_MAX
        clamped.append("hit the x1.001 floor")

    # The widest picture this line can carry and still leave slack. Raising
    # hscale shrinks the picture, so this is a lower bound on hscale.
    fits = smallest_safe_hscale(capture_units, output_line_px, headroom)
    if fits is not None and hscale < fits:
        hscale = min(fits, HSCALE_MAX)
        clamped.append(
            f"raised to {hscale} so the product leaves {headroom} px inside the line"
        )

    produced = produced_px(capture_units, hscale)
    display_window = math.floor(produced)
    minimum_memory = min(output_line_px, math.ceil(produced + headroom))

    return {
        "hscale": hscale,
        "produced_px": produced,
        "display_window_px": display_window,
        "min_memory_window_px": minimum_memory,
        "magnification": magnification(hscale),
        "clamped": clamped,
        "safe": minimum_memory - produced >= HEADROOM_MIN_PX,
    }


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
