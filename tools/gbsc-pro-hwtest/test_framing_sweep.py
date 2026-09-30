"""framing_sweep's pure parts: the mode file check, the mode lookup and the
per-state analysis, against synthetic frames. No hardware."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import framing_decomposition as fd
import framing_sweep as fs

ENTRIES = [
    {"x_res": "640", "y_res": "480", "pixel_rate": "25175",
     "h_timings": "94,22,22,640,22,0", "v_timings": "2,32,0,480,0,11", "sync_pol": "3"},
    {"x_res": "800", "y_res": "600", "pixel_rate": "40000",
     "h_timings": "128,48,40,800,40,0", "v_timings": "4,23,0,600,0,1", "sync_pol": "0"},
    {"x_res": "800", "y_res": "600", "pixel_rate": "36000",
     "h_timings": "72,84,34,800,34,0", "v_timings": "2,22,0,600,0,1", "sync_pol": "0"},
]


def test_the_mode_file_must_offer_every_mode_the_source_lists():
    listed = ["X640 Y480 C256 F60", "X800 Y600 C256 F60", "X800 Y600 C256 F56"]
    assert fs.verify_mdf(listed, ENTRIES) == []
    assert fs.verify_mdf(listed + ["X320 Y256 C256 F50"], ENTRIES) == ["X320 Y256 C256 F50"]


def test_a_mode_request_finds_its_entry_by_resolution_and_nearest_rate():
    assert fs.mdf_entry_for(ENTRIES, "X800 Y600 C256 F56")["pixel_rate"] == "36000"
    assert fs.mdf_entry_for(ENTRIES, "X800 Y600 C256 F60")["pixel_rate"] == "40000"
    assert fs.mdf_entry_for(ENTRIES, "X320 Y256 C256 F50") is None


def test_every_tier_names_real_outputs_and_a_probe_level():
    for name, tier in fs.TIERS.items():
        assert tier["probes"] in fs.PROBE_LEVELS, name
        assert all(output in fs.OUTPUTS for output in tier["outputs"]), name


REGS = {"VDS_HSYNC_RST": 1599, "VDS_HSCALE": 512, "VDS_HB_SP": 40,
        "VDS_DIS_HB_SP": 147, "VDS_DIS_HB_ST": 1747, "IF_HB_SP2": 259, "IF_HB_ST2": 1061,
        "VDS_VSYNC_RST": 1124, "VDS_VSCALE": 512, "VDS_VB_SP": 30,
        "VDS_DIS_VB_SP": 41, "VDS_DIS_VB_ST": 1121, "IF_VB_SP": 33, "IF_VB_ST": 517,
        "STATUS_SYNC_PROC_VTOTAL": 524}
GEOMETRY = {"oh": 260, "eh": 800, "ov": 35, "ev": 480, "lineRateHz": 31500,
            "poh": 1800, "peh": 8000, "pov": 667, "pev": 9143}


def synthetic_clip(left_green, right_green, top_green, bottom_green):
    clip = np.full((2, 1080, 1920, 3), 80, np.uint8)
    clip[:, :, left_green, 1] = 255
    clip[:, :, right_green, 1] = 255
    clip[:, top_green, :, 1] = 255
    clip[:, bottom_green, :, 1] = 255
    return clip


def test_the_analysis_places_the_card_in_raster_units_through_the_window():
    # 1080p, T 1600: the model's window opens at 160 and 1920 columns span
    # 1600 x 1920 / 2200 = 1396.4 units, 1.375 columns each. A green column at
    # 0 is unit 160; one at 1919 is unit 160 + 1919 / 1.375.
    got = fs.analyse_default("1080p", REGS, GEOMETRY, synthetic_clip(0, 1919, 0, 1079))
    assert got["window"]["E0m"] == 160
    assert abs(got["slope"]["h_pred"] - 2200 / 1600) < 1e-9
    assert abs(got["positions"]["h"]["C0"] - 160.0) < 1e-6
    assert abs(got["positions"]["h"]["C1"] - (160.0 + 1919 / 1.375)) < 1e-6
    assert got["positions"]["v"]["C0"] == 41.0
    assert abs(got["positions"]["v"]["C1"] - (41.0 + 1079)) < 1e-6


def test_black_at_an_edge_is_counted_and_attributed():
    clip = synthetic_clip(20, 1900, 0, 1079)
    clip[:, :, :20, :] = 0                       # our blanking inside the window
    got = fs.analyse_default("1080p", REGS, GEOMETRY, clip)
    assert got["black_cols"]["left"] == 20
    assert got["residuals"]["h"]["near"]["verdict"] != "flush"


# --- what refuses a record, and what names its frame ----------------------------------

def test_a_status_counter_dithering_between_the_two_reads_does_not_refuse_the_record():
    # The sync processor's counters read a unit either way on a healthy unit;
    # they are measurements beside the clip, not the state the clip was taken in.
    before = {"VDS_HSCALE": 848, "STATUS_SYNC_PROC_HTOTAL": 1444, "STATUS_SYNC_PROC_HLOW_LEN": 168}
    after = dict(before, STATUS_SYNC_PROC_HTOTAL=1445, STATUS_SYNC_PROC_HLOW_LEN=169)
    assert fs.moved_fields(before, after) == []


def test_a_solved_register_moving_between_the_two_reads_is_named():
    before = {"VDS_HSCALE": 848, "VDS_DIS_HB_SP": 160, "STATUS_SYNC_PROC_HTOTAL": 1444}
    after = dict(before, VDS_HSCALE=850, VDS_DIS_HB_SP=162)
    assert fs.moved_fields(before, after) == ["VDS_HSCALE", "VDS_DIS_HB_SP"]


def test_each_record_of_a_repeated_state_keeps_its_own_frame(tmp_path, monkeypatch):
    monkeypatch.setattr(fs.hdmi_capture, "write_png", lambda path, frame: None)
    ctx = dict(png_dir=str(tmp_path))
    first = fs.save_png(ctx, "X640 Y480 C256 F60", "1080p", "default", None)
    second = fs.save_png(ctx, "X640 Y480 C256 F60", "1080p", "default", None)
    other = fs.save_png(ctx, "X640 Y480 C256 F60", "1080p", "zoom", None)
    assert first != second
    assert first.endswith("X640-Y480-C256-F60-1080p-default.png")
    assert other.endswith("X640-Y480-C256-F60-1080p-zoom.png")


def test_the_analysis_models_the_mode_the_registers_carry_not_the_one_asked():
    regs = dict(REGS, VDS_VSYNC_RST=1065, VDS_HSYNC_RST=1391, STATUS_SYNC_PROC_VTOTAL=519)
    geometry = dict(GEOMETRY, lineRateHz=37861)
    clip = synthetic_clip(20, 1900, 0, 1079)
    got = fs.analyse_default("1080p", regs, geometry, clip)
    want = fd.predicted_window("1024p", 1392, fd.field_rate_of(regs, geometry))
    assert got["carried"] == "1024p"
    assert got["window"]["E0m"] == want["E0m"]
    assert got["slope"]["h_pred"] == fd.slope_predicted("1024p", 1392)[0]
