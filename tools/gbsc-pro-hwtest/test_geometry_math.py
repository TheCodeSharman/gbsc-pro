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


def test_the_solver_no_longer_has_to_guess_the_origin():
    """The placement is computed, not warned about: the offset is a line in
    magnification, so there is no constant to guess from."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert not any("GUESSED" in note for note in axis["clamped"]), axis["clamped"]


# --- solving an axis ----------------------------------------------------------


def test_the_solver_centres_the_picture_on_the_raster():
    """Bench reference, 2026-08-05 evening: capture 798 at HSCALE 650 produces
    1257.75 px on a 1445 px line, so the origin belongs at (1445-1257.75)/2 =
    93.6, rounding to 94."""
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["origin"] == 94
    # x1.575 puts the write start 94.4 px after VDS_HB_SP, so a picture centred
    # at 94 needs the memory window at 0. The two nearly cancelling is why 94
    # looked like a constant for so long.
    assert axis["window_sp"] == 0


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
    assert axis["display_st"] == (axis["origin"] + int(axis["produced"])
                                  - gm.AXIS_H.margin)


def test_the_display_window_never_runs_past_the_last_written_line():
    """Rounding UP exposes a line of unwritten memory as scratch, which is what
    was on the bench tonight -- a frozen band, with every register reading what
    was asked for.

    The old version of this test pinned display_st to 1090, a number the refuted
    model produced; what was actually observed is the RULE, that a value past the
    last written line shows scratch. So assert the rule. It holds whatever the
    span turns out to be, which the hardcoded number did not.
    """
    axis = gm.solve_axis(513, 487, False, 1126, gm.AXIS_V)
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
    Bench: 513 half-lines at VSCALE 660 is 795.9 output lines, not 1591."""
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


# --- fitting the loss from measurements ---------------------------------------


def test_the_fit_recovers_a_loss_it_was_given():
    """The deficit is linear in magnification -- capture x m - produced = c x m + k
    -- so a straight-line fit recovers both terms. Synthetic points with a known
    c and k must come back as that c and k, or the fit is not measuring what it
    claims."""
    points = [(capture, scale, (capture - 4.0) * 1024 / scale - 20.0)
              for capture, scale in ((400, 1023), (400, 512), (798, 800))]

    c, k = gm.fit_loss(points)

    assert abs(c - 4.0) < 0.01
    assert abs(k - 20.0) < 0.01


def test_the_fit_needs_two_different_magnifications():
    """Every point at one scale lies on a single vertical line, so c and k cannot
    be separated -- which is exactly how the bench measurements at HSCALE 1023
    looked conclusive and were not."""
    with pytest.raises(ValueError):
        gm.fit_loss([(400, 1023, 386.0), (798, 1023, 785.0)])


def test_the_bench_vertical_pair_fits_the_measured_numbers():
    """2026-08-05, capture 511 half-lines at two scales, produced read off the
    screen by creeping VDS_DIS_VB_ST until the unwritten band vanished."""
    c, k = gm.fit_loss([(511, 550, 923.0), (511, 1023, 486.0)])

    assert 2 < c < 5, f"c = {c:.2f} half-lines"
    assert 20 < k < 25, f"k = {k:.2f} lines"


# --- where the scaler starts writing ------------------------------------------

# measure_origin.py, 2026-08-05, near edge crept until the frozen band went.
# (magnification, offset from VDS_?B_SP).
ORIGIN_H = [(1.0009775171065494, 80), (2.0, 105), (3.2, 135)]
ORIGIN_V = [(1.0009775171065494, 1), (2.0, 2), (3.4133333333333336, 3)]


@pytest.mark.parametrize("magnification,expected", ORIGIN_H)
def test_the_horizontal_write_start_matches_every_reading(magnification, expected):
    """It is not a constant. It walks right at ~25 px per unit magnification,
    which is ~25 INPUT samples of run-up the scaler consumes before it writes
    anything -- IF_HS_TAP11_BYPS names an 11-tap filter."""
    assert abs(gm.AXIS_H.origin_offset(magnification) - expected) < 1.0


@pytest.mark.parametrize("magnification,expected", ORIGIN_V)
def test_the_vertical_write_start_matches_every_reading(magnification, expected):
    """Nearly flat, because the vertical reads whole lines from a line buffer and
    has no interpolator to feed. ~30x smaller than the horizontal term."""
    assert abs(gm.AXIS_V.origin_offset(magnification) - expected) < 1.0


@pytest.mark.parametrize("magnification,recorded", [(1.001, 80), (1.575, 93)])
def test_the_readings_that_were_recorded_as_unexplained_also_fit(magnification,
                                                                 recorded):
    """solve_axis's docstring recorded 80 at x1.001 and 93 at x1.575 and said
    nothing predicted it; ORIGIN_OFFSET_H carried 78 and 94 as irreconcilable.
    One formula, four magnifications -- they were never in conflict, they were
    measured at different scales."""
    assert abs(gm.AXIS_H.origin_offset(magnification) - recorded) <= 1.5


def test_the_bezel_is_not_the_write_start():
    """CORNER_H 129 and CORNER_V 63 were measured at one magnification each and
    treated as constants. 63 is also PANEL_VISIBLE_TOP, and the vertical write
    start is 2 lines after VDS_VB_SP, not 26 -- so 37 + 26 = 63 was the panel's
    top edge, not the scaler's."""
    assert gm.AXIS_V.origin_offset(2.0) < 5
    assert gm.AXIS_H.origin_offset(1.58) == pytest.approx(94, abs=1.0)


def test_high_magnification_cannot_reach_the_left_of_the_panel():
    """A real limit, not a rounding choice. VDS_HB_SP cannot go negative and the
    write start is 55 + 25 x m after it, so above ~x2.9 the picture cannot begin
    at the panel's left edge however the registers are set."""
    corner, window_sp = gm.place_picture(256, False, gm.AXIS_H)   # x4.0

    assert window_sp == 0
    assert corner > gm.PANEL_VISIBLE_LEFT


def test_below_that_the_picture_sits_on_the_panel_edge_and_the_window_moves():
    """At every magnification it can reach, the corner is the same place on
    screen -- so a scale change does not walk the picture sideways."""
    corners = [gm.place_picture(scale, False, gm.AXIS_H)[0]
               for scale in (1023, 700, 512)]

    assert corners == [gm.PANEL_VISIBLE_LEFT] * 3, corners


# --- the straight line underneath both fits -----------------------------------


def test_a_line_fit_recovers_the_line_it_was_given():
    """Every geometry question here reduces to "is this quantity a straight line
    in that one", so the fit is shared. Synthetic points on a known line must come
    back as that line."""
    xs = [1.0, 2.0, 3.0, 4.0]
    ys = [3.0 * x + 7.0 for x in xs]

    slope, intercept = gm.fit_line(xs, ys)

    assert abs(slope - 3.0) < 1e-9
    assert abs(intercept - 7.0) < 1e-9


def test_a_residual_shows_the_point_that_is_off_the_line():
    """The residuals are the whole point of fitting here rather than solving.
    Three models were believed on this bench because nothing was ever printed
    that could contradict them, so one point pushed off the line must show up as
    that point."""
    xs = [1.0, 2.0, 3.0, 4.0, 5.0]
    ys = [3.0 * x + 7.0 for x in xs]
    ys[2] += 10.0

    residuals = gm.line_residuals(xs, ys, *gm.fit_line(xs, ys))

    assert residuals.index(max(residuals)) == 2


def test_a_line_fit_refuses_points_that_share_one_x():
    """Slope and intercept are not separable from a single x, so such a fit can
    never fail -- which is exactly how a pair of horizontal readings taken at one
    scale "proved" a fixed capture loss they could not have distinguished."""
    with pytest.raises(ValueError):
        gm.fit_line([2.0, 2.0, 2.0], [1.0, 5.0, 9.0])


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
MEASURED_H = [(798, 1023, 785), (798, 800, 1014), (400, 1023, 386),
              (400, 512, 811), (200, 320, 680)]
MEASURED_V = [(511, 1023, 487), (511, 700, 723), (511, 512, 997),
              (300, 1023, 275), (300, 512, 575), (200, 300, 658)]

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
WIN_SP_WHEN_MEASURED = {"h": 35, "v": 37}
ASSUMED_CORNER = {"h": 129, "v": 63}


def _recorded_far_edge(capture, scale, axis, key):
    magnification = 1024 / scale
    write_start = WIN_SP_WHEN_MEASURED[key] + axis.origin_offset(magnification)
    return write_start + capture * magnification - ASSUMED_CORNER[key]


@pytest.mark.parametrize("capture,scale,recorded", MEASURED_H)
def test_a_pure_multiply_reproduces_every_horizontal_reading(capture, scale,
                                                             recorded):
    """produced = capture x 1024 / scale, with no loss term at either end."""
    assert abs(_recorded_far_edge(capture, scale, gm.AXIS_H, "h")
               - recorded) <= gm.AXIS_H.margin


@pytest.mark.parametrize("capture,scale,recorded", MEASURED_V)
def test_a_pure_multiply_reproduces_every_vertical_reading(capture, scale,
                                                           recorded):
    assert abs(_recorded_far_edge(capture, scale, gm.AXIS_V, "v")
               - recorded) <= gm.AXIS_V.margin


def test_produced_is_a_pure_multiply():
    """The headline. capture_offset and output_loss are gone from both axes --
    they were the write start's magnification term, seen from the far end."""
    assert gm.produced_px(400, 512, axis=gm.AXIS_H) == pytest.approx(800.0)
    assert gm.produced_px(200, 300, axis=gm.AXIS_V) == pytest.approx(682.67,
                                                                    abs=0.01)


def test_the_old_two_term_model_is_not_quietly_restored():
    """`produced = (capture - c) x m - k` fitted these same readings to 0.43 px
    and was wrong anyway: it was measuring a fixed span from a moving origin, so
    it absorbed the origin's magnification term as a loss and split it across
    both ends. It cannot be distinguished from the truth by residuals alone --
    only by measuring the near edge, which is what settled it. So this guards the
    shape rather than the fit."""
    assert not hasattr(gm.AXIS_H, "capture_offset")
    assert not hasattr(gm.AXIS_H, "output_loss")


def test_the_two_axes_differ_only_in_where_the_write_starts():
    """The old model had them as different SHAPES -- the horizontal needing both
    terms, the vertical only one. They are the same shape. What differs is the
    pipeline latency before the first write: ~25 px per unit magnification for an
    11-tap horizontal filter, ~1 line for a vertical line buffer."""
    assert gm.produced_px(400, 512, axis=gm.AXIS_H) == \
           gm.produced_px(400, 512, axis=gm.AXIS_V)
    assert gm.AXIS_H.start_per_mag > 20 * gm.AXIS_V.start_per_mag


# --- the vertical axis, against a fully measured state ------------------------


def test_the_picture_is_never_placed_outside_the_frame_buffer():
    """VDS_?B_SP..ST is what memory is valid; VDS_DIS_?B_SP..ST is what part of
    it reaches the output. So the display window has to sit INSIDE the memory
    window, and the corner is only a constant if the memory window is too.

    The bench found this the hard way: VDS_VB_SP had been moved to 199 while the
    corner stayed pinned at 63, putting the display window 136 lines before any
    valid memory. The picture was really at 199 + 26 = 225, so the screen was
    letterboxed at the top and clipped at the bottom at the same time.
    """
    for total, axis, corner in ((1445, gm.AXIS_H, 129), (1126, gm.AXIS_V, 63)):
        solved = gm.solve_axis(500, 650, False, total, axis, origin=corner)

        assert solved["window_sp"] <= solved["display_sp"], (
            f"{axis.name}: display window starts before valid memory")
        assert solved["display_st"] <= solved["window_st"], (
            f"{axis.name}: display window runs past valid memory")


def test_the_pixel_perfect_alignment_is_reproduced_at_its_own_magnification():
    """All four sides aligned by eye, 2026-08-05: VDS_HB_SP 35, picture starting
    at 129, at x1.58. 129 is this formula evaluated at that magnification, not a
    constant corner -- holding it fixed while the scale changes puts scratch on
    the screen."""
    assert 35 + gm.AXIS_H.origin_offset(1.58) == pytest.approx(129, abs=1.0)


def test_the_vertical_corner_was_the_bezel_all_along():
    """CORNER_V was 63 and PANEL_VISIBLE_TOP is 63, measured the same way -- creep
    the near edge until something vanishes -- and 37 + 26 = 63 made them agree.
    Measured properly, with the picture moved well clear of the bezel first, the
    vertical write start is 2 lines after VDS_VB_SP. So 63 was where the panel
    stops showing, not where the scaler starts writing."""
    assert gm.AXIS_V.origin_offset(2.0) < 5
    assert 37 + gm.AXIS_V.origin_offset(2.0) < gm.PANEL_VISIBLE_TOP


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
