"""Unit tests for the datasheet description extractor. No PDF needed.

The samples are the RD's real layout, copied out of `pdftotext -layout` output:
a page header, a bit grid, then a Bit/Name/Function table whose Function column
trails its Name by a line or two.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_registers as extract

# S1_14 and S1_15, verbatim. IF_HB_ST1 was absent from tv5725.h until
# 2026-08-09 and is the register that caused the zigzag.
ONE_FIELD_PER_REGISTER = """
INPUT_FORMATTER 14                                                       REG S1_14, R/W

             7            6           5              4                3               2              1           0
 Bit                                                 IF_HB_ST1 [7:0]

       Bit       Name                     Function
                                          Horizontal blanking start position (set 1)
       7-0       IF_HB_ST1 [7:0]
                                          Horizontal blanking (set 1) start position [7:0].

                                                   RD-5725-1.1                                            01-8
TRUEVIEW5725                                                            Registers Definition

INPUT_FORMATTER 16                                                       REG S1_16, R/W

             7            6            5              4                3               2               1           0
 Bit                                                  IF_HB_SP1 [7:0]

       Bit       Name                      Function
                                           Horizontal blanking stop position (set 1)
       7-0       IF_HB_SP1 [7:0]
                                           Horizontal blanking (set 1) stop position [7:0].
"""


# S5_02, verbatim, and the hard case. THE FUNCTION CELL IS VERTICALLY CENTRED ON
# THE NAME ROW, so a field's text sits both above and below its own name:
# "SOG control signal" belongs to ADC_SOGCTRL below it, while "ADC input
# selection" two lines later belongs to ADC_INPUT_SEL three lines below THAT.
#
# An invented fixture that put every field's text after its name made the tests
# here pass while the real map stayed wrong, which is worse than no test.
SEVERAL_FIELDS_PER_REGISTER = """
ADC CONTROL 00                                                           REG S5_02, R/W

             7          6         5            4            3              2            1            0
 Bit       ADC_INPUT_SEL                              ADC_SOGCTRL                                ADC_SOGEN

       Bit       Name                 Function
                                      ADC SOG enable
       0         ADC_SOGEN            When = 0, ADC disable SOG mode
                                      When = 1, ADC enable SOG mode
                                      SOG control signal
       5-1       ADC_SOGCTRL
                                      ADC input selection
                                      When = 00, R0/G0/B0/SOG0 as input
       7-6       ADC_INPUT_SEL        When = 01, R1/G1/B1/SOG1 as input
                                      When = 10, R2/G2/B2 as input
                                      When = 11, reserved
"""


def describe(text, *names):
    return extract.describe(text, set(names))


def test_it_finds_a_description_at_all():
    got = describe(ONE_FIELD_PER_REGISTER, "IF_HB_ST1")
    assert "Horizontal blanking" in got["IF_HB_ST1"]


def test_it_does_not_swallow_the_table_headings():
    """'Bit  Name  Function Horizontal blanking start position (set 1)' is what
    the old extractor produced for this exact register."""
    got = describe(ONE_FIELD_PER_REGISTER, "IF_HB_ST1")
    assert not got["IF_HB_ST1"].startswith("Bit")
    for heading in ("Bit", "Name", "Function"):
        assert f" {heading} " not in f" {got['IF_HB_ST1']} "


def test_a_description_does_not_run_forward_into_the_next_field():
    """Half of the aggregation bug, and the half a line-based rule can fix:
    ADC_SOGEN's chunk used to run down into ADC_SOGCTRL's row."""
    got = describe(SEVERAL_FIELDS_PER_REGISTER,
                   "ADC_SOGEN", "ADC_SOGCTRL", "ADC_INPUT_SEL")
    assert "SOG enable" in got["ADC_SOGEN"]
    assert "ADC_SOGCTRL" not in got["ADC_SOGEN"]
    assert "input selection" not in got["ADC_SOGEN"]


def test_a_field_gets_its_own_title_and_its_own_enum():
    """The headline case. ADC_INPUT_SEL lost its 00 line to the field above, so
    the map's channel numbering looked as though it started at 01."""
    got = describe(SEVERAL_FIELDS_PER_REGISTER, "ADC_SOGCTRL", "ADC_INPUT_SEL",
                   "ADC_SOGEN")
    assert "00" in got["ADC_INPUT_SEL"]
    assert got["ADC_SOGCTRL"].startswith("SOG control signal")


@pytest.mark.xfail(strict=True, reason=(
    "A field whose Function cell is a SINGLE line still absorbs the next "
    "field's title. ADC_SOGCTRL is just 'SOG control signal' with no enum "
    "under it, so there is no blank row between its title and "
    "ADC_INPUT_SEL's, and taking one line above / stopping one short cannot "
    "tell them apart. Everything with a body -- ADC_SOGEN, ADC_INPUT_SEL, "
    "every IF_HB_* -- comes out clean. Separating these needs the PDF's cell "
    "geometry (pdftotext -bbox / pdfplumber) rather than flattened text."))
def test_a_single_line_cell_does_not_absorb_the_next_title():
    got = describe(SEVERAL_FIELDS_PER_REGISTER, "ADC_SOGCTRL", "ADC_INPUT_SEL",
                   "ADC_SOGEN")
    assert "input selection" not in got["ADC_SOGCTRL"]


def test_a_description_does_not_run_into_the_next_register():
    got = describe(ONE_FIELD_PER_REGISTER, "IF_HB_ST1", "IF_HB_SP1")
    assert "stop position" not in got["IF_HB_ST1"]
    assert "start position" not in got["IF_HB_SP1"]


def test_page_furniture_never_appears():
    got = describe(ONE_FIELD_PER_REGISTER, "IF_HB_ST1", "IF_HB_SP1")
    for value in got.values():
        assert "RD-5725" not in value
        assert "TRUEVIEW5725" not in value
        assert "Registers Definition" not in value


def test_a_name_that_is_not_in_the_text_gets_no_description():
    got = describe(ONE_FIELD_PER_REGISTER, "IF_HB_ST1", "MADPT_Y_MI_OFFSET")
    assert got.get("MADPT_Y_MI_OFFSET", "") == ""
