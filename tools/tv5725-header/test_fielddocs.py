"""What the datasheet extraction must get right, pinned by example.

Every case here is a field the parser silently got wrong, and "silently" is the
point: a missed field looks like a register the datasheet does not document, and
a mis-joined one looks like a field nobody has heard of. Neither announces
itself, and both ended up in tv5725.h.
"""

import os
import re
import pytest

import fielddocs
import merge_slices

# Resolved against this file, not the working directory, so `pytest` at the repo
# root reads the same inputs as `pytest` in here. Relative paths made the whole
# suite error out from anywhere else, which read as 25 broken tests.
HERE = os.path.dirname(os.path.abspath(__file__))
REGDEF = os.path.join(HERE, "regdef.txt")
HEADER = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code",
                      "gbs-control", "tv5725.h")


@pytest.fixture(scope="module")
def fields():
    return fielddocs.parse(REGDEF)


@pytest.fixture(scope="module")
def merged():
    """keep_slices=True is load-bearing, not a detail.

    Without it the `[9:8]` suffixes are stripped during parsing, so every slice
    of a wide field lands on the same dict key and all but one is overwritten
    before merge_slices ever sees them. The field then merges from a single
    piece and comes out one byte wide. Both attempts at re-deriving the field
    set on 2026-08-13 failed exactly here, and so did the first draft of this
    fixture.
    """
    raw = fielddocs.parse(REGDEF, keep_slices=True)
    return merge_slices.merge([(n, v[0], v[1], v[2], v[3], v[4], v[5])
                               for n, v in raw.items()])


# (name, segment, register, low bit, width) -- checked against the PDF by hand.
WIDE = [
    ("VDS_HSCALE", 3, 0x16, 0, 10),
    ("PLLAD_MD", 5, 0x12, 0, 12),
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12),
    ("IF_HB_ST1", 1, 0x14, 0, 11),
    ("MEM_MODE_REG", 4, 0x01, 0, 16),
]


@pytest.mark.parametrize("name,seg,reg,lo,width", WIDE)
def test_a_field_split_across_registers_keeps_its_full_width(merged, name, seg, reg, lo, width):
    """The datasheet names a wide field once per register it occupies, so
    VDS_HSCALE appears as [7:0] on s3_16's page and [9:8] on s3_17's. Treated as
    two fields it is stored 8 bits wide and every value written through it is
    truncated.

    This is the check that has to run before any coverage count is believed. Two
    separate attempts at re-deriving the field set on 2026-08-13 both produced
    complete-looking tables that failed exactly here -- one because the parse was
    keyed by name so later slices overwrote earlier ones, one because (hi, lo)
    was read as (lo, hi).
    """
    got = merged.get(name)
    assert got is not None, f"{name} not extracted at all"
    assert (got["seg"], got["reg"], got["lo"], got["width"]) == (seg, reg, lo, width)


def test_a_table_headed_FUNCTION_in_capitals_is_still_a_table(fields):
    """Seven of the 558 register tables spell the column FUNCTION rather than
    Function. A case-sensitive header match skipped all of them, and with them
    every field they define -- MEM_FST_REG at s4_0f being the one that showed up,
    because a preset writes 0x06 there in all twelve tables."""
    got = fields.get("MEM_FST_REG")
    assert got is not None, "MEM_FST_REG lost: the FUNCTION-in-capitals table was skipped"
    assert (got[0], got[1]) == (4, 0x0F)


@pytest.mark.parametrize("name,seg,reg", [
    ("PB_CAP_BUF_STA_ADDR_A", 4, 0x31),
    ("PB_CAP_BUF_STA_ADDR_B", 4, 0x34),
    ("PB_CAP_BUF_STA_ADDR_D", 4, 0x3E),
    ("IF_HS_RATE_SEG0", 1, 0x03),
])
def test_a_name_wrapped_across_two_lines_is_rejoined(fields, name, seg, reg):
    """The PDF breaks a long field name over two lines and puts the bit number
    only on the second:

              PB_CAP_BUF_STA_ADDR    Capture and Play Back Buffer A START ...
        4-0   _A [20:16]             Start address buffer A [20:16] ...

    Unjoined, the field is recorded under a fragment of its own name. That is
    worse than missing it, because the fragment reaches the header and looks
    like a real register: `PB_CAP_BUF_STA_ADDR_B` shipped as `R_B`, and
    `IF_HS_RATE_SEG0` as `EG0`.
    """
    got = fields.get(name)
    assert got is not None, f"{name} not rejoined from its wrapped halves"
    assert (got[0], got[1]) == (seg, reg)


@pytest.mark.parametrize("name,seg,reg", [
    ("CAP_SAFE_GAURD_A", 4, 0x24),      # the datasheet's own spelling, "GAURD"
    ("WFF_SAFE_GUARD_A", 4, 0x44),
    ("WFF_SAFE_GUARD_B", 4, 0x47),
    ("VDS_NS_SQUARE_RAD", 3, 0x5B),
])
def test_a_wrapped_name_whose_bit_row_holds_only_a_slice_is_rejoined(fields, name, seg, reg):
    """The same wrap as the test above, one step further: the PDF puts the WHOLE
    name on the upper line, leaving the bit row carrying nothing but the slice.

              WFF_SAFE_GUARD_B       Write FIFO Buffer B Safe Guard Address:
        7-0   [7:0]                  Safe guard address buffer B [7:0] ...

    BITROW required the Name cell to start with `[A-Z_]`, so a cell starting `[`
    did not match at all and the row was read as description text. The field
    then vanished silently -- no fragment, no wrong width, just absent, which is
    the one failure mode none of the other guards here can see.

    This is the SAME defect the module docstring already records having fixed
    for a leading underscore ("Requiring an initial [A-Z] dropped those rows
    entirely"), one character class short. It cost four fields.

    VDS_NS_SQUARE_RAD is the worst of them, because two of its three slices
    parsed fine and only the `[0]` piece at s3_5b was lost -- so merge_slices
    could not reconcile it and dropped a 15-bit field entirely rather than
    recording a short one.
    """
    got = fields.get(name)
    assert got is not None, f"{name} lost: its bit row holds only a slice"
    assert (got[0], got[1]) == (seg, reg)


def test_a_row_whose_name_column_is_misaligned_is_read_from_the_row(fields, merged):
    """Column positions are taken from each table's own `Bit Name Function`
    header, and adjacent tables do not agree on them:

        s3_08   ........Bit...............Name..............Function
                ........7-4.......VDS_VB_SP [3:0]

    The header puts Name at column 26 and the row puts it at 18, so the column
    slice yields `P [3:0]` -- a fragment of the real name. Reading the name from
    the row instead is what the MADPT_GM_NOISE_VALUI case forbids, so the rule
    is narrower: when the column slice is a strict SUFFIX of what the row
    carries, the columns are misaligned and the row wins.

    The cost of getting it wrong is not a mis-named field, it is a missing one:
    the `[3:0]` piece holds bit 0, so merge_slices cannot anchor VDS_VB_SP and
    drops all 11 bits of it -- the same way VDS_NS_SQUARE_RAD goes -- and
    VDS_VB_ST comes out 8 bits wide instead of 11.
    """
    assert fields.get("VDS_VB_SP"), "VDS_VB_SP read as a fragment of its own name"
    assert (merged["VDS_VB_SP"]["seg"], merged["VDS_VB_SP"]["reg"],
            merged["VDS_VB_SP"]["lo"], merged["VDS_VB_SP"]["width"]) == (3, 0x08, 4, 11)
    assert merged["VDS_VB_ST"]["width"] == 11


def test_no_field_name_is_a_fragment(fields):
    """The general form of the case above. Every name the PDF's own tables carry
    is at least three characters and starts a word; anything shorter is a stray
    capital picked out of the Name column."""
    tiny = sorted(n for n in fields if len(n) < 3)
    assert not tiny, f"fragment names extracted: {tiny}"


def test_a_field_does_not_carry_the_next_fields_title(fields):
    """The Function cell is vertically CENTRED on its Name row, so a field's
    title sits above its own name and its detail below:

                                    8bit to 16bit convert Y/UV flip control
                                    If input is 8bit data, ... Y and UV order:
            2       IF_UV_REVERT
                                    0: Keep the designed order
                                    1: Flip the Y and UV order
                                    Select CCIR656 data
                                    If input data is 8bit CCIR656 mode, ...
            3       IF_SEL_656      0: input is CCIR 601 mode. ...

    Taking a fixed one line above and running to the next Name row gives every
    field the NEXT one's title. It is invisible in isolation -- the text is real
    datasheet prose about a real register -- and it reached both merged.json and
    tv5725.h's comments, where IF_UV_REVERT explains how to select CCIR656 data.
    """
    assert "Select CCIR656 data" not in fields["IF_UV_REVERT"][5]
    assert fields["IF_SEL_656"][5].startswith("Select CCIR656 data")


@pytest.mark.parametrize("name", ["GPIO_VAL_1", "GPIO_VAL_3", "GPIO_VAL_5",
                                  "GPIO_VAL_7", "WFF_HB_DELAY"])
def test_a_field_whose_whole_cell_is_one_line_keeps_it(fields, name):
    """A run of consecutive one-line cells has no slack in it:

                                    GPIO bit0 output value
            0       GPIO_VAL_0
                                    GPIO bit1 output value
            1       GPIO_VAL_1

    Centring alone lets GPIO_VAL_0 claim a line below to match the one above,
    which is the very line that is GPIO_VAL_1's entire cell -- so the even
    fields read fine and the odd ones come out blank. A field with no text on
    its own Name row must be left at least its title.
    """
    assert fields[name][5], f"{name} lost its one-line description"


# Where the extraction and tv5725.h disagree, and the header is right.
#
# **These are the datasheet's own errors, not the parser's.** RD-5725-1.1
# labels a wide field twice per register -- once in the bit diagram above the
# table, once in the Bit/Name rows -- and for these eleven the two disagree.
# VDS_HB_ST is the clearest:
#
#     REG S3_04   diagram:  VDS_HB_ST [7:0]         <- right
#                 row:      7-0  VDS_HB_ST [9:8]    <- wrong
#     REG S3_05   diagram:  VDS_HB_ST [11:8]        <- right
#                 row:      3-0  VDS_HB_ST [3:0]    <- wrong
#
# merge_slices reads the rows, so it saw pieces [9:8] and [3:0], put field bit 0
# in the S3_05 piece, anchored the field there and made it 10 bits wide. Hence
# "s3 0x05 w 10" against the header's "s3 0x04 w 12".
#
# The header is right and provably so: VDS_HB_ST routinely holds 1342, which
# does not fit in 10 bits, and the geometry engine has been driving it on the
# bench for weeks.
#
# The header's values are the output of a hand audit, c922c80 (2026-08-04), and
# this list matches its conclusion exactly.
#
# **CROSS-CHECK ALL THREE SOURCES; DO NOT TRUST THE DIAGRAM ALONE.** Three
# independent sources vote:
#
#   1. the bit diagram above the table
#   2. the Bit/Name table rows
#   3. the "bit[hi:lo]" written inside the Function description text
#
# Cross-checking all three cut the open disagreements from 12 to 2, and
# SP_H_CST_SP is the case where the DIAGRAM carries the wrong slice and the table
# is right -- neither source can be trusted on its own.
#
# Not implemented in the extractor: eleven fields the header already has right is
# blocking nothing, and parsing a column-aligned graphic is a job of its own.
#
# Listed individually rather than tolerated in bulk, so a NEW disagreement fails
# loudly. The risk is real and one-directional: a field declared narrower than
# it is truncates every value written through it, silently.
HEADER_WINS = {
    "PIP_U_GAIN", "PIP_U_OFST", "PIP_V_GAIN", "PIP_Y_GAIN", "PIP_Y_OFST",
    "VDS_DIS_VB_SP", "VDS_FRAME_RST", "VDS_HB_ST", "VDS_HS_SP",
    "VDS_SYNC_LEV", "VDS_VB_ST",
}


def test_the_extraction_agrees_with_the_header_everywhere_else(merged):
    """823 of the 834 names both describe agree exactly on segment, register,
    offset and width. That agreement is what makes the extraction usable as a
    source for new declarations at all -- and the eleven exceptions are why a
    width from it must never be trusted over the header's."""
    import re
    hdr = open(HEADER,
               encoding="utf-8", errors="replace").read()
    ureg = re.compile(r"typedef\s+UReg<\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)"
                      r"\s*,\s*(\d+)\s*,\s*(\d+)\s*>\s*(\w+)\s*;")
    header_fields = {n: (int(s, 0), int(r, 0), int(o), int(w))
                     for s, r, o, w, n in ureg.findall(hdr)}

    unexpected = []
    for name in sorted(set(header_fields) & set(merged)):
        f = merged[name]
        if (f["seg"], f["reg"], f["lo"], f["width"]) != header_fields[name]:
            if name not in HEADER_WINS:
                unexpected.append((name, header_fields[name],
                                   (f["seg"], f["reg"], f["lo"], f["width"])))
    assert not unexpected, (
        "new disagreement between tv5725.h and the datasheet extraction: "
        f"{unexpected}. Check which is right before adding to HEADER_WINS -- a "
        "width that is too small truncates every write through that field."
    )


def test_known_fragments_from_the_2026_08_13_regression_are_gone(fields):
    """The exact names commit 940774a wrote into tv5725.h before this was fixed.
    Named individually so the failure says which wrap broke rather than just
    that a count moved."""
    were_shipped = ["R_B", "EG0", "EG1", "EG7", "ALUE", "LUE", "FFSET", "FSET",
                    "YPS", "SHOLD", "TILL", "OCK", "ERT", "ATE", "NRD", "T_EN"]
    present = sorted(n for n in were_shipped if n in fields)
    assert not present, f"still extracting name fragments: {present}"


def test_the_bit_diagram_is_a_second_opinion_and_not_an_authority():
    """diagram.py places fields from the bit graphic rather than the table rows.

    It agrees with only ~61% of the fields where tv5725.h and the table already
    agree, so it must never overrule either on its own. Pinned as a number
    because the temptation is real: it gets VDS_HB_ST right, which is the
    headline disagreement.

    SP_H_CST_SP is the same shape from the other side -- the wrong slice in its
    diagram and the right one in its table. Three sources, one vote each.
    """
    import re
    import diagram

    placed = diagram.fields(REGDEF)
    hdr = open(HEADER,
               encoding="utf-8", errors="replace").read()
    ureg = re.compile(r"typedef\s+UReg<\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)"
                      r"\s*,\s*(\d+)\s*,\s*(\d+)\s*>\s*(\w+)\s*;")
    header_fields = {n: (int(s, 0), int(r, 0), int(o), int(w))
                     for s, r, o, w, n in ureg.findall(hdr)}

    checkable = [n for n in placed if n in header_fields]
    assert len(checkable) > 150, "diagram parse collapsed; it used to place ~186"
    agree = [n for n in checkable if placed[n] == header_fields[n]]
    rate = len(agree) / len(checkable)
    assert 0.5 < rate < 0.8, (
        f"diagram agreement with the header moved to {rate:.0%}. If it went UP, "
        f"the parse improved and this bound should follow it. If it went DOWN, "
        f"something broke. Either way it is still not an authority on its own."
    )


def test_the_diagram_settles_VDS_HB_ST_in_the_header_s_favour():
    """The one field the diagram earns its place on. The table makes VDS_HB_ST
    10 bits at s3_05; the header and the diagram both make it 12 at s3_04, and
    the bench settles it -- it holds 1342, which needs 12."""
    import diagram
    assert diagram.fields(REGDEF).get("VDS_HB_ST") == (3, 0x04, 0, 12)


# Multi-byte fields flagged for manual review, each read off its own datasheet
# pages and each found already correct. Recorded with the evidence so they are
# not re-opened.
#
#   VDS_C2_TAG_LOW_SLOPE    s3_66 bits 7-5 = [2:0]  s3_67 bits 6-0 = [9:3]
#   VDS_C2_TAG_HIGH_SLOPE   s3_67 bit  7   = [0]    s3_68 = [8:1]  s3_69 bit 0 = [9]
#   VDS_C1_TAG_LOW_SLOPE    s3_60 bits 7-4 = [3:0]  s3_61 bits 5-0 = [9:4]
#   VDS_C1_TAG_HIGH_SLOPE   s3_61 bits 7-6 = [1:0]  s3_62 = [9:2]
#   VDS_PK_LINE_BUF_SP      s3_41 = [7:0]           s3_42 = [9:8]
#
# **A THREE-BYTE STRADDLE IS NOT BY ITSELF A DEFECT.** VDS_C2_TAG_HIGH_SLOPE
# starts at bit 7 of s3_67 and really does run through s3_68 into s3_69, and the
# datasheet means it. c922c80's VDS_GLB_NOISE is the other shape -- offset 0,
# wrongly recorded at offset 7 -- so "offset 7" is a prompt to check the bit
# diagrams, never a verdict.
DATASHEET_VERIFIED = [
    ("VDS_C2_TAG_LOW_SLOPE", 3, 0x66, 5, 10),
    ("VDS_C2_TAG_HIGH_SLOPE", 3, 0x67, 7, 10),
    ("VDS_C1_TAG_LOW_SLOPE", 3, 0x60, 4, 10),
    ("VDS_C1_TAG_HIGH_SLOPE", 3, 0x61, 6, 10),
    ("VDS_PK_LINE_BUF_SP", 3, 0x41, 0, 10),
]


@pytest.mark.parametrize("name,seg,reg,lo,width", DATASHEET_VERIFIED)
def test_the_hand_verified_multibyte_fields_keep_their_addressing(name, seg, reg, lo, width):
    """These are the ones that could have been silently truncated: multi-byte,
    written by a preset, and declared from an extraction that is known to
    understate widths. Read off the pages by hand, all five already right."""
    import re
    hdr = open(HEADER,
               encoding="utf-8", errors="replace").read()
    m = re.search(r"typedef\s+UReg<\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)"
                  r"\s*,\s*(\d+)\s*,\s*(\d+)\s*>\s*" + name + r"\s*;", hdr)
    assert m, f"{name} is no longer declared in tv5725.h"
    assert (int(m.group(1), 0), int(m.group(2), 0),
            int(m.group(3)), int(m.group(4))) == (seg, reg, lo, width)


# Names in merged.json that tv5725.h deliberately does NOT declare, each with
# the reason. Categorised rather than counted: a NEW absence fails the test
# below instead of joining a tolerated total.
NOT_IN_HEADER = {
    # -- Section headings the extractor reads as one very wide field. They are
    #    not registers and must never be declared. Recognisable by the trailing
    #    underscore and an impossible width.
    "CHIP_ID_": "heading, w24", "CRC_STATUS_": "heading, w80",
    "IF_HPERIOD_": "heading, w9", "IF_STATUS_": "heading, w45",
    "IF_VPERIOD_": "heading, w11", "INT_STATUS_": "heading, w8",
    "SYNC_PROC_STATUS_": "heading, w56", "TEST_BUS_": "heading, w24",
    "VDS_STATUS_": "heading, w24",

    # -- The PDF's own typos. The header carries the corrected spelling at the
    #    same address, so the field IS declared -- under a name a reader can
    #    actually search for.
    "CAP_SAFE_GAURD_A": "GAURD; header has CAP_SAFE_GUARD_A",
    "OSD_YCBCR_RGB_FORMATE": "FORMATE; header has OSD_YCBCR_RGB_FORMAT",

    # -- Wrap fragments that survive in the extraction. Each lands on the same
    #    bits as a real field the header declares in full.
    "AA_DLY": "of VDS_FID_AA_DLY", "BF_EN": "of VDS_BF_EN",
    "FF_LREQ_CUT": "of RFF_LREQ_CUT", "K_EN": "of VDS_FLOCK_EN",
    "P_BYPS": "of VDS_STEP_BYPS", "RUN_FID": "of VDS_FREERUN_FID",
    "T_BYPS": "of VDS_DYN_BYPS", "_GAIN": "of VDS_SVM_GAIN",
    "_LEV [8]": "of VDS_SYNC_LEV", "_OFFSET": "of VDS_SVM_OFFSET",
    "VDS_C2_TAG_LOW_SLOP": "of VDS_C2_TAG_LOW_SLOPE, and w3 vs the header's w10",
    "VDS_USIN_GAIN            This field contains the U sin gain":
        "description bled into the name cell; header has VDS_USIN_GAIN",

    # -- Resolved away by the no-aliases rule, 2026-08-13. The header declares
    #    INT_CONTROL_RST_SOGBAD / _SOGSWITCH / _NOHSYNC on these exact bits.
    "INT_RST_0": "alias", "INT_RST_1": "alias", "INT_RST_4": "alias",

    # -- Status blocks the datasheet documents as one multi-byte field, where
    #    each bit inside means something of its own. The header names the bits
    #    instead, which is finer: STATUS_MEM_FF_WFF_FIFO_FULL says what it
    #    reports, MEM_FF_STATUS_ bit 0 does not.
    "DEINT_STATUS_": "block; header names its bits, STATUS_DEINT_* ",
    "GPIO_STATUS_": "block; header names its bits, STATUS_GPIO_*",
    "MEM_FF_STATUS_": "block; header names its bits, STATUS_MEM_FF_*",
    "MISC_STATUS_": "block; header names its bits, STATUS_MISC_*",
}


def test_the_shipped_header_declares_every_field_the_datasheet_does(merged):
    """The completeness claim, finally asserted rather than asserted-in-prose.

    **THE DATASHEET IS THE AUTHORITY, NOT THE EXTRACTION.** Comparing the header
    against the extraction is unfalsifiable when both have the same hole:
    WFF_SAFE_GUARD_B and VDS_NS_SQUARE_RAD are documented in RD-5725-1.1, absent
    from tv5725.h, and invisible to every test that compared the two.

    This asserts the other thing: what the datasheet declares, the header
    declares.
    """
    hdr = open(HEADER,
               encoding="utf-8", errors="replace").read()
    declared = set(re.findall(r"UReg<[^>]+>\s*(\w+)\s*;", hdr))

    absent = sorted(set(merged) - declared - set(NOT_IN_HEADER))

    assert not absent, (
        f"the datasheet declares these and tv5725.h does not: {absent}. Add "
        "them, or add them to NOT_IN_HEADER with the reason -- a heading, a PDF "
        "typo the header corrects, or a wrap fragment.")
