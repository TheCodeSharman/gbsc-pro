"""framing_report's table and fits, against synthetic records. No hardware."""

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
