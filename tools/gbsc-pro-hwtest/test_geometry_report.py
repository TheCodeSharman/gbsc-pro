"""geometry.py's report, over a register set that needs no unit to produce.

report() is a pure function of the dict read_all() returns, so the arithmetic a
bench session reads off the screen can be pinned here rather than checked by eye
against a live board at 1am.

The numbers below are the bench unit on 2026-08-08: RISC PC at 320x256@50 into a
1445 x 1126 raster, with the picture 1117 lines tall inside a 1080-line encoder
window -- the state where the icon bar was clipped off the bottom.
"""

import geometry

BENCH = {
    "STATUS_16": 0x0F,
    "HPERIOD_IF": 431,
    "HPERIOD_SAMPLE": {"median": 431, "spread": 0, "state": "ok", "n": 8},
    "VTOTAL": 311,
    "HLOW_LEN": 181,
    "PLLAD_MD": 2553,
    "IF_HSYNC_RST": 1276,
    # Line-doubled, as the 15 kHz bench source is: PLLAD_MD 2553 -> 1276.
    "IF_PRGRSV_CNTRL": 0,
    "IF_LD_RAM_BYPS": 0,
    "IF_LD_SEL_PROV": 0,
    "IF_HS_DEC_FACTOR": 1,
    "IF_HB_SP2": 144,
    "IF_HB_ST2": 1074,
    "SP_RT_HS_SP": 2374,
    "VDS_HSYNC_RST": 1444,
    "VDS_HS_ST": 32,
    "VDS_HS_SP": 217,
    "VDS_HB_ST": 1443,
    "VDS_HB_SP": 9,
    "VDS_DIS_HB_ST": 1345,
    "VDS_DIS_HB_SP": 98,
    "VDS_HSCALE": 762,
    "VDS_HSCALE_BYPS": 0,
    # vertical
    "VDS_VSYNC_RST": 1125,
    "VDS_VS_ST": 0,
    "VDS_VS_SP": 6,
    "VDS_VB_ST": 1124,
    "VDS_VB_SP": 1,
    "VDS_DIS_VB_ST": 1118,
    "VDS_DIS_VB_SP": 4,
    "VDS_VSCALE": 307,
    "VDS_VSCALE_BYPS": 0,
    "IF_VB_ST": 479,
    "IF_VB_SP": 144,
}


def rendered(overrides=None):
    state = dict(BENCH)
    state.update(overrides or {})
    return geometry.report(state)


def test_reports_the_vertical_capture_window():
    assert "capture 144 .. 479   = 335 lines" in rendered()


def test_reports_what_the_scaler_produces_vertically():
    # 335 capture lines x 1024 / 307 = 1117.39
    assert "335 capture lines -> 1117.39 lines" in rendered()


def test_reports_the_vertical_display_window():
    assert "display blanking active  4 .. 1118   = 1114 lines" in rendered()


def test_says_when_the_picture_is_taller_than_the_encoder_can_send():
    # The encoder's active window is 1080 lines. A 1117-line picture loses 37 of
    # them at some position, and no amount of panning recovers them.
    assert "TALLER than the encoder" in rendered()


def test_says_nothing_about_encoder_overflow_when_the_picture_fits():
    fits = rendered({"VDS_VSCALE": 330})  # 335 x 1024 / 330 = 1039.5 lines
    assert "TALLER than the encoder" not in fits


def test_reports_where_the_picture_sits_against_the_encoder_window():
    # Display window 4..1118 against the encoder's 41..1121: 37 lines of picture
    # above the top of what is transmitted, 3 lines of unused room below.
    assert "encoder window  top -37   bottom -3" in rendered()


def test_horizontal_reporting_still_works():
    assert "930 capture units -> 1249.76 px" in rendered()
