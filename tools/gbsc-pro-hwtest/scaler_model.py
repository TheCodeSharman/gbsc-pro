#!/usr/bin/env python3
"""The firmware's placement arithmetic, mirrored, so a measurement has something
to deviate from.

**THIS DECIDES NO REGISTER.** The engine in `src/tv5725/` owns the geometry: it
calculates every register from held state, so a second implementation is a second
owner and a copy that goes stale. It did -- the pad tests asserted the firmware
equalled this and reported its drift as a firmware fault, and `regpanel.py` solved
the placement here and wrote the answer to the chip. Both now ask the engine.

What is left is a PREDICTION for `measure_origin.py` and `measure_produced.py` to
compare a bench reading against, which is the whole point of those jigs. Nothing
that reads the unit back needs this module, and nothing that configures it may
use it. It is the part of the old geometry_math that goes when the jigs no longer
need a reference of their own.
"""

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


class Axis:
    """What differs between the two axes. The arithmetic does not.

    `capture` is in IF units horizontally and HALF-LINES vertically -- the unit
    difference is absorbed by the scale value, so the formula is the same, but it
    is why a vertical capture number looks half the size you expect.
    """

    def __init__(self, start_const, start_per_mag, margin, window_sp_min):
        self.start_const = start_const
        self.start_per_mag = start_per_mag
        self.margin = margin
        self.window_sp_min = window_sp_min

    def origin_offset(self, magnification):
        """How far after VDS_?B_SP the scaler starts writing, at this
        magnification. Not a constant -- see START_PER_MAG_H."""
        return self.start_const + self.start_per_mag * magnification


# `margin` trims the display window's far edge. produced reproduces every far
# edge measured to within 1.01 px and 1.71 lines, and the two costs of being
# wrong are not symmetric: a window 2 px short loses 2 px of picture nobody can
# see, while a window 2 px long shows scratch. So each axis gives back its own
# worst residual, rounded up -- the measured uncertainty, spent in the safe
# direction.
AXIS_H = Axis(
    START_CONST_H, START_PER_MAG_H,
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
)

# No vertical margin: a settled state at -1.9 lines was clean, and the horizontal
# rule is a line-time constraint with no per-frame equivalent. Tentative -- that
# state's display window was clipping, the same flaw that invalidated the
# horizontal evidence, and the VSCALE +1 ambiguity is worth 1.2 lines here.
AXIS_V = Axis(
    START_CONST_V, START_PER_MAG_V,
    margin=3,
    # UNMEASURED. The horizontal floor of 8 was found by creeping until the
    # picture broke; nobody has done the vertical. 0 is what the code assumed
    # before, so it is no worse than the status quo -- but it is an assumption,
    # not a measurement, and it should not be read as one.
    window_sp_min=0,
)


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

