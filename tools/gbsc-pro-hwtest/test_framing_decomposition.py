"""framing_decomposition's arithmetic, against the firmware's own pinned values
and synthetic frames. No hardware.

The output-side port reproduces what test/test_output_mode.cpp asserts of
OutputMode::solve(), so a constant moving in the C++ fails here in the same
commit -- the port is the PREDICTED column of a sweep record, not a spec.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import framing_decomposition as fd


# --- the raster: OutputMode::horizontalTotalFor() and clockDividerFor() -------

def test_the_frame_lines_gbs_unit_waits_for_are_the_modes_own():
    import gbs_unit
    for output, mode in fd.MODES.items():
        assert fd.frame_lines(mode) == gbs_unit.OUTPUT_FRAME_LINES[output]


def test_the_line_total_is_the_clock_budget_floored_then_made_even():
    # 108 MHz over 1125 lines at 50 Hz is 1920 exactly; at 50.08 Hz 1916.9,
    # floored to 1916 and already even.
    assert fd.horizontal_total_for(108_000_000, 1125, 50.0) == 1920
    assert fd.horizontal_total_for(108_000_000, 1125, 50.08) == 1916


def test_an_odd_total_is_biased_up_so_vds_hsync_rst_lands_odd():
    # 108e6 / 60.317 / 628-line frames is not the point; construct one: 1001
    # clocks per line floors to 1001 and comes back 1002.
    hz = 1001 * 1125 * 50
    assert fd.horizontal_total_for(hz, 1125, 50.0) == 1002


def test_the_largest_seed_under_the_ceiling_is_taken():
    assert fd.clock_for("1080p", 50.0, 108_000_000) == 108_000_000
    assert fd.clock_for("1080p", 50.0, 129_600_000) == 129_600_000


def test_a_mode_the_encoder_cannot_transmit_is_refused():
    # 2200 x 1125 x 70 Hz is 173 MHz against the encoder's 165.
    assert fd.solve_raster("1080p", 70.0) is None
    assert fd.solve_raster("1080p", 60.0) is not None


# --- the encoder window: OutputMode::solve() -----------------------------------

def test_1080p_at_50hz_and_108mhz_reproduces_the_pinned_raster():
    raster = fd.solve_raster("1080p", 50.0)
    assert raster["T"] == 1920
    assert raster["E0m"] == 140 + fd.TRANSMITTED_WINDOW_DELAY_PX
    assert raster["E1m"] - raster["E0m"] == 1675      # 1920 x 1920 / 2200, floored


def test_1024p_at_50hz_reproduces_the_pinned_raster():
    raster = fd.solve_raster("1024p", 50.0)
    assert raster["T"] == 2026
    assert raster["E0m"] == 360 + fd.TRANSMITTED_WINDOW_DELAY_PX
    assert raster["E1m"] - raster["E0m"] == 1536      # 2026 x 1280 / 1688


def test_1080p_at_129_6mhz_widens_the_raster_and_the_window():
    raster = fd.solve_raster("1080p", 50.0, ceiling_hz=129_600_000)
    assert raster["T"] == 2304
    assert raster["E1m"] - raster["E0m"] == 2010


def test_the_window_is_placed_for_a_total_read_off_the_chip():
    # The sweep has the raster the ENGINE solved, VDS_HSYNC_RST + 1, and the
    # model is asked where the window sits inside THAT line.
    window = fd.predicted_window("1080p", 1916, 50.08)
    assert window["E0m"] == 160
    assert window["E1m"] == 160 + (1916 * 1920) // 2200


def test_above_the_rate_where_the_porch_no_longer_fits_the_start_gives_way():
    # 960p at 75 Hz: T 1440, sync + porch 424 as durations, span 1024 as a
    # fraction, lastUsable 1424 -- the start moves back to 400 and the stop is
    # 1424, which is what the bench measured as 1423.
    window = fd.predicted_window("960p", 1440, 75.0)
    assert window["E1m"] == 1424
    assert window["E0m"] == 1424 - 1024


def test_the_vertical_window_is_the_standards_lines():
    window = fd.predicted_window("1080p", 1920, 50.0)
    assert window["V0m"] == 5 + 36
    assert window["V1m"] == 1125 - 4


# --- columns and units ----------------------------------------------------------

def test_the_dongle_column_slope_is_the_encoder_line_over_our_total():
    # At 1080p the encoder resamples our T into 2200, and 1920 of those are
    # emitted, so one of our units is 2200/T columns.
    cols, rows = fd.slope_predicted("1080p", 1600)
    assert abs(cols - 2200 / 1600) < 1e-9
    assert rows == 1.0


def test_a_720p_output_is_stretched_to_the_capture_by_the_dongle():
    cols, rows = fd.slope_predicted("720p", 1800)
    assert abs(cols - 1920 / (1800 * 1280 / 1650)) < 1e-9
    assert abs(rows - 1080 / 720) < 1e-9


def test_a_column_is_converted_through_the_measured_window_start_not_the_aperture():
    assert fd.columns_to_units(0.0, e0=157.3, slope=1.375) == 157.3
    assert abs(fd.columns_to_units(11.0, e0=157.3, slope=1.375) - (157.3 + 8.0)) < 1e-9


# --- the register-only positions -------------------------------------------------

REGS = {"VDS_HSYNC_RST": 1599, "VDS_HSCALE": 512, "VDS_HB_SP": 40,
        "VDS_DIS_HB_SP": 147, "VDS_DIS_HB_ST": 1747, "IF_HB_SP2": 259, "IF_HB_ST2": 1061,
        "VDS_VSYNC_RST": 1124, "VDS_VSCALE": 512, "VDS_VB_SP": 30,
        "VDS_DIS_VB_SP": 36, "VDS_DIS_VB_ST": 1116, "IF_VB_SP": 33, "IF_VB_ST": 517}
GEOMETRY = {"oh": 260, "eh": 800, "ov": 35, "ev": 480}


def test_the_picture_is_placed_from_the_write_model_and_the_scale():
    h = fd.positions(REGS, GEOMETRY)["h"]
    assert h["T"] == 1600
    assert h["m"] == 2.0
    assert h["A0"] == 147 and h["A1"] == 1747
    assert h["W0"] == 40 + 55 + 25 * 2.0                 # VDS_HB_SP + 55 + 25m
    assert h["P0"] == h["W0"] + 1 * 2.0                  # one capture unit of margin, scaled
    assert h["P1"] == h["W0"] + (1061 - 259 - 1) * 2.0   # the pair less the far margin


def test_the_vertical_picture_uses_the_vertical_write_model():
    v = fd.positions(REGS, GEOMETRY)["v"]
    assert v["T"] == 1125
    assert abs(v["W0"] - (30 + 0.2 + 0.8 * 2.0)) < 1e-9
    assert abs(v["P0"] - (v["W0"] + 2 * 2.0)) < 1e-9             # two units of margin vertically
    assert abs(v["P1"] - (v["W0"] + (517 - 33 - 2) * 2.0)) < 1e-9


def test_a_capture_narrower_than_the_ask_is_flagged_as_clamped():
    clamped = dict(REGS, IF_HB_ST2=1000)                 # 741 units against 800 asked
    assert fd.positions(clamped, GEOMETRY)["h"]["clamped"]
    assert not fd.positions(REGS, GEOMETRY)["h"]["clamped"]


# --- the four terms ---------------------------------------------------------------

def test_the_four_terms_sum_to_the_black_at_the_edge():
    pos = dict(A0=160.0, A1=1832.0, P0=161.5, P1=1830.0)
    window = dict(E0=157.3, E1=1841.0, E0m=160, E1m=1832)
    content = dict(C0=165.4, C1=1829.5)
    got = fd.decompose(pos, window, content, slope=1.375, allowance=(1, 2))
    left, right = got["near"], got["far"]
    assert abs(sum(left[k] for k in fd.TERMS) - (165.4 - 157.3)) < 1e-9
    assert abs(sum(right[k] for k in fd.TERMS) - (1841.0 - 1829.5)) < 1e-9


def test_the_largest_term_names_the_stage_that_owns_the_black():
    pos = dict(A0=160.0, A1=1832.0, P0=160.5, P1=1831.5)
    window = dict(E0=160.0, E1=1841.0, E0m=160, E1m=1832)   # the model stops 9 units short
    content = dict(C0=160.5, C1=1831.5)
    got = fd.decompose(pos, window, content, slope=1.15, allowance=(1, 2))
    assert got["near"]["verdict"] == "flush"
    assert got["far"]["verdict"] == "dEncoder"
    assert abs(got["far"]["black_cols"] - 9.5 * 1.15) < 1e-9


def test_a_border_off_the_frame_with_no_black_beside_it_is_a_clip():
    pos = dict(A0=160.0, A1=1832.0, P0=160.5, P1=1831.5)
    window = dict(E0=160.0, E1=1832.0, E0m=160, E1m=1832)
    content = dict(C0=None, C1=1831.5, black0=0, black1=0)
    got = fd.decompose(pos, window, content, slope=1.15, allowance=(1, 2))
    assert got["near"]["verdict"] == "clipped" and got["near"]["clipped"]
    assert got["far"]["verdict"] == "flush" and not got["far"]["clipped"]


def test_black_beside_a_border_off_the_frame_is_still_attributed():
    # The window opens 6.7 units before the aperture, 9 columns of black, and
    # the source's outermost pixel is not on the frame: both at once, and the
    # black is the encoder's whether or not the card's edge can be seen.
    pos = dict(A0=160.0, A1=1832.0, P0=160.4, P1=1831.5)
    window = dict(E0=153.3, E1=1832.0, E0m=160, E1m=1832)
    content = dict(C0=None, C1=1831.5, black0=9, black1=0)
    got = fd.decompose(pos, window, content, slope=1.375, allowance=(1, 2))
    assert got["near"]["verdict"] == "dEncoder+clip"
    assert abs(got["near"]["black_cols"] - 9) < 1e-9


def test_a_missing_measurement_leaves_its_term_unattributed():
    pos = dict(A0=160.0, A1=1832.0, P0=160.5, P1=1831.5)
    window = dict(E0=None, E1=None, E0m=160, E1m=1832)
    content = dict(C0=None, C1=1831.5)
    got = fd.decompose(pos, window, content, slope=1.15, allowance=(1, 2))
    assert got["near"]["dEncoder"] is None and got["near"]["dCapture"] is None
    assert got["near"]["verdict"] == "unmeasured"
    assert got["far"]["dEncoder"] is None
    assert got["far"]["verdict"] == "flush"                # judged against the model's edge


# --- reading the frame --------------------------------------------------------------

def grey_frame(width=200, height=20, fill=120):
    return np.full((height, width), float(fill), np.float32)


def test_edge_profiles_run_inward_from_each_edge():
    columns = grey_frame()
    columns[:, :5] = 0         # five black columns at the left
    profiles = fd.edge_profiles(columns, depth=8)
    assert list(profiles["left"][:6]) == [0, 0, 0, 0, 0, 120]
    assert len(profiles["right"]) == 8 and profiles["right"][0] == 120

    rows = grey_frame()
    rows[-3:, :] = 0           # three black rows at the bottom
    assert list(fd.edge_profiles(rows, depth=8)["bottom"][:4]) == [0, 0, 0, 120]


def test_black_is_counted_from_the_edge_and_stops_at_the_first_lit_column():
    profile = np.array([10, 12, 15, 90, 5, 5], np.float32)
    assert fd.black_extent(profile) == 3


def test_the_fetch_ramp_past_the_picture_is_not_black():
    # 235 falling to 75 following the last written column reads as content to
    # a brightness test; it is not blanking and must not count as black.
    ramp = np.linspace(235, 75, 14).astype(np.float32)
    assert fd.black_extent(ramp) == 0


def test_a_walk_whose_black_grows_from_the_first_step_says_so():
    assert fd.walk_signature([(160, 0), (180, 22), (200, 45), (220, 68)]) == "grows"
    assert fd.walk_signature([(1832, 30), (1812, 8), (1792, 0), (1772, 0)]) == "shrinks"
    assert fd.walk_signature([(160, 3), (180, 3), (200, 4), (220, 3)]) == "constant"


def test_the_card_edges_are_the_green_centroids_in_columns():
    clip = np.zeros((1, 20, 400, 3), np.uint8)
    clip[0, :, 50, 1] = 255
    clip[0, :, 250, 1] = 255
    near, far, residual = fd.card_columns(clip, axis=1, expected_span=200.0)
    assert near == 50.0 and far == 250.0 and residual == 0.0


def test_a_border_off_the_frame_leaves_that_edge_unread():
    clip = np.zeros((1, 20, 400, 3), np.uint8)
    clip[0, :, 50, 1] = 255
    near, far, residual = fd.card_columns(clip, axis=1, expected_span=200.0)
    assert near == 50.0 and far is None and residual is None


def test_the_pan_probe_gives_the_slope_from_a_known_move():
    # Eight capture units at 2x magnification are sixteen raster units, seen
    # as 22 columns: 1.375 columns per unit.
    assert abs(fd.slope_from_pan(shift_cols=22.0, pan_units=8, magnification=2.0) - 1.375) < 1e-9


# --- what the first bench state taught the instrument -----------------------------

def test_a_lone_green_edge_is_filed_by_which_half_of_the_frame_it_is_in():
    clip = np.zeros((1, 20, 400, 3), np.uint8)
    clip[0, :, 390, 1] = 255
    near, far, _ = fd.card_columns(clip, axis=1, expected_span=200.0)
    assert near is None and far == 390.0


def test_the_picture_is_placed_from_the_registers_and_the_axis_margins():
    # The capture register pair is the picture plus one margin unit each end
    # horizontally and two vertically (Axis.cpp), so the picture needs no
    # reading of the framing's ask -- whose origin is not the register's.
    h = fd.positions(REGS, GEOMETRY)["h"]
    assert h["P0"] == h["W0"] + fd.CAPTURE_MARGIN_H * h["m"]
    assert h["P1"] == h["W0"] + ((1061 - 259) - fd.CAPTURE_MARGIN_H) * h["m"]
    v = fd.positions(REGS, GEOMETRY)["v"]
    assert abs(v["P0"] - (v["W0"] + fd.CAPTURE_MARGIN_V * v["m"])) < 1e-9
    assert abs(v["P1"] - (v["W0"] + ((517 - 33) - fd.CAPTURE_MARGIN_V) * v["m"])) < 1e-9


def test_a_window_edge_falls_back_to_the_black_count_where_the_strips_found_nothing():
    walked = dict(h_near=dict(strip_zero=153.4, black_zero=152.7),
                  h_far=dict(strip_zero=1549.9, black_zero=1550.0),
                  v_near=dict(strip_zero=40.0, black_zero=40.6),
                  v_far=dict(strip_zero=None, black_zero=1123.2))
    window, instruments = fd.measured_window(walked)
    assert window["E0"] == 153.4 and window["V1"] == 1123.2
    assert instruments["V1"] == "black" and instruments["E0"] == "strip"


# --- the mode the registers carry ----------------------------------------------------

def test_the_carried_output_is_read_off_the_frame_lines():
    assert fd.carried_output({"VDS_VSYNC_RST": 1065}) == "1024p"
    assert fd.carried_output({"VDS_VSYNC_RST": 1124}) == "1080p"
    assert fd.carried_output({"VDS_VSYNC_RST": 999}) == "960p"
    assert fd.carried_output({"VDS_VSYNC_RST": 700}) is None


def test_a_record_modelled_as_the_mode_asked_is_remodelled_as_the_mode_carried():
    # 640x480@73 asked for 1080p; the encoder cannot carry 1080p at 72.8 Hz and
    # the engine solved 1024p: 1066 lines, a 1392-unit line at 108 MHz.
    regs = dict(REGS, VDS_VSYNC_RST=1065, VDS_HSYNC_RST=1391, STATUS_SYNC_PROC_VTOTAL=519)
    geometry = dict(GEOMETRY, lineRateHz=37861)
    rate = fd.field_rate_of(regs, geometry)
    record = dict(output="1080p", registers=regs, geometry=geometry,
                  window=dict(fd.predicted_window("1080p", 1392, rate), E0=320.5, E1=1377.2, V0=None, V1=None),
                  slope=dict(h_pred=2200 / 1392, v_pred=1.0),
                  positions=fd.positions(regs, geometry),
                  card=dict(h=(4.0, 1910.0, 0.0), v=(None, 1070.0, None)))
    fd.remodel(record)
    want = fd.predicted_window("1024p", 1392, rate)
    assert record["carried"] == "1024p"
    assert record["window"]["E0m"] == want["E0m"] and record["window"]["E1m"] == want["E1m"]
    assert record["window"]["V0m"] == want["V0m"] and record["window"]["V1m"] == want["V1m"]
    cols, rows = fd.slope_predicted("1024p", 1392)
    assert record["slope"]["h_pred"] == cols and record["slope"]["v_pred"] == rows
    assert abs(record["positions"]["h"]["C0"] - fd.columns_to_units(4.0, 320.5, cols)) < 1e-9
    assert abs(record["positions"]["v"]["C1"] - fd.columns_to_units(1070.0, want["V0m"], rows)) < 1e-9
    assert record["positions"]["v"]["C0"] is None
