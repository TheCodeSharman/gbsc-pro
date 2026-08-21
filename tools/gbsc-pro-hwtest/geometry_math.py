#!/usr/bin/env python3
"""Arithmetic for reading the scaler's geometry back off the chip.

**THIS DOES NOT DECIDE ANY REGISTER, AND MUST NOT AGAIN.** The firmware engine in
`src/tv5725/` owns the geometry: it calculates every register from held state, so
a second implementation here is a second owner and a copy that goes stale. It
did: the pad tests asserted the firmware equalled this module and reported its
drift as a firmware fault, and `regpanel.py` solved the placement here and wrote
the answer to the chip. Both now ask the engine.

Reading a set of registers back is the one thing the firmware cannot supply,
because it never does it -- which is why this half survives. `scaler_model.py`
holds the placement arithmetic that mirrors the engine, and `curve_fit.py` the
least-squares the bench jigs decide with.

    magnification = 1024 / VDS_HSCALE          (VDS_HSCALE_BYPS means 1:1)
    produced      = capture_units x magnification
    headroom      = memory_window - produced

Everything else is rearrangement. `capture_units` are the input formatter's own
units, whose size depends on the scan mode -- see "What the IF counter counts" in
docs/scaler-geometry-model.md; `produced`, the windows and the line are output
pixels.

No hardware, no I/O: pure arithmetic so it can be tested against the recorded
snapshots without a unit on the bench.
"""


# VDS_HSCALE is a divisor of 1024, so smaller means more magnification. The
# register is 10 bits: 1023 is the practical floor at x1.001, 256 the ceiling at
# x4.
HSCALE_UNITY = 1024


# --- reading a register set --------------------------------------------------


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


# --- choosing a register set -------------------------------------------------


def probe_park(expected_start, visible_edge, band=60):
    """Where to park the display window's near edge before creeping it up.

    The measurement is: park before where the scaler is expected to start
    writing, so a band of frozen scratch shows between the display edge and the
    picture, then creep the edge up until the band goes. That value is the write
    start.

    It only works if the parked edge is on screen. A band parked behind the bezel
    vanishes at the bezel rather than at the write start, and the reading is then
    the panel's edge wearing the write start's name -- which is how 127 and 78
    reached this file as two separate constants and were the same reading twice.

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

# --- where to start, for a source nobody has tuned yet -----------------------


# --- what a capture unit is ---------------------------------------------------


def line_doubled(prgrsv, ld_ram_byps, ld_sel_prov, hs_dec_factor):
    """Whether the line doubler is in the capture path, from the four registers
    that decide it, or None when they disagree.

    The firmware writes all four from one argument, so a disagreement is a
    half-applied change rather than a mode. Reporting that beats picking one
    register and being wrong about every unit derived from it.
    """
    doubled = (prgrsv == 0, ld_ram_byps == 0, ld_sel_prov == 0, hs_dec_factor != 0)
    if all(doubled):
        return True
    if not any(doubled):
        return False
    return None


def if_vertical_wrap(source_lines, line_doubled):
    """Where IF_VB_ST and IF_VB_SP roll over, or None if the scan mode is unknown.

    The input formatter has one line counter and the doubler sets its rate: at
    twice the source's line rate the counter is in half-lines and wraps at
    2 x (VTOTAL + 1), and at the source's own rate it wraps at VTOTAL + 1.
    RD-5725-1.1 gives no unit for either register; both wraps are measured.
    docs/scaler-geometry-model.md, "What the IF counter counts".

    A window written past the wrap is never reached, so the write enable does not
    fall and the frame buffer takes written and unwritten lines alternately -- a
    bound computed from a guessed wrap is worse than none.
    """
    if line_doubled is None:
        return None
    return 2 * (source_lines + 1) if line_doubled else source_lines + 1


def if_line_units(divider, line_doubled):
    """IF_HSYNC_RST for this ADC divider, or None if the scan mode is unknown.

    The same doubling seen horizontally: the IF fits two output lines into one
    input line while doubling, so it counts half as finely. Measured PLLAD_MD
    2250 against IF_HSYNC_RST 1125 doubled, and 1124 against 1124 not.
    """
    if line_doubled is None:
        return None
    return divider // 2 if line_doubled else divider
