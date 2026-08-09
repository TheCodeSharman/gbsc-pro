"""Unit tests for the position-register bounds check. No hardware needed."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inrange


# A plausible bench state: 1277 unit IF line, 1445 px output line, 1126 line
# output frame, 624 half-line input frame, 2553 ADC samples per line.
BENCH = {
    "IF_HSYNC_RST": 1276, "STATUS_SYNC_PROC_VTOTAL": 311,
    "VDS_HSYNC_RST": 1444, "VDS_VSYNC_RST": 1125, "PLLAD_MD": 2553,
    "IF_HB_ST": 258, "IF_HB_SP": 72, "IF_HB_ST1": 1088, "IF_HB_SP1": 72,
    "IF_HB_ST2": 1143, "IF_HB_SP2": 134,
    "IF_LINE_ST": 64, "IF_LINE_SP": 1200,
    "IF_HBIN_ST": 0, "IF_HBIN_SP": 272,
    "IF_VB_ST": 578, "IF_VB_SP": 46, "IF_INI_ST": 0,
    "VDS_HB_ST": 1443, "VDS_HB_SP": 9,
    "VDS_DIS_HB_ST": 1348, "VDS_DIS_HB_SP": 95,
    "VDS_HS_ST": 8, "VDS_HS_SP": 56,
    "VDS_VB_ST": 1124, "VDS_VB_SP": 1,
    "VDS_DIS_VB_ST": 1120, "VDS_DIS_VB_SP": 3,
    "VDS_VS_ST": 1, "VDS_VS_SP": 6,
    "SP_RT_HS_ST": 0, "SP_RT_HS_SP": 2374,
    "SP_CS_CLP_ST": 26, "SP_CS_CLP_SP": 150,
    "SP_H_CST_ST": 42, "SP_H_CST_SP": 1667,
    "SP_CS_HS_ST": 0, "SP_CS_HS_SP": 0,
}


def check(**overrides):
    state = dict(BENCH, **overrides)
    return inrange.check(state.get)


def names(findings):
    return [f.name for f in findings]


def test_a_healthy_state_reports_nothing():
    assert check() == []


def test_it_catches_the_register_that_caused_the_zigzag():
    """IF_HB_ST at 1347 on a 1277 unit line, the state a nine round bisect reached
    on 2026-08-09. A bounds check names it in one pass, which is the whole reason
    this exists."""
    assert names(check(IF_HB_ST=1347)) == ["IF_HB_ST"]


def test_it_reports_the_bound_that_was_broken():
    finding = check(IF_HB_ST=1347)[0]
    assert finding.value == 1347
    assert finding.limit == 1277
    assert "IF_HSYNC_RST" in finding.because


def test_the_last_unit_of_the_line_is_allowed():
    """The capture STOP may reach units - 1 and no further -- that value is the
    wrap point, so it is legal and units itself is not."""
    assert check(IF_HB_ST2=1276) == []
    assert names(check(IF_HB_ST2=1277)) == ["IF_HB_ST2"]


def test_the_blanking_set_the_header_was_missing_is_checked_too():
    # IF_HB_ST1 was absent from tv5725.h until 2026-08-09, so it was absent from
    # this check as well -- a register the map never declared has no entry to
    # look up. Set 1 is loaded by every preset table and written by no code, so
    # nothing else would ever notice it out of range.
    assert names(check(IF_HB_ST1=1300)) == ["IF_HB_ST1"]


def test_a_window_measured_from_its_own_start_is_not_checked_as_a_position():
    # IF_LINE_SP is IF_LINE_ST plus one whole line, so on this bench it is 1341
    # against a 1277 unit line and that is correct. Checking it against the
    # raster reported a fault that two sessions then looked for a writer for.
    assert names(check(IF_LINE_SP=1341)) == []


def test_a_fault_does_not_have_to_be_visible_to_be_there():
    """SP_CS_HS_SP was found at 2816 against PLLAD_MD 2553 in a state confirmed
    clean by eye, so an out-of-range register is not always something you can
    see. It was gone after a reboot, which is what made it a stray write rather
    than a setting."""
    assert names(check(SP_CS_HS_SP=2816)) == ["SP_CS_HS_SP"]


def test_each_axis_is_measured_against_its_own_raster():
    """A vertical register must not be judged against the horizontal line, which
    is the mistake that makes a checker like this useless: IF_VB counts
    HALF-LINES, so its limit is 2 x (VTOTAL + 1) = 624, not 1277."""
    assert names(check(IF_VB_ST=700)) == ["IF_VB_ST"]
    assert check(IF_VB_ST=623) == []


def test_the_sync_processor_is_measured_in_adc_samples():
    """Settled 2026-08-09: the sync processor counts ADC samples, so its limit is
    PLLAD_MD and not the IF line. SP_RT_HS_SP at 2374 is healthy and would be
    flagged by anything using the 1277 unit line."""
    assert check() == []
    assert names(check(SP_RT_HS_SP=2600)) == ["SP_RT_HS_SP"]


def test_several_faults_are_all_reported():
    assert set(names(check(IF_HB_ST=1347, IF_LINE_ST=1341, VDS_HB_ST=1500))) == {
        "IF_HB_ST", "IF_LINE_ST", "VDS_HB_ST"}


def test_a_register_that_could_not_be_read_is_not_a_finding():
    """A dropped read is None, and reporting it as out of range would send the
    next session after a register that is fine."""
    state = dict(BENCH)
    state["IF_HB_ST"] = None
    assert inrange.check(state.get) == []


def test_an_unreadable_raster_total_skips_its_group_rather_than_guessing():
    state = dict(BENCH)
    state["IF_HSYNC_RST"] = None
    assert [f.name for f in inrange.check(state.get)
            if f.name.startswith("IF_H")] == []


def test_bypass_is_not_reported_as_a_wall_of_faults():
    """In RGBHV bypass the VDS is out of the video path and VDS_?SYNC_RST reads
    0, so every VDS position would look out of range. That is the documented
    normal state of a bypassed unit, not twelve faults."""
    findings = check(VDS_HSYNC_RST=0, VDS_VSYNC_RST=0)
    assert [f.name for f in findings if f.name.startswith("VDS_")] == []
