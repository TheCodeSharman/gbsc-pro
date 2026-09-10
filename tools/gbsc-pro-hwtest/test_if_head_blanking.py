"""The input formatter's head blanking follows the scan mode, against a live unit.

IF_HBIN_SP is two things and which one depends on IF_LD_RAM_BYPS. With the
line-double FIFO in circuit it is that FIFO's line reset, and moving it pans the
whole picture. With the FIFO bypassed it is a blanking edge in the capture
window's own units, so any value it holds crops the left of the picture a second
time -- on top of the crop the capture window already applies.

Measured on the bench at 272, the value every scaling table shipped: 800x600@60
lost its outermost castellation column and 640x480@60 lost its whole left
colour-bar column, while 384x288@70 -- whose picture starts later in the line --
lost nothing. The bench's everyday 15 kHz source is line-doubled and so was
never affected, which is why it went unseen.

docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md

Read-only: it reads four fields and presses nothing.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import locked_steadily, read_fields, wait_for

# Tv5725::InputFormatter::NoHeadBlanking and ::LineDoubleReset.
NO_HEAD_BLANKING = 2
LINE_DOUBLE_RESET = 272


@pytest.fixture
def solved(host):
    """A source the engine has measured and solved a capture window for.

    Waits rather than skips: a skip on an unlocked source reads as a pass.
    """
    assert wait_for(lambda: locked_steadily(host), timeout=90.0, interval=1.0), \
        "no locked source after 90 s; the engine has solved no capture window to check"
    return read_fields(host, ["IF_LD_RAM_BYPS", "IF_HBIN_SP", "IF_HB_SP2", "IF_HB_ST2"])


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
