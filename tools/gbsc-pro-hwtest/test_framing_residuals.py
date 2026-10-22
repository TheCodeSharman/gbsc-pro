"""Where a swept mode landed against its own design, off the sweep's records.
No hardware.

The records below carry the bench's own 640x480@60 -- the case the whole thing
exists for, where the machine emits AKF50's raster and the engine answers with
DMT's -- so a residual read off them is a statement about the arithmetic rather
than about a reading.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import framing_residuals as fr

# AKF50's 640x480@60, as RetroScaler-Acorn.mdf states it: the picture starts at
# 138 of 800, where DMT states 144.
AKF50_640x480 = dict(x_res="640", y_res="480", pixel_rate="25175",
                     h_timings="94,22,22,640,22,0", v_timings="2,32,0,480,0,11")
# A ROUND raster, so the arithmetic below can be checked by eye: its picture
# starts at a fifth of the line and runs four fifths, which /geometry's
# ten-thousandths state exactly. A real entry cannot -- AKF50's 320x256@50
# starts at 110 of 512, which rounds, and the tenth of a pixel that leaves is
# a measurement of the report's resolution rather than of a framing.
ROUND_400x200 = dict(x_res="400", y_res="200", pixel_rate="10000",
                     h_timings="50,50,0,400,0,0", v_timings="10,40,0,200,0,0")

# The engine's own placement, and a window of 1000 capture units magnified onto
# a 2000-pixel frame -- two output pixels to the unit, so a residual in units is
# half what it is in pixels.
ON_DESIGN = {"poh": 2000, "peh": 8000, "pov": 2000, "pev": 8000,
             "eh": 1000, "ev": 500}
AS_DMT = {"poh": 1800, "peh": 8000, "pov": 667, "pev": 9162,
          "eh": 1000, "ev": 500}
# The same round raster placed ten of its own pixels late ACROSS and on its
# design DOWN, which is what a standard answering on one axis alone looks like.
ACROSS_FROM_ANOTHER_ROW = dict(ON_DESIGN, poh=2200)


def record(entry, geometry, black, mode="X400 Y200 C256 F80"):
    return dict(source=mode, geometry=geometry, emitted=[2000, 1000],
                black_cols=black)


def flush():
    return dict(left=0, right=0, top=0, bottom=0)


def test_a_window_on_its_own_design_is_out_by_nothing():
    off = fr.residuals(record(ROUND_400x200, ON_DESIGN, flush()),
                       ROUND_400x200)
    assert all(off["units"][edge] == 0.0 for edge in fr.EDGES)


def test_black_at_an_edge_is_read_in_the_capture_units_the_engine_places_in():
    """Two output pixels to the capture unit here, so six columns of black
    before the border is three units of placement."""
    off = fr.residuals(record(ROUND_400x200, ON_DESIGN, dict(flush(), left=6)),
                       ROUND_400x200)
    assert off["units"]["left"] == 3.0


def test_the_black_the_design_puts_at_an_edge_is_not_a_residual():
    """DMT answering for AKF50's 640x480@60 shows six of the source's own
    pixels at the far edge, which is eighteen emitted columns here and none of
    it a deficit."""
    emitted_per_source_px = 2000 / 640.0
    black = dict(flush(), right=round(6.0 * emitted_per_source_px))
    off = fr.residuals(record(AKF50_640x480, AS_DMT, black,
                              mode="X640 Y480 C256 F60"), AKF50_640x480)
    assert abs(off["units"]["right"]) < 0.5


def test_a_mode_the_engine_answered_from_another_raster_is_not_its_own_row():
    """The residual is only a measurement of the engine where the row it
    matched IS the one the machine emits, so the pooling has to know which."""
    own = fr.residuals(record(ROUND_400x200, ON_DESIGN, flush()), ROUND_400x200)
    assert fr.on_its_own_row(own, "across")
    assert fr.on_its_own_row(own, "down")


def test_each_axis_is_asked_separately_which_row_answered_it():
    """The two authorities can state one resolution's vertical window
    identically and its horizontal window ten pixels apart -- 1024x768@60 is 71
    source pixels out across and half a line down -- so a mode the standard
    answers horizontally still measures the engine vertically."""
    mixed = fr.residuals(record(ROUND_400x200, ACROSS_FROM_ANOTHER_ROW, flush()),
                         ROUND_400x200)
    assert not fr.on_its_own_row(mixed, "across")
    assert fr.on_its_own_row(mixed, "down")


def test_a_record_the_sweep_never_judged_has_no_residual():
    assert fr.residuals(dict(source="X400 Y200 C256 F80", skipped="refused"),
                        ROUND_400x200) is None
