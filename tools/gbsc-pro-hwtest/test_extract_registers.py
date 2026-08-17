"""What the panel's register map must get right.

The map is a join, not a parse: addressing from Tv5725.h, wording from the
datasheet extraction in tools/tv5725-header. Everything about *reading the PDF*
is pinned over there, in test_fielddocs.py, because there is one extraction and
it should have one test suite.

What is pinned here is the join itself, and the two rules that make it correct:
match on bits rather than names, and hold a partition of the chip's bit space
rather than a pile of overlapping names.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_registers as extract


@pytest.fixture(scope="module")
def fields():
    return extract.build()


def test_the_join_is_on_bits_not_on_names(fields):
    """The datasheet misspells names and the header carries the correction, so a
    name-keyed join silently drops real registers. CAP_SAFE_GUARD_A is filed
    under "GAURD" in RD-5725-1.1; OSD_YCBCR_RGB_FORMAT under "FORMATE". Worse,
    the PDF wraps long names and the extraction keeps the fragment, so
    VDS_SVM_GAIN is documented as `_GAIN` and VDS_FLOCK_EN as `K_EN`.

    Eighteen fields reach the panel only through their bits.
    """
    for name in ("CAP_SAFE_GUARD_A", "OSD_YCBCR_RGB_FORMAT", "HPERIOD_IF",
                 "VPERIOD_IF", "VDS_SVM_GAIN", "VDS_FLOCK_EN"):
        assert name in fields, f"{name} lost: its datasheet name differs"
        assert fields[name]["desc"], f"{name} matched but carries no description"


def test_every_bit_has_exactly_one_owner(fields):
    """The map is a partition of the chip's bit space, at the finest naming
    Tv5725.h offers. Two names over one bit is two owners: a write through the
    coarse one silently rewrites everything the fine ones describe, and a panel
    showing both invites exactly that."""
    spans = sorted((e["seg"], extract._span(e["reg"], e["off"], e["width"]), n)
                   for n, e in fields.items())
    overlaps = [(a[2], b[2]) for a, b in zip(spans, spans[1:])
                if a[0] == b[0] and b[1][0] <= a[1][1]]
    assert not overlaps, f"bits with two owners: {overlaps[:5]}"


def test_a_name_covering_finer_fields_is_dropped(fields):
    """The whole-byte convenience names. PLL648_CONTROL_01 sets five documented
    fields under a name RD-5725-1.1 does not have, and PAD_CONTROL_00_0x48 sits
    over eight. Keeping the container as well as its contents is what puts the
    same bit in the panel twice."""
    for name in ("PLL648_CONTROL_01", "STATUS_16", "PAD_CONTROL_00_0x48",
                 "RESET_CONTROL_0x47", "INTERRUPT_CONTROL_00",
                 "MADPT_Y_DELAY_UV_DELAY"):
        assert name not in fields, f"{name} covers finer fields and should go"


def test_a_field_with_nothing_finer_inside_it_stays(fields):
    """Dropping containers is about overlap, not about documentation. A byte
    with no finer naming is the only owner its bits have, so it stays even
    where the datasheet says nothing about it — the panel and Tv5725.h are
    meant to agree on what exists."""
    for name in ("ADC_UNUSED_60", "GBS_PRESET_ID"):
        assert name in fields, f"{name} is the only name those bits have"


def test_a_field_inside_a_documented_block_is_documented(fields):
    """The datasheet documents segment 0's status as a few very wide blocks --
    SYNC_PROC_STATUS_ is 56 bits spanning s0_16..s0_1C -- while Tv5725.h names
    the individual counters inside it. Those bits ARE attested; the datasheet
    just describes them a register at a time rather than a field at a time, and
    the header's finer naming is the more useful of the two.

    Requiring an exact bit match dropped STATUS_SYNC_PROC_VTOTAL, the field
    every lock check on this board reads.
    """
    for name in ("STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HLOW_LEN",
                 "STATUS_IF_INP_INT"):
        assert name in fields, f"{name} lost: it sits inside a wider block"
        assert fields[name]["desc"]


def test_addressing_comes_from_the_header_where_the_two_disagree(fields):
    """RD-5725-1.1 gives VDS_HB_ST's slice in both its bit diagram and its
    Bit/Name rows, and they disagree; the rows make it 10 bits at s3_05 instead
    of 12 at s3_04. The header is right and provably so — the field routinely
    holds 1342, which does not fit in 10 bits."""
    got = fields["VDS_HB_ST"]
    assert (got["seg"], got["reg"], got["off"], got["width"]) == (3, 0x04, 0, 12)


# The two the centring rule cannot place, both in s0_90/s0_93. Their cells are
# multi-row VALUE TABLES rather than prose -- "0 0 1  1 0 3  1 1 4" -- so the
# rows carry no left-column marker and counting lines cannot find the boundary.
# OSD_DISP_EN swallows the whole of OSD_VERTICAL_ZOOM's cell, which is left
# empty. Listed rather than tolerated: a THIRD one fails this test.
VALUE_TABLE_BLEED = [
    ("OSD_DISP_EN", "OSD_VERTICAL_ZOOM"),
    ("OSD_MENU_MOD_SEL", "OSD_MENU_ICON_SEL"),
]


def test_no_description_carries_a_sibling_fields_name(fields):
    """The bleed this whole chain exists to remove, checked at the far end: a
    field's text must not name a different field of the same register. That is
    what "IF_UV_REVERT ... Select CCIR656 data" looked like, and it read as
    ordinary datasheet prose all the way into Tv5725.h."""
    by_reg = {}
    for name, e in fields.items():
        by_reg.setdefault((e["seg"], e["reg"]), []).append(name)
    found = []
    for names in by_reg.values():
        for name in names:
            if fields[name].get("block"):
                continue          # a pointer names its own block, deliberately
            for other in names:
                if other != name and len(other) > 6 and other in fields[name]["desc"]:
                    found.append((name, other))
    assert sorted(found) == sorted(VALUE_TABLE_BLEED), (
        f"sibling bleed changed: {sorted(found)}")
