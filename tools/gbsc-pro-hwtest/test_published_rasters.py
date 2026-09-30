"""published_rasters against the firmware's own SourceTiming tables. No hardware.

The rows are read out of SourceTiming.cpp rather than copied, so a row added or
a polarity changed in the C++ is what this predicts from.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import published_rasters as pr

AKF50_640x480 = {"x_res": "640", "y_res": "480", "pixel_rate": "25175",
                 "h_timings": "94,22,22,640,22,0", "v_timings": "2,32,0,480,0,11",
                 "sync_pol": "3"}
AKF50_360x480 = {"x_res": "360", "y_res": "480", "pixel_rate": "16783",
                 "h_timings": "64,46,16,360,16,30", "v_timings": "2,32,0,480,0,11",
                 "sync_pol": "3"}
AKF50_800x600 = {"x_res": "800", "y_res": "600", "pixel_rate": "40000",
                 "h_timings": "128,48,40,800,40,0", "v_timings": "4,23,0,600,0,1",
                 "sync_pol": "0"}


def test_every_row_is_read_with_its_authority_and_polarity():
    rows = pr.rows()
    assert len(rows) >= 43
    dmt = [r for r in rows if r["authority"] == "Dmt" and r["name"] == "640x480@60"][0]
    assert dmt["frame"] == 525 and dmt["total"] == 800 and dmt["start"] == 144
    assert (dmt["hpol"], dmt["vpol"]) == ("N", "N")


def test_a_mode_file_entry_yields_the_key_the_chip_would_measure():
    key = pr.key_of(AKF50_640x480)
    assert key["frame"] == 525
    assert abs(key["rate"] - 59.94) < 0.01
    assert abs(key["duty"] - 94 / 800) < 1e-9
    assert (key["hpol"], key["vpol"]) == ("N", "N")
    assert abs(key["h_start"] - 138 / 800) < 1e-9 and abs(key["h_extent"] - 640 / 800) < 1e-9


def test_the_acorn_640x480_lands_on_the_dmt_row_six_pixels_off():
    row = pr.match(pr.rows(), pr.key_of(AKF50_640x480))
    assert row["authority"] == "Dmt" and row["name"] == "640x480@60"
    cost = pr.expected_offsets(AKF50_640x480, row)
    assert abs(cost["left"] - 6.0) < 1e-6 and abs(cost["right"] - 6.0) < 1e-6
    assert abs(cost["top"] - 1.0) < 1e-6 and abs(cost["bottom"] - 1.0) < 1e-6


def test_a_different_layout_on_the_same_key_costs_tens_of_pixels():
    row = pr.match(pr.rows(), pr.key_of(AKF50_360x480))
    assert row["name"] == "640x480@60"
    cost = pr.expected_offsets(AKF50_360x480, row)
    assert abs(cost["left"] - (-30.2)) < 0.05 and abs(cost["right"] - 35.4) < 0.05


def test_an_identical_layout_costs_nothing():
    row = pr.match(pr.rows(), pr.key_of(AKF50_800x600))
    cost = pr.expected_offsets(AKF50_800x600, row)
    assert all(abs(cost[edge]) < 1e-6 for edge in ("left", "right", "top", "bottom"))


def test_the_polarity_pair_separates_the_85hz_dmt_pair():
    at350 = {"x_res": "640", "y_res": "350", "pixel_rate": "31500",
             "h_timings": "64,96,0,640,0,32", "v_timings": "3,60,0,350,0,32", "sync_pol": "2"}
    at400 = dict(at350, y_res="400", v_timings="3,41,0,400,0,1", sync_pol="1")
    rows = pr.rows()
    assert pr.match(rows, pr.key_of(at350))["name"] == "640x350@85"
    assert pr.match(rows, pr.key_of(at400))["name"] == "640x400@85"


def test_a_framing_proportion_names_the_row_it_came_from():
    rows = pr.rows()
    assert pr.tier_answered(rows, {"poh": 2048, "peh": 7576, "pov": 430, "pev": 9554})["name"] == "800x600@60"
    assert pr.tier_answered(rows, {"poh": 1170, "peh": 8640, "pov": 610, "pev": 9330})["name"] == "envelope"
