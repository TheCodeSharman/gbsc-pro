"""framing_report's table and fits, against synthetic records. No hardware."""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import framing_report as fr


def record(source, output, total, rate, e0m, e0, e1m, e1, sync_porch=140, verdict="ok"):
    d_enc_near = None if e0 is None else e0m - e0
    d_enc_far = None if e1 is None else e1 - e1m
    black_near = None if e0 is None else (e0m + 0.5) - e0
    black_far = None if e1 is None else e1 - (e1m - 1.0)
    return dict(source=source, output=output, verdict=verdict,
                window=dict(T=total, E0m=e0m, E0=e0, E1m=e1m, E1=e1, V0m=41, V1m=1121,
                            V0=None, V1=None, clock_hz=total * 1125 * rate,
                            hsync=(13, 13 + 32)),
                registers={"STATUS_SYNC_PROC_VTOTAL": 524}, geometry=dict(lineRateHz=525 * rate, ch=2047, cv=1050),
                sync_porch=sync_porch,
                slope=dict(h_pred=2200 / total, v_pred=1.0, h_near=None, h_far=None),
                black_cols=dict(left=0, right=14, top=0, bottom=0),
                positions=dict(h=dict(m=1.5, A0=e0m, A1=e1m, P0=e0m + 0.5, P1=e1m - 1.0,
                                      C0=e0m + 0.5, C1=e1m - 1.0),
                               v=dict(m=2.0, A0=41, A1=1121, P0=41.2, P1=1120.8, C0=41.2, C1=1120.8)),
                residuals=dict(
                    h=dict(near=dict(dCapture=0.0, dPlace=0.5, dModelApplied=0.0, dEncoder=d_enc_near,
                                     black_units=black_near, black_cols=0.0, verdict="flush"),
                            far=dict(dCapture=0.0, dPlace=1.0, dModelApplied=0.0, dEncoder=d_enc_far,
                                     black_units=black_far, black_cols=14.0, verdict="dEncoder")),
                    v=dict(near=dict(dCapture=0.0, dPlace=0.2, dModelApplied=0.0, dEncoder=None,
                                     black_units=0.2, black_cols=0.2, verdict="flush"),
                            far=dict(dCapture=0.0, dPlace=0.2, dModelApplied=0.0, dEncoder=None,
                                     black_units=0.2, black_cols=0.2, verdict="flush"))),
                expected=dict(key=dict(h_total=800, v_total=525),
                              offsets_px=dict(left=6.0, right=6.0, top=1.0, bottom=1.0)))


def test_a_row_carries_the_mode_the_raster_and_the_four_terms():
    row = fr.format_row(record("X640 Y480 C256 F60", "1080p", 1600, 60.0, 160, 157.3, 1556, 1560.0))
    assert "X640 Y480 C256 F60" in row and "1080p" in row and "1600" in row
    assert "dEnc" not in row                      # the header carries the names, not each row
    assert "+2.7" in row and "+4.0" in row        # E0m - E0 and E1 - E1m


def test_the_expected_capture_offset_is_converted_to_raster_units():
    # 6 source pixels of an 800-pixel line captured into 2047 units, magnified
    # 1.5x: 6 x 2047 / 800 x 1.5 = 23.0 raster units.
    got = fr.expected_units(record("X640 Y480 C256 F60", "1080p", 1600, 60.0, 160, 160, 1556, 1556))
    assert abs(got["left"] - 6 * 2047 / 800 * 1.5) < 1e-9
    assert abs(got["top"] - 1 * 1050 / 525 * 2.0) < 1e-9


def test_the_window_fit_separates_a_duration_from_a_fraction():
    # A start that is constant in T is a duration; one that grows with T is a
    # fraction. Three rasters with E0 pinned at 160 fit slope 0 against T.
    records = [record("a", "1080p", t, 108e6 / 1125 / t, 160, 160.0, 160 + t * 1920 // 2200,
                      160 + t * 1920 / 2200) for t in (1600, 1706, 1916)]
    fit = fr.window_fits(records)["1080p"]
    assert abs(fit["start_vs_total"]["slope"]) < 1e-9
    assert abs(fit["width_vs_fraction"]["slope"] - 1.0) < 1e-6


def test_a_run_with_no_measured_window_reports_nothing_to_fit():
    records = [record("a", "1080p", 1600, 60.0, 160, None, 1556, None)]
    assert fr.window_fits(records) == {}


def test_a_loaded_record_is_judged_by_the_current_decomposition(tmp_path):
    # Written under a decomposition that filed a border off the frame as
    # unmeasured. The raw record carries 9 black columns beside that border and
    # a window opening 6.7 units early, which is the encoder's black.
    r = record("X640 Y480 C256 F60", "1080p", 1600, 60.0, 160, 153.3, 1556, 1549.9,
               verdict="L:unmeasured")
    r["positions"]["h"]["C0"] = None
    r["black_cols"]["left"] = 9
    path = tmp_path / "run.jsonl"
    path.write_text(json.dumps(r) + "\n")
    loaded = fr.load(str(path))[0]
    assert loaded["verdict"].startswith("L:dEncoder+clip")
    assert loaded["residuals"]["h"]["near"]["clipped"]


def test_a_loaded_record_refits_its_walks_so_a_fit_of_a_constant_is_not_a_position(tmp_path):
    # Written before the fit refused a slope of nothing: the bottom walk counted
    # 278 black rows at every step and the stored fit put the window at -3e18.
    r = record("X800 Y600 C256 F60", "1080p", 1592, 60.32, 160, 157.7, 1549, 1546.7,
               verdict="B:dEncoder")
    r["window"]["V1"] = -2.8777071770543037e+18
    r["walk"] = dict(
        h_near=dict(strips=[[180, 31], [200, 59], [220, 86], [240, 114]], blacks=[[180, 31], [200, 64], [220, 87], [240, 122]],
                    strip_zero=157.7, black_zero=156.5),
        h_far=dict(strips=[[1527, 28], [1507, 55], [1487, 83], [1467, 111]], blacks=[[1527, 35], [1507, 57], [1487, 83], [1467, 114]],
                   strip_zero=1546.7, black_zero=1550.2),
        v_near=dict(strips=[[61, 20], [81, 40], [101, 60], [121, 80]], blacks=[[61, 20], [81, 40], [101, 60], [121, 80]],
                    strip_zero=39.9, black_zero=41.0),
        v_far=dict(strips=[], blacks=[[1101, 278], [1081, 278], [1061, 278], [1041, 278]],
                   strip_zero=None, black_zero=-2.8777071770543037e+18))
    path = tmp_path / "run.jsonl"
    path.write_text(json.dumps(r) + "\n")
    loaded = fr.load(str(path))[0]
    assert loaded["window"]["V1"] is None
    assert loaded["window"]["instruments"]["V1"] is None
    assert abs(loaded["window"]["E0"] - 157.7) < 0.5
    assert "dEncoder" not in loaded["residuals"]["v"]["far"]["verdict"]


def test_a_loaded_record_is_modelled_as_the_mode_its_registers_carry(tmp_path):
    r = record("X640 Y480 C256 F73", "1080p", 1392, 72.81, 162, 320.5, 1376, 1377.2)
    r["registers"] = {"STATUS_SYNC_PROC_VTOTAL": 519, "VDS_VSYNC_RST": 1065, "VDS_HSYNC_RST": 1391,
                      "VDS_HSCALE": 700, "VDS_HB_SP": 40, "VDS_DIS_HB_SP": 328, "VDS_DIS_HB_ST": 1383,
                      "IF_HB_SP2": 259, "IF_HB_ST2": 1061, "VDS_VSCALE": 512, "VDS_VB_SP": 30,
                      "VDS_DIS_VB_SP": 41, "VDS_DIS_VB_ST": 1065, "IF_VB_SP": 33, "IF_VB_ST": 517}
    r["geometry"].update(lineRateHz=37861, eh=800, ev=480, oh=260, ov=35)
    r["card"] = dict(h=(4.0, 1910.0, 0.0), v=(None, 1070.0, None))
    path = tmp_path / "run.jsonl"
    path.write_text(json.dumps(r) + "\n")
    loaded = fr.load(str(path))[0]
    assert loaded["carried"] == "1024p"
    assert "1024p" in fr.format_row(loaded)


def test_a_gzipped_run_loads_like_a_plain_one(tmp_path):
    import gzip
    r = record("X640 Y480 C256 F60", "1080p", 1600, 60.0, 160, 157.3, 1556, 1560.0)
    path = tmp_path / "run.jsonl.gz"
    with gzip.open(path, "wt") as handle:
        handle.write(json.dumps(r) + "\n")
    assert fr.load(str(path))[0]["source"] == "X640 Y480 C256 F60"


def test_the_acquisition_view_names_the_raster_each_state_came_from():
    # The same state twice in one run, reached first from another raster and
    # then from itself: the view keeps the run order and says what each lock
    # followed, since a placement that follows the transition rather than
    # the state only shows that way.
    records = [record("a", "1080p", 1600, 60.0, 160, 153.5, 1556, 1550.0),
               record("b", "1080p", 1916, 50.08, 160, 171.5, 1832, 1843.8),
               record("b", "1080p", 1916, 50.08, 160, 171.1, 1832, 1843.8)]
    rows = fr.acquisitions(records)
    assert [row["from_T"] for row in rows] == [None, 1600, 1916]
    assert abs(rows[1]["delay"] - (171.5 - 140)) < 1e-9
    assert abs(rows[1]["width_off"] - ((1843.8 - 171.5) - 1916 * 1920 / 2200)) < 1e-9


def test_the_acquisition_view_does_not_claim_a_raster_across_a_skipped_state():
    records = [record("a", "1080p", 1600, 60.0, 160, 153.5, 1556, 1550.0),
               dict(source="b", output="1080p", skipped="source refused the mode"),
               record("c", "1080p", 1916, 50.08, 160, 171.5, 1832, 1843.8)]
    rows = fr.acquisitions(records)
    assert [row["source"] for row in rows] == ["a", "c"]
    assert rows[1]["from_T"] is None


# --- the sink view --------------------------------------------------------------------

def sink_record(source, output, total, rate, e0, e1, black_left=30, moved=-24):
    """A state whose sink probe ran: the capture panned into the source's
    blanking, the pad toggled, the horizontal window walked."""
    stop = 160 + total * 1920 // 2200
    r = record(source, output, total, rate, 160, None, stop, None)
    r["sink"] = dict(pan=dict(asked=-24, moved_units=moved), black_left=black_left,
                     window=dict(T=total, carried=output, A0=160, A1=stop, E0m=160, E1m=stop,
                                 E0=e0, E1=e1, clock_hz=total * 1125 * rate,
                                 instruments=dict(E0="strip", E1="strip")))
    return r


def test_the_sink_view_reads_each_window_against_the_modes_sync_and_porch():
    rows = fr.sink_rows([sink_record("a", "1080p", 1916, 50.08, 171.6, 1844.5)])
    assert rows[0]["T"] == 1916
    assert abs(rows[0]["delay"] - (171.6 - 140)) < 1e-9
    assert abs(rows[0]["width_off"] - ((1844.5 - 171.6) - 1916 * 1920 / 2200)) < 1e-9
    assert rows[0]["own_position"]


def test_a_sink_row_with_no_black_at_the_edge_is_not_the_sinks_own_position():
    # The sink pulls the window to content that is earlier than its own
    # position, so a frame with the picture at column 0 measured the content.
    rows = fr.sink_rows([sink_record("a", "1080p", 1916, 50.08, 158.9, 1830.0, black_left=0)])
    assert not rows[0]["own_position"]


def test_the_sink_fits_run_on_the_sink_windows_and_the_rows_group_by_raster():
    records = [sink_record("a", "1080p", 1600, 60.0, 158.4, 1554.0),
               sink_record("b", "1080p", 1600, 60.0, 158.0, 1554.2),
               sink_record("c", "1080p", 1916, 50.08, 171.6, 1844.5)]
    assert fr.sink_fits(records)["1080p"]["points"] == 3
    groups = fr.raster_groups(fr.sink_rows(records))
    assert [g["n"] for g in groups] == [2, 1]
    assert abs(groups[0]["mean"] - 158.2) < 1e-9
    assert abs(groups[0]["spread"] - 0.4) < 1e-9


def test_a_loaded_sink_record_reads_its_window_off_its_walks_again(tmp_path):
    r = sink_record("a", "1080p", 1916, 50.08, 999.0, 1844.5)
    r["sink"]["walk"] = dict(
        h_near=dict(strips=[[180, 10], [200, 33], [220, 56], [240, 79]], blacks=[],
                    strip_zero=999.0, black_zero=None),
        h_far=dict(strips=[[1811, 38], [1791, 61], [1771, 84], [1751, 107]], blacks=[],
                   strip_zero=1844.5, black_zero=None))
    path = tmp_path / "run.jsonl"
    path.write_text(json.dumps(r) + "\n")
    loaded = fr.load(str(path))[0]
    assert abs(loaded["sink"]["window"]["E0"] - 171.3) < 0.5
    assert abs(loaded["sink"]["window"]["E1"] - 1844.0) < 0.5


def test_several_runs_load_as_one_in_the_order_given(tmp_path):
    first, second = tmp_path / "a.jsonl", tmp_path / "b.jsonl"
    first.write_text(json.dumps(record("a", "1080p", 1600, 60.0, 160, 157.3, 1556, 1560.0)) + "\n")
    second.write_text(json.dumps(record("b", "1080p", 1916, 50.08, 160, 171.6, 1832, 1843.8)) + "\n")
    assert [r["source"] for r in fr.load_runs([str(first), str(second)])] == ["a", "b"]


def test_a_loaded_sink_record_fits_its_black_count_past_the_flat_start(tmp_path):
    # Black at the edge is what the probe needs and what blinds the strip, so
    # the count is the instrument there -- and it says nothing until the
    # blanking has passed the content.
    r = sink_record("a", "1080p", 1916, 50.08, 88.9, 1844.7)
    r["sink"]["walk"] = dict(
        h_near=dict(strips=[], blacks=[[180, 75], [200, 75], [220, 75], [240, 78], [260, 101],
                                       [280, 124], [300, 147], [320, 170]],
                    strip_zero=None, black_zero=88.9),
        h_far=dict(strips=[[1811, 39], [1791, 61], [1771, 84], [1751, 108]], blacks=[],
                   strip_zero=1844.7, black_zero=None))
    path = tmp_path / "run.jsonl"
    path.write_text(json.dumps(r) + "\n")
    loaded = fr.load(str(path))[0]
    assert abs(loaded["sink"]["window"]["E0"] - 172.2) < 0.5
    assert loaded["sink"]["window"]["instruments"]["E0"] == "black"


def test_a_sink_row_carries_the_window_in_the_sinks_own_pixels():
    # The sink resamples our line into its standard's total, so a start that
    # is one count of ITS pixels reads as E0 x total / T: 171.6 units of a
    # 1916-unit line are 197.1 of 1080p's 2200.
    row = fr.sink_rows([sink_record("a", "1080p", 1916, 50.08, 171.6, 1844.5)])[0]
    assert abs(row["E0_sink_px"] - 171.6 * 2200 / 1916) < 1e-9
    assert abs(row["delay_sink_px"] - (171.6 - 140) * 2200 / 1916) < 1e-9
