"""What the design puts at each emitted edge, off a mode file and a /geometry.
No hardware.

The numbers below are the bench's own monitor definition and the engine's own
placement, which is what makes them a statement about the design rather than
about a reading: 640x480@60 is the case the whole thing exists for, where the
machine emits AKF50's raster and the engine answers with DMT's.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import expected_framing as ef

# AKF50's 640x480@60, as RetroScaler-Acorn.mdf states it: sync 94, porch 22,
# border 22, so the picture starts at 138 of 800.
AKF50_640x480 = dict(x_res="640", y_res="480", pixel_rate="25175",
                     h_timings="94,22,22,640,22,0", v_timings="2,32,0,480,0,11")
# Its own 320x256@50, where no standard states the mode at all.
AKF50_320x256 = dict(x_res="320", y_res="256", pixel_rate="8000",
                     h_timings="36,30,44,320,44,38", v_timings="3,16,17,256,17,3")

# What the engine places for each: DMT's 144 of 800 for the first, the mode's
# own 110 of 512 for the second. Vertically the capture reaches the END of the
# window's last line, so the extent is one line longer than the picture.
AS_DMT = {"poh": 1800, "peh": 8000, "pov": 667, "pev": 9162}
AS_ITS_OWN = {"poh": 2148, "peh": 6250, "pov": 1154, "pev": 8237}


def test_the_mode_files_picture_starts_after_its_border():
    timing = ef.stated(AKF50_640x480)
    assert timing["h_total"] == 800 and timing["v_total"] == 525
    assert timing["active_px"] == 640 and timing["active_ln"] == 480
    assert timing["h_start"] == 138 / 800            # sync + porch + border
    assert abs(timing["field_rate"] - 59.94) < 0.01


def test_an_entry_missing_its_timings_states_nothing_rather_than_guessing():
    assert ef.stated(dict(x_res="640", y_res="480")) is None
    assert ef.stated(dict(AKF50_640x480, h_timings="0,0,0,0,0,0")) is None


def test_the_standard_answering_for_a_mode_file_entry_costs_both_edges():
    # DMT puts 640x480@60's picture at 144 of 800 and the machine emits 138, so
    # the window opens 6 of the source's pixels late and closes 6 late: the
    # outermost pixel is not captured at the near edge and 6 pixels of the
    # source's own border are shown at the far one.
    off = ef.offsets(AKF50_640x480, AS_DMT)
    assert abs(off["left"] - 6.0) < 0.05
    assert abs(off["right"] - 6.0) < 0.05


def test_a_mode_only_its_own_file_states_costs_nothing():
    off = ef.offsets(AKF50_320x256, AS_ITS_OWN)
    assert abs(off["left"]) < 0.1
    assert abs(off["right"]) < 0.1


def test_the_vertical_capture_reaches_the_end_of_its_last_line():
    # activeExtent states a COUNT of lines, so a span reaching the end of the
    # last one is a line longer -- by design, and the acceptance has to expect
    # the line of blanking it shows rather than read it as a fault.
    off = ef.offsets(AKF50_320x256, AS_ITS_OWN)
    assert abs(off["top"]) < 0.1
    assert abs(off["bottom"] - 1.0) < 0.1


def test_the_offsets_become_emitted_pixels_through_the_modes_own_count():
    # At a default framing the frame IS the picture, so one source pixel is
    # 1920/640 = 3 columns across and 1080/480 = 2.25 rows down.
    want = ef.margins(AKF50_640x480, AS_DMT, (1920, 1080))
    assert abs(want["across"][0] + 18.0) < 0.2       # 6 px of picture, clipped
    assert abs(want["across"][1] - 18.0) < 0.2       # and 6 px of black shown
    assert abs(want["down"][0] + 2.25) < 0.1
    assert abs(want["down"][1] - 4.5) < 0.1          # the extra line, plus one


@pytest.mark.parametrize("mode,expected", [
    ("X640 Y480 C256 F60", "640"),
    ("X320 Y256 C256 F50", "320"),
    ("X1024 Y768 C256 F60", None),
])
def test_the_entry_is_picked_by_x_and_y_exactly_and_the_rate_nearest(mode, expected):
    entries = [AKF50_640x480, AKF50_320x256]
    found = ef.entry_for(entries, mode)
    assert (found["x_res"] if found else None) == expected


def test_two_entries_of_one_resolution_are_split_on_the_field_rate():
    at_72 = dict(AKF50_640x480, pixel_rate="31500", h_timings="40,128,0,640,0,24",
                 v_timings="3,28,0,480,0,9")
    entries = [AKF50_640x480, at_72]
    assert ef.entry_for(entries, "X640 Y480 C256 F60") is AKF50_640x480
    assert ef.entry_for(entries, "X640 Y480 C256 F72") is at_72
