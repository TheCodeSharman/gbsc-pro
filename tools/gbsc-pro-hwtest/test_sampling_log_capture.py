"""Reading a SamplingLog capture back.

No hardware. The console carries three kinds of frame on one socket -- samples,
solves, and status frames beginning '#' -- so a reader that takes the stream at
face value scores whatever else was being broadcast at the time.
"""

import pytest

from sampling_log import lock_statistics, parse

CAPTURE = """#5A@YC
smp,header,ms,divider,pllad_lock,sp_vtotal,sp_htotal,hperiod_if,vperiod_if,hsact,ifbits,intstatus
sol,header,ms,vds_hsync_rst,vds_vsync_rst,vds_hscale,vds_vscale,dis_hb_st,dis_hb_sp
sol,55357,1915,1124,613,589,1813,142
smp,23,2200,1,311,2200,511,55,1,12,0
smp,48,2200,0,311,2201,511,56,1,12,0
smp,73,2200,1,311,2199,511,57,1,12,0
smp,done
"""


def test_only_the_samples_are_parsed():
    assert len(parse(CAPTURE.splitlines())["ms"]) == 3


def test_a_status_frame_is_not_a_sample():
    """Frames beginning '#' are the web UI's status, not terminal text."""
    assert parse(["#5A@YC", "smp,23,2200,1,311,2200,511,55,1,12,0"])["lock"].tolist() == [1]


def test_the_named_columns_come_back_in_order():
    assert parse(CAPTURE.splitlines())["htotal"].tolist() == [2200, 2201, 2199]


def test_the_lock_duty_is_the_mean_of_the_bit():
    assert lock_statistics(parse(CAPTURE.splitlines()))["duty"] == pytest.approx(2 / 3)


def test_every_change_of_the_lock_bit_is_a_transition():
    assert lock_statistics(parse(CAPTURE.splitlines()))["transitions"] == 2


def test_the_sync_processor_is_scored_against_the_divider_not_against_itself():
    """STATUS_SYNC_PROC_HTOTAL echoes PLLAD_MD when the PLL is locked, so the
    honest health metric is the difference, which has a right answer of zero."""
    stats = lock_statistics(parse(CAPTURE.splitlines()))
    assert stats["offset_min"] == -1
    assert stats["offset_max"] == 1
    assert stats["offset_zero"] == pytest.approx(1 / 3)


def test_a_capture_with_no_samples_is_not_an_exception():
    assert len(parse(["smp,done"])["ms"]) == 0
