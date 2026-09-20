"""The memory window's width, against a live unit.

An EVEN `VDS_HB_ST - VDS_HB_SP` shears the picture and an odd one is clean, so
`Axis::solve()` gives a unit back wherever the width comes out even. The width is
`floor(originOffset + produced)` and carries no dependence on `VDS_HB_SP`, which
is why stepping the zoom used to shear on about half the steps.

`investigations/the-shear-follows-the-produced-widths-parity.md`

Read-only unless `--source` is given, which adds the zoom sweep. The host suite
proves the arithmetic; what this proves is that the bias reaches the registers
and survives the pads.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get, locked_steadily, read_fields, wait_for

FIELDS = ["VDS_HB_SP", "VDS_HB_ST", "VDS_DIS_HB_ST", "VDS_HSCALE"]

# Below this the near edge is pinned and the picture is broken whatever the
# width, so a reading there says nothing about the bias.
WINDOW_STOP_MIN = 8


def width(at):
    return at["VDS_HB_ST"] - at["VDS_HB_SP"]


def clamped(at):
    return at["VDS_HB_SP"] <= WINDOW_STOP_MIN + 1


@pytest.fixture
def solved(host):
    """A source the engine has measured and solved a window for.

    Waits rather than skips: a skip on an unlocked source reads as a pass.
    """
    assert wait_for(lambda: locked_steadily(host), timeout=90.0, interval=1.0), \
        "no locked source after 90 s; the engine has solved no memory window to check"
    return read_fields(host, FIELDS)


def test_the_solved_memory_window_is_an_odd_number_of_units_wide(solved):
    if clamped(solved):
        pytest.skip(f"VDS_HB_SP {solved['VDS_HB_SP']} is at its floor: "
                    "the clamp governs the picture, not the width")
    assert width(solved) % 2 == 1, (
        f"VDS_HB_ST {solved['VDS_HB_ST']} - VDS_HB_SP {solved['VDS_HB_SP']} "
        f"= {width(solved)}, which is even and shears")


def test_the_two_windows_still_share_their_far_edge(solved):
    """The bias moves one quantity that lands in two registers.

    Moving VDS_HB_ST alone would put the blanking and the fetch on different
    edges, which is a fault of its own rather than a narrower window.
    """
    assert solved["VDS_HB_ST"] == solved["VDS_DIS_HB_ST"]


@pytest.mark.usefixtures("source")
def test_the_width_stays_odd_across_the_zoom(host, framing_autosave):
    """Every solve the pads can reach, not just the one it happens to sit on.

    The parity is a property of the produced width, so it is the zoom that moves
    it -- a single framing cannot show the bias working.
    """
    even, seen = [], []
    try:
        for key in ("I", "O"):
            for _ in range(24):
                get(host, f"/sc?{key}=8")
                assert wait_for(lambda: locked_steadily(host), timeout=30.0,
                                interval=0.5), "the source stopped solving mid-sweep"
                at = read_fields(host, FIELDS)
                seen.append(at)
                if not clamped(at) and width(at) % 2 == 0:
                    even.append(at)
    finally:
        get(host, "/sc?B")

    assert len(seen) > 40, f"only {len(seen)} solves stepped; the sweep did not run"
    assert not even, (
        f"{len(even)} of {len(seen)} solves came out even, first at "
        f"VDS_HSCALE {even[0]['VDS_HSCALE']}: width {width(even[0])}")
