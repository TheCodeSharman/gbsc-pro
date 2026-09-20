"""The input formatter's hbin window follows the scan mode, against a live unit.

Both halves of it. IF_HBIN_SP is two things and which one depends on
IF_LD_RAM_BYPS. With the line-double FIFO in circuit it is that FIFO's line reset, and moving it pans the
whole picture. With the FIFO bypassed it is a blanking edge in the capture
window's own units, so any value it holds crops the left of the picture a second
time -- on top of the crop the capture window already applies.

Measured on the bench at 272, the value every scaling table shipped: 800x600@60
lost its outermost castellation column and 640x480@60 lost its whole left
colour-bar column, while 384x288@70 -- whose picture starts later in the line --
lost nothing. The bench's everyday 15 kHz source is line-doubled and so was
never affected, which is why it went unseen.

IF_HBIN_ST is the same window's start, and it blanks the TAIL of the captured
line -- the part that runs past where active video stops and carries the
contamination before the next line's sync pulse. It may only be raised on a
doubled line: against IF_HBIN_SP 2 the part blanks the whole line from 18 up.

docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
docs/investigations/the-hbin-start-blanks-the-captured-tail.md

Read-only: it reads five fields and presses nothing.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import locked_steadily, read_fields, wait_for

# Tv5725::InputFormatter::NoHeadBlanking, ::LineDoubleReset and
# ::DoubledTailBlanking.
NO_HEAD_BLANKING = 2
LINE_DOUBLE_RESET = 272
DOUBLED_TAIL_BLANKING = 96

# Where a progressive line goes black: 16 leaves the picture whole and 18 blanks
# it entirely, measured at 320x256@70 with IF_HBIN_SP at NO_HEAD_BLANKING.
PROGRESSIVE_BLANKS_EVERYTHING_FROM = 18


@pytest.fixture
def solved(host):
    """A source the engine has measured and solved a capture window for.

    Waits rather than skips: a skip on an unlocked source reads as a pass.
    """
    assert wait_for(lambda: locked_steadily(host), timeout=90.0, interval=1.0), \
        "no locked source after 90 s; the engine has solved no capture window to check"
    return read_fields(host, ["IF_LD_RAM_BYPS", "IF_HBIN_SP", "IF_HBIN_ST",
                              "IF_HB_SP2", "IF_HB_ST2"])


def test_head_blanking_follows_the_scan_mode(solved):
    expected = (NO_HEAD_BLANKING if solved["IF_LD_RAM_BYPS"] == 1
                else LINE_DOUBLE_RESET)
    assert solved["IF_HBIN_SP"] == expected, (
        f"IF_LD_RAM_BYPS {solved['IF_LD_RAM_BYPS']} wants IF_HBIN_SP {expected}, "
        f"reads {solved['IF_HBIN_SP']}")


def test_head_blanking_does_not_reach_into_the_capture(solved):
    """The capture window is the only thing that may crop the left.

    Only where the doubler is bypassed: with the FIFO in circuit IF_HBIN_SP is a
    line reset rather than a blanking edge, and sits past the capture start by
    design.
    """
    if solved["IF_LD_RAM_BYPS"] != 1:
        pytest.skip("line-doubled: IF_HBIN_SP is the FIFO's line reset, not a blanking edge")

    assert solved["IF_HBIN_SP"] <= solved["IF_HB_SP2"], (
        f"IF_HBIN_SP {solved['IF_HBIN_SP']} blanks past the capture start "
        f"{solved['IF_HB_SP2']}, cropping {solved['IF_HB_SP2'] - solved['IF_HBIN_SP']} "
        f"units of picture the capture window asked for")


def test_the_tail_blanking_follows_the_scan_mode(solved):
    expected = 0 if solved["IF_LD_RAM_BYPS"] == 1 else DOUBLED_TAIL_BLANKING
    assert solved["IF_HBIN_ST"] == expected, (
        f"IF_LD_RAM_BYPS {solved['IF_LD_RAM_BYPS']} wants IF_HBIN_ST {expected}, "
        f"reads {solved['IF_HBIN_ST']}")


def test_a_progressive_line_keeps_the_tail_blanking_below_the_black_screen(solved):
    """The bound, rather than the value, so it survives a change to the constant.

    A doubled line takes the guard against IF_HBIN_SP 272 and loses nothing. A
    progressive one has IF_HBIN_SP at 2, and there the same field blanks the
    whole line instead of its tail.
    """
    if solved["IF_LD_RAM_BYPS"] != 1:
        pytest.skip("line-doubled: the tail guard is what this scan mode is for")

    assert solved["IF_HBIN_ST"] < PROGRESSIVE_BLANKS_EVERYTHING_FROM, (
        f"IF_HBIN_ST {solved['IF_HBIN_ST']} against IF_HBIN_SP "
        f"{solved['IF_HBIN_SP']}: a progressive line goes black from "
        f"{PROGRESSIVE_BLANKS_EVERYTHING_FROM} up")
