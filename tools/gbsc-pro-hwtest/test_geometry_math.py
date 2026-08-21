"""geometry_math against the register sets that were photographed working.

No hardware. The four SOLVED-* snapshots are states that were on screen, clean
and full width, at the end of the 2026-08-03 session; the artefacting ones are
recorded too. If the arithmetic cannot reproduce what those registers do, it is
wrong, and no amount of bench time will make it right.
"""

import glob
import inspect
import json
import os

import pytest

import geometry_math as gm

HERE = os.path.dirname(os.path.abspath(__file__))
SNAPSHOTS = os.path.join(HERE, "snapshots")


def field(config, segment, register, offset, width):
    """A field out of a snapshot's flat {"seg:reg": value} map."""
    span = (offset + width + 7) // 8
    raw = 0
    for index in range(span):
        byte = config.get(f"{segment}:{register + index:02x}")
        if byte is None:
            return None
        raw |= byte << (8 * index)
    return (raw >> offset) & ((1 << width) - 1)


def geometry_of(path):
    """The horizontal geometry a snapshot describes, or None if it predates the
    segment 1 capture (a few early dumps only cover the preset ranges)."""
    with open(path) as handle:
        loaded = json.load(handle)
    config = loaded.get("config", loaded)

    line_units = field(config, 1, 0x0E, 0, 11)
    sp2 = field(config, 1, 0x1A, 0, 11)
    st2 = field(config, 1, 0x18, 0, 11)
    if None in (line_units, sp2, st2):
        return None

    capture = st2 - sp2
    if capture < 0:  # the window wraps the end of the line
        capture += line_units + 1

    return {
        "name": os.path.basename(path),
        "capture": capture,
        "hscale": field(config, 3, 0x16, 0, 10),
        "bypassed": bool(field(config, 3, 0x00, 4, 1)),
        "line_px": field(config, 3, 0x01, 0, 12) + 1,
        "memory": field(config, 3, 0x04, 0, 12) - field(config, 3, 0x05, 4, 12),
        "display": field(config, 3, 0x10, 0, 12) - field(config, 3, 0x11, 4, 12),
    }


def load(pattern):
    found = []
    for path in sorted(glob.glob(os.path.join(SNAPSHOTS, pattern))):
        geometry = geometry_of(path)
        if geometry and geometry["capture"] > 0 and geometry["hscale"]:
            found.append(geometry)
    return found


SOLVED = load("SOLVED-*.json")
TUNED = load("*2026-08-03*.json")


def ids(cases):
    return [case["name"] for case in cases]


# --- the analyse direction ---------------------------------------------------


def test_snapshots_were_found():
    """A glob that quietly matches nothing would make every test below vacuous."""
    assert SOLVED, "no SOLVED-* snapshots with segment 1 captured"


# --- solving an axis ----------------------------------------------------------


def test_magnification_reports_a_dropped_read_rather_than_guessing():
    """HSCALE 0 is a dropped I2C read, not 1:1. Returning 1.0 there would give a
    plausible width that is wrong, which is worse than refusing."""
    assert gm.magnification(0) is None
    assert gm.magnification(0, bypassed=True) == 1.0


# --- parking the near edge somewhere it can actually be read ------------------


def test_the_near_edge_parks_below_the_write_start_and_still_on_screen():
    """To find where the scaler starts writing you park the display edge before
    it, so a band of scratch shows, and creep up until the band goes. The park
    has to be visible or there is no band to watch."""
    park = gm.probe_park(400, visible_edge=gm.PANEL_VISIBLE_LEFT, band=60)

    assert park == 340
    assert park > gm.PANEL_VISIBLE_LEFT


def test_a_write_start_too_near_the_bezel_refuses_rather_than_parking_off_screen():
    """CORNER_H is 129 and PANEL_VISIBLE_LEFT is 127, so the usual placement
    leaves two pixels to park in and a band there would vanish at the bezel
    rather than at the write start. Whether that is what produced
    ORIGIN_OFFSET_H's two values is a hypothesis measure_origin.py exists to
    test -- what is pinned here is only the refusal. Move the picture right."""
    with pytest.raises(ValueError, match="off the left"):
        gm.probe_park(150, visible_edge=gm.PANEL_VISIBLE_LEFT, band=60)


# --- produced, against every measurement of it ---------------------------------

# Measured 2026-08-05 with measure_produced.py: the display edge crept down until
# the band of unwritten memory past the picture just vanished, so each `produced`
# is floor() of the real value. Four magnifications horizontally (x1.001..x3.2),
# four vertically (x1.001..x3.4).

# The eleven readings are the acceptance criteria. Each is VDS_DIS_?B_ST at the
# value where the band of unwritten memory vanished, taken with VDS_HB_SP 35 /
# VDS_VB_SP 37 and recorded as that value minus a corner assumed to be 129 / 63.
# The corner is not a constant, so they are re-expressed against where the scaler
# actually starts:
#
#     recorded = write_start + produced - assumed_corner
#
# Nothing here is refitted -- the same numbers off the same screen, read from an
# origin that moves.


def test_produced_is_a_pure_multiply():
    """The headline. capture_offset and output_loss are gone from both axes --
    they were the write start's magnification term, seen from the far end."""
    assert gm.produced_px(400, 512) == pytest.approx(800.0)
    assert gm.produced_px(200, 300) == pytest.approx(682.67, abs=0.01)


def test_produced_takes_no_axis():
    """The multiply is the same on both axes, so a signature offering to vary it
    by axis contradicts what it implements. The parameter was accepted and never
    read; its absence is asserted the way test_axis.cpp guards scale_step's."""
    assert "axis" not in inspect.signature(gm.produced_px).parameters


# --- what a capture unit is, which the scan mode decides ----------------------


def test_the_scan_mode_reads_back_from_all_four_registers():
    """One argument decides all four, so a disagreement is a half-applied change
    rather than a mode -- and saying so beats picking one and being wrong."""
    assert gm.line_doubled(prgrsv=0, ld_ram_byps=0, ld_sel_prov=0, hs_dec_factor=1) is True
    assert gm.line_doubled(prgrsv=1, ld_ram_byps=1, ld_sel_prov=1, hs_dec_factor=0) is False
    assert gm.line_doubled(prgrsv=1, ld_ram_byps=0, ld_sel_prov=1, hs_dec_factor=0) is None


def test_the_vertical_wrap_follows_the_scan_mode():
    """IF_VB counts at the IF's line rate, which the doubler doubles. Measured:
    the 312-line bench source wraps at 624 doubled, and the 500-line 640x480@75
    source wraps at 500 with the doubler bypassed."""
    assert gm.if_vertical_wrap(311, line_doubled=True) == 624
    assert gm.if_vertical_wrap(499, line_doubled=False) == 500


def test_the_if_line_follows_the_scan_mode():
    """IF_HSYNC_RST is PLLAD_MD/2 while the doubler is in the path and PLLAD_MD
    when it is not -- measured 2250/1125 and 1124/1124."""
    assert gm.if_line_units(2250, line_doubled=True) == 1125
    assert gm.if_line_units(1124, line_doubled=False) == 1124


def test_an_unknown_scan_mode_refuses_rather_than_assuming():
    """A wrong wrap silently rescales every bound checked against it, so a bound
    nobody can compute is better than one computed from a guess."""
    assert gm.if_vertical_wrap(311, line_doubled=None) is None
    assert gm.if_line_units(2250, line_doubled=None) is None
