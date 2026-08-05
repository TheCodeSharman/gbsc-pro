"""geometry_math against the register sets that were photographed working.

No hardware. The four SOLVED-* snapshots are states that were on screen, clean
and full width, at the end of the 2026-08-03 session; the artefacting ones are
recorded too. If the arithmetic cannot reproduce what those registers do, it is
wrong, and no amount of bench time will make it right.
"""

import glob
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


# --- panning the capture window ----------------------------------------------


def test_a_pan_moves_both_capture_edges_and_keeps_the_width():
    """Panning chooses which part of the source is grabbed. The width is what
    `produced` is computed from, so a pan that changed it would resize the
    picture as a side effect."""
    sp, st = gm.pan_capture(264, 1062, +8, wrap_at=1277)

    assert (sp, st) == (272, 1070)


def test_a_pan_stops_at_the_wrap_point_rather_than_crossing_it():
    """IF_VB_ST wraps at 624 and nothing in the firmware guards it; crossing
    makes the picture jump, which has been misread as losing the capture."""
    sp, st = gm.pan_capture(56, 569, +100, wrap_at=624)

    assert st <= 623, f"IF_VB_ST {st} is at or past the 624 wrap"
    assert st - sp == 513, "clamping must not change the captured width"


def test_a_pan_stops_at_zero_going_the_other_way():
    sp, st = gm.pan_capture(56, 569, -100, wrap_at=624)

    assert sp >= 0
    assert st - sp == 513


# --- the origin is measured, not assumed --------------------------------------


def test_a_given_origin_is_used_exactly_and_never_recomputed():
    """The offset is NOT a constant: measured against TestPat's 1 px green
    frame it is 78 bypassed, 80 at HSCALE 1023 (x1.001) and 93 at HSCALE 650
    (x1.575). Computing it from one constant shifted a hand-tuned picture 13 px.

    So a caller that knows where the picture is -- because a human aligned it --
    must be able to say so, and have every edge left where it is.
    """
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H, origin=129)

    assert axis["origin"] == 129
    assert axis["display_sp"] == 129


def test_the_bench_known_good_is_reproduced_without_shifting_it():
    """A hand alignment, to the pixel, 2026-08-05: VDS_HB_SP 36, display window
    starting at 129. Given that origin, the solver must return it unchanged
    rather than recentring it somewhere else."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H,
                         origin=129, window_sp_of=36)

    assert axis["window_sp"] == 36
    assert axis["display_sp"] == 129


def test_without_a_known_origin_the_solver_says_it_is_guessing():
    """Falling back to the constant is allowed -- there has to be a starting
    point -- but it must not look like a measurement."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert any("origin" in note for note in axis["clamped"]), axis["clamped"]


# --- solving an axis ----------------------------------------------------------


def test_the_solver_centres_the_picture_on_the_raster():
    """Bench reference, 2026-08-05 evening: capture 798 at HSCALE 650 produces
    1257.16 px on a 1445 px line, so the origin belongs at (1445-1257.16)/2 =
    93.92, rounding to 94."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["origin"] == 94
    assert axis["window_sp"] == 94 - gm.ORIGIN_OFFSET_H_SCALED


def test_the_solver_takes_every_pixel_of_margin_available():
    """The window's far end is free once the origin is pinned, so it goes to the
    raster's last usable value rather than to a computed target.

    On a 1445 px line VDS_HSYNC_RST is 1444, and VDS_HB_ST must stay STRICTLY
    below it -- so the last usable value is 1443, not 1444.
    """
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["window_st"] == 1443


def test_no_returned_register_reaches_the_value_that_wraps():
    """VDS_VB_ST at VDS_VSYNC_RST rolls the frame, and VDS_HB_ST at
    VDS_HSYNC_RST wraps. Test at the boundary itself: off-by-one is the risk."""
    for total, axis, rst in ((1445, gm.AXIS_H, 1444), (1126, gm.AXIS_V, 1125)):
        solved = gm.solve_axis(500, 650, False, total, axis)
        assert solved["window_st"] < rst, f"{axis.name} window reaches the wrap"
        assert solved["display_st"] < rst, f"{axis.name} display reaches the wrap"


def test_the_display_window_hugs_the_picture():
    """The error that invalidated two of tonight's measurements was a display
    window left sized for a different picture, blanking the right-hand end where
    tearing shows."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["display_sp"] == axis["origin"]
    assert axis["display_st"] == axis["origin"] + int(axis["produced"])


def test_the_display_window_never_runs_past_the_last_written_line():
    """Rounding UP exposes a line of unwritten memory as scratch. Measured on
    the bench: capture 513 half-lines at VSCALE 487 produces 1078.67 lines from
    origin 26, so the window must stop at 1104, not 1105 -- 1105 put a band of
    coloured scratch along the bottom of the screen.

    Michael's own horizontal value corroborates it: VDS_DIS_HB_ST 927 against
    origin 129 and produced 798.78 is origin + floor(produced), and it blanked
    cleanly.
    """
    axis = gm.solve_axis(513, 487, False, 1126, gm.AXIS_V)

    assert axis["display_st"] == 1104
    # VDS_DIS_?B_ST is where blanking STARTS -- the first blanked pixel, not the
    # last shown one -- so it may equal origin + produced but never exceed it.
    assert axis["display_st"] <= axis["origin"] + axis["produced"]


def test_the_solver_corrects_the_thirteen_pixel_offset_on_the_bench():
    """The bench had VDS_DIS_HB_SP 129 against an origin of 116 -- 13 px of
    picture blanked left, 13 px of scratch shown right. Same inputs, corrected."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["display_sp"] != 129


def test_a_vertical_solve_treats_IF_VB_as_half_lines():
    """IF_VB counts half-lines and wraps at 624 = 2 x the 312-line frame. Reading
    it as whole lines doubles the picture, and is the likeliest bug here.
    Bench: 513 half-lines at VSCALE 660 is 795.93 output lines, not 1591.9."""
    axis = gm.solve_axis(513, 660, False, 1125, gm.AXIS_V)

    assert 795 < axis["produced"] < 797


def test_the_vertical_window_never_reaches_the_wrap_that_rolls_the_frame():
    axis = gm.solve_axis(513, 660, False, 1125, gm.AXIS_V)

    assert axis["window_st"] < 1125


def test_a_picture_too_wide_for_the_line_warns_rather_than_shrinking():
    """Scale is the user's to set. If even a maximised window leaves under the
    warn floor, say so and hand back the widest window -- do not quietly change
    the picture the user asked for."""
    axis = gm.solve_axis(798, 570, False, 1445, gm.AXIS_H)

    assert axis["produced"] > 1400
    assert axis["margin_given"] < gm.HEADROOM_WARN_PX
    assert axis["clamped"], "a shortfall must be reported, not silent"


def test_a_comfortable_picture_reports_no_shortfall():
    """With the origin known there is nothing to warn about at this scale."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H,
                         origin=129, window_sp_of=36)

    assert axis["margin_given"] >= gm.HEADROOM_WARN_PX
    assert axis["clamped"] == []


def test_the_window_that_shredded_on_the_bench_is_not_safe():
    """VDS_HB_ST 847 against VDS_HB_SP 49 at HSCALE 1023 -- the video in
    docs/photos/2026-08-05-horizontal-geometry/15-source-video-IMG_1253.mov."""
    assert not gm.is_safe(847 - 49, 798.78)


def test_the_warn_floor_clears_both_boundaries_that_were_measured_validly():
    """The floor is not a boundary -- it is deliberately well above both, since
    the requirement is non-monotonic in HSCALE, the corruption has multiple
    stable bands, and it may vary between units."""
    assert gm.HEADROOM_WARN_PX > 33.2, "the HSCALE 1023 edge"
    assert gm.HEADROOM_WARN_PX > 84.6, "the HSCALE 850 edge"


@pytest.mark.parametrize("case", SOLVED, ids=ids(SOLVED))
def test_produced_width_fits_the_line(case):
    produced = gm.produced_px(case["capture"], case["hscale"], case["bypassed"])
    assert 0 < produced <= case["line_px"]


def test_magnification_reports_a_dropped_read_rather_than_guessing():
    """HSCALE 0 is a dropped I2C read, not 1:1. Returning 1.0 there would give a
    plausible width that is wrong, which is worse than refusing."""
    assert gm.magnification(0) is None
    assert gm.magnification(0, bypassed=True) == 1.0


# --- the generative direction ------------------------------------------------


def test_memory_window_never_exceeds_the_line():
    assert gm.ensure_memory_window(9999, 1292, 1445) == 1445


# --- the starting-point rule -------------------------------------------------


def test_default_vertical_fraction_splits_on_field_rate():
    """50 Hz sources carry the same active height in a longer frame, so they have
    far more vertical blanking. One constant would be wrong for one of them."""
    _, pal = gm.default_active_fractions(50.0)
    _, ntsc = gm.default_active_fractions(60.0)
    assert pal < ntsc


def test_default_capture_window_over_captures():
    """Biased to show black edges rather than crop. A cropped edge looks like a
    tuning fault and sends you hunting for a problem that is not there."""
    line = 1277
    start, stop = gm.default_capture_window(line, 50.0)
    assert (stop - start) > line * gm.DEFAULT_H_ACTIVE_FRACTION
    assert 0 <= start < stop <= line


def test_default_capture_window_stays_inside_the_line():
    for line in (400, 1277, 2047):
        for rate in (50.0, 60.1):
            start, stop = gm.default_capture_window(line, rate)
            assert 0 <= start < stop <= line


# --- the vertical axis, against a fully measured state ------------------------


def test_the_vertical_formula_reproduces_a_measured_picture():
    """docs/photos/2026-08-05-horizontal-geometry/README.md, photo 13: the only
    vertical state where the picture's own edges were read off the screen rather
    than inferred from the display window.

        IF_VB 22..568 = 546 half-lines, VSCALE 660, VDS_VB 37..978
        picture top at output line 63, last written line 909

    546 x 1024 / 660 = 847.13, so the first blanked line is 63 + 847 = 910 and
    the last written one is 909. Exact.

    This is what retires the "vertical model is wrong by 30 lines" finding. That
    came from the pixel-perfect snapshot's VDS_DIS_VB of 19..1123, read as a
    1104-line picture -- but 19 is 44 lines ABOVE the picture and 1123 is railed
    against the raster, so 1104 measured the display window and not the picture.
    """
    v = gm.solve_axis(capture=546, scale=660, bypassed=False, raster_total=1126,
                      axis=gm.AXIS_V, origin=63, window_sp_of=37)

    assert round(v["produced"], 2) == 847.13
    assert v["display_st"] - 1 == 909


def test_the_vertical_corner_is_where_the_picture_starts():
    """Not where the display window starts. The two differed by 44 lines on the
    bench, invisibly, because the display window's top edge fell above the
    panel's own top edge and nothing showed the difference."""
    assert gm.CORNER_V == 63


# --- zooming past the scale ceiling -------------------------------------------


def test_zoom_shrinks_the_capture_and_keeps_the_picture_at_the_ceiling():
    """Once the picture fills the memory window the scaler cannot magnify any
    further -- produced would overflow. Capturing LESS source and scaling it by
    the same amount is still a zoom, and it keeps the output size pinned."""
    sp, st, scale = gm.zoom_capture(264, 1062, max_produced=1408, step=8)

    assert st - sp == 790, "capture shrank by the step"
    assert (sp - 264) == (1062 - st), "shrank symmetrically, so it stays centred"
    # Within a pixel or two: the scale is an integer register, so the refill
    # cannot be exact.
    assert abs((st - sp) * 1024 / scale - 1408) < 2.0, "produced still fills it"


def test_zoom_refuses_to_shrink_the_capture_to_nothing():
    with pytest.raises(ValueError):
        gm.zoom_capture(500, 510, max_produced=1408, step=100)


def test_below_the_ceiling_a_scale_step_is_just_a_scale_step():
    """Nothing clever until the picture actually fills the window: the capture
    is the user's, set by panning, and must not move under them."""
    step = gm.scale_step(sp=264, st=1062, scale=900, delta=-8,
                         memory_window=1424, wrap_at=1277)

    assert (step["sp"], step["st"]) == (264, 1062)
    assert step["scale"] == 892
    assert not step["zoomed"]


def test_at_the_ceiling_a_zoom_trims_the_capture_instead():
    """Dropping the scale further would produce more than the memory window
    holds, which shreds the picture. Capturing less source is still a zoom.

    798 units in a 1424 px window fills it at scale 574."""
    step = gm.scale_step(sp=264, st=1062, scale=574, delta=-8,
                         memory_window=1424, wrap_at=1277)

    assert step["zoomed"]
    assert step["st"] - step["sp"] < 798, "captured less of the source"
    # An odd step cannot split evenly, so the centre holds to within a unit.
    assert abs((step["sp"] - 264) - (1062 - step["st"])) <= 1


def test_a_zoom_never_produces_more_than_the_window_holds():
    """The whole point. The refill scale is a ceiling and not a round for
    exactly this: rounding down asks for a fraction more than there is."""
    step = gm.scale_step(sp=264, st=1062, scale=574, delta=-8,
                         memory_window=1424, wrap_at=1277)

    produced = (step["st"] - step["sp"]) * 1024 / step["scale"]
    assert produced <= 1424


def test_zooming_out_at_the_ceiling_widens_the_capture_back():
    """Round trip. A control that cannot be undone is how a hand-aligned
    picture gets lost -- '+' then '-' must return the capture it started with."""
    inward = gm.scale_step(264, 1062, 574, -8, 1424, 1277)
    back = gm.scale_step(inward["sp"], inward["st"], inward["scale"], 8,
                         1424, 1277)

    assert (back["sp"], back["st"]) == (264, 1062)


def test_zoom_stays_inside_the_scale_register():
    """A capture small enough would ask for a scale below the x4 limit."""
    sp, st, scale = gm.zoom_capture(264, 1062, max_produced=1408, step=600)

    assert scale >= gm.HSCALE_MIN
