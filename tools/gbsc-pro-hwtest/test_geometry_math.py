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
    """Bench reference: capture 798 at HSCALE 650 produces 1257 px on a 1445 px
    line, so centred puts the corner at (1445-1257)/2 = 94.

    It cannot go there. At x1.575 the write start is 94.4 px after VDS_HB_SP, so
    a corner of 94 needs the register below its floor of 8 -- where the display
    corrupts. The picture is pushed right to 102 instead. A near-1:1 picture is
    centred exactly; a large one is centred as far as the hardware allows.
    """
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["origin"] == 102
    assert axis["window_sp"] == gm.AXIS_H.window_sp_min


def test_the_memory_window_is_exactly_the_display_window():
    """**ALLOCATE ONLY WHAT IS DISPLAYED.**

    Taking the raster's last usable value instead -- 1443 on a 1445 px line --
    assumes the far end is free once the origin is pinned. It is not: memory past
    the picture is memory the playback stage still walks, and it shows as
    artefacts down the LEFT edge.
    """
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert axis["window_st"] == axis["display_st"]
    assert axis["window_st"] <= 1443


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


def test_there_is_no_headroom_to_report_because_the_window_is_the_picture():
    """`margin_given` is gone, and its absence is the point.

    It reported memory window minus produced, and two tests measured it against
    a 100 px warn floor. The window is now exactly the display window, so the
    quantity is structurally about zero at every framing -- a floor would fire
    on all of them, and a number that always says the same thing is not a
    measurement.

    The slack question moved rather than vanished: PB_FETCH_NUM tracks the
    capture width now, so whether the line finishes reading is decided by the
    burst size. See Tv5725::Memory.
    """
    axis = gm.solve_axis(798, 650, False, 1445, gm.AXIS_H)

    assert "margin_given" not in axis
    assert axis["window_st"] == axis["display_st"]


def test_a_picture_too_wide_for_the_line_is_still_reported():
    """Scale is the user's to set. A picture that overruns the raster is clamped
    at the last usable register and that must be said, not swallowed -- this is
    the one shortfall that survives the headroom rule's removal, because it is
    about the RASTER rather than about spare window.
    """
    axis = gm.solve_axis(798, 570, False, 1445, gm.AXIS_H)

    assert axis["produced"] > 1400
    assert axis["display_st"] == 1443, "clamped at the raster's last usable"
    assert axis["window_st"] == axis["display_st"]


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


def test_the_picture_is_centred_on_the_raster():
    """Centred, not pinned to an assumed panel edge.

    Where a TV stops showing is a property of the TV, and this file describes a
    scaler. PANEL_VISIBLE_LEFT was 127 and measured 90 on the bench display;
    the next panel will differ again. Centring needs no such number and leaves
    the user free to find their own edges with pan and scale.
    """
    corner, window_sp = gm.place_picture(produced=845, raster_total=1445,
                                         magnification=2.0, axis=gm.AXIS_H)

    assert corner == 300
    assert 1445 - (corner + 845) == corner, "margins must match either side"


def test_centring_moves_the_memory_window_not_the_picture():
    """The corner is where the picture goes; VDS_?B_SP is whatever puts the write
    start there. At x2 the scaler starts 105 px after the register."""
    corner, window_sp = gm.place_picture(produced=845, raster_total=1445,
                                         magnification=2.0, axis=gm.AXIS_H)

    assert window_sp + gm.AXIS_H.origin_offset(2.0) == corner


def test_a_picture_too_wide_to_centre_is_pinned_as_far_over_as_it_goes():
    """A picture wider than the raster cannot be centred -- it starts at the write
    floor and overscans off the far end. The floor is VDS_?B_SP's minimum, not
    zero: below 8 the horizontal display corrupts, measured on the bench
    2026-08-06."""
    corner, window_sp = gm.place_picture(produced=1400, raster_total=1445,
                                         magnification=2.0, axis=gm.AXIS_H)

    assert window_sp == gm.AXIS_H.window_sp_min
    assert corner == round(gm.AXIS_H.window_sp_min
                           + gm.AXIS_H.origin_offset(2.0))


def test_the_memory_window_is_never_placed_where_the_picture_corrupts():
    """VDS_HB_SP below 8 corrupts the display -- a hard floor, and 0 is inside
    it. Nothing computed here may return a value that breaks the picture."""
    for magnification in (1.001, 1.416, 2.0, 3.2, 4.0):
        _, window_sp = gm.place_picture(produced=1400, raster_total=1445,
                                        magnification=magnification,
                                        axis=gm.AXIS_H)

        assert window_sp >= 8, f"x{magnification}: VDS_HB_SP {window_sp}"


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






def test_zoom_stays_inside_the_scale_register():
    """A capture small enough would ask for a scale below the x4 limit."""
    sp, st, scale = gm.zoom_capture(264, 1062, max_produced=1408, step=600)

    assert scale >= gm.HSCALE_MIN


# --- the scale control prefers the capture over the picture size --------------







# --- the picture size is computed, never inherited ----------------------------


def test_the_picture_is_made_as_big_as_the_raster_allows():
    """**CALCULATE THE STARTING STATE; DO NOT READ IT BACK.** Start with the
    display window as big as it can get and scale the window to fit.

    Inheriting the current size freezes a picture at 620 lines that no zoom step
    can grow. Computed from the capture and the raster, it cannot happen.
    """
    scale, produced = gm.fit_to_raster(798, 1445, gm.AXIS_H)
    corner, window_sp = gm.place_picture(produced, 1445, 1024 / scale, gm.AXIS_H)

    assert window_sp >= gm.AXIS_H.window_sp_min, "must not corrupt"
    assert corner + produced <= 1443, "must fit inside the raster"
    assert window_sp <= gm.AXIS_H.window_sp_min + 4, (
        "and must use nearly all of it -- VDS_HB_SP should sit on its floor")


def test_a_smaller_capture_still_fills_the_raster():
    """The whole point: the picture size does not depend on how much source is
    being captured. Crop harder and the scale takes up the slack."""
    for capture in (798, 400, 200, 155):
        scale, produced = gm.fit_to_raster(capture, 1445, gm.AXIS_H)
        corner, window_sp = gm.place_picture(produced, 1445, 1024 / scale,
                                             gm.AXIS_H)

        assert corner + produced <= 1443, f"capture {capture} overflows"
        assert window_sp >= gm.AXIS_H.window_sp_min, f"capture {capture} corrupts"
        if scale > gm.HSCALE_MIN:
            assert window_sp <= gm.AXIS_H.window_sp_min + 4, (
                f"capture {capture} left the raster half used")


def test_the_scale_register_bounds_how_big_the_picture_can_get():
    """A tiny capture cannot fill the raster -- 2.048x is the most magnification
    allowed. That is a limit, not a failure, and it must still fit.

    Michael, 2026-08-09: the scale "shouldn't be any smaller than 500 - after
    that the scaling artifacts a lot." The register itself reaches 256; this is
    a picture-quality ceiling above it.
    """
    assert gm.HSCALE_MIN == 500
    scale, produced = gm.fit_to_raster(50, 1445, gm.AXIS_H)

    assert scale == gm.HSCALE_MIN
    assert produced == 50 * 1024 / gm.HSCALE_MIN


def test_a_zoom_step_changes_the_crop_and_the_picture_stays_full_size():
    """One pad. It crops; the picture is always as big as the raster allows and
    the scale is derived to suit. No second control, nothing frozen."""
    step = gm.scale_step(sp=264, st=1062, delta=-64, raster_total=1445,
                         axis=gm.AXIS_H, wrap_at=1277)

    assert step["st"] - step["sp"] < 798, "zoom in must crop"
    _, full = gm.fit_to_raster(step["st"] - step["sp"], 1445, gm.AXIS_H)
    assert abs(step["produced"] - full) < 0.5, "and still fill the raster"


def test_zooming_out_shows_more_source_at_the_same_picture_size():
    """Reducing the scale must reveal more source, not shrink the picture."""
    out = gm.scale_step(sp=264, st=1062, delta=+64, raster_total=1445,
                        axis=gm.AXIS_H, wrap_at=1277)

    assert out["st"] - out["sp"] > 798, "must take in more source"
    assert out["produced"] > 1443 - 220, "and the picture must stay full size"


def test_a_zoom_step_out_and_back_returns_every_register():
    """Symmetric about the centre, so it is exactly reversible."""
    state = dict(sp=264, st=1062)
    for _ in range(5):
        step = gm.scale_step(**state, delta=+64, raster_total=1445,
                             axis=gm.AXIS_H, wrap_at=1277)
        state = dict(sp=step["sp"], st=step["st"])
    for _ in range(5):
        step = gm.scale_step(**state, delta=-64, raster_total=1445,
                             axis=gm.AXIS_H, wrap_at=1277)
        state = dict(sp=step["sp"], st=step["st"])

    assert (state["sp"], state["st"]) == (264, 1062)


def test_a_zoom_step_never_reads_the_scale_that_happens_to_be_set():
    """The bug behind every size problem tonight. The step takes no scale
    argument at all, so a wrong one cannot be inherited."""
    import inspect

    assert "scale" not in inspect.signature(gm.scale_step).parameters

def test_a_zoom_in_never_crops_the_capture_away_to_nothing():
    """A control that can destroy the capture is one keypress from a dead
    picture with no way back."""
    sp, st = 264, 290
    for _ in range(20):
        step = gm.scale_step(sp=sp, st=st, delta=-16, raster_total=1445,
                             axis=gm.AXIS_H, wrap_at=1277)
        sp, st = step["sp"], step["st"]

    assert st - sp >= 16


def test_a_zoom_out_stops_at_the_line_rather_than_wrapping_it():
    """IF_HB_ST2 wraps at IF_HSYNC_RST+1 and crossing it makes the picture jump,
    which has been misread as losing the capture window."""
    sp, st = 264, 1062
    for _ in range(40):
        step = gm.scale_step(sp=sp, st=st, delta=+32, raster_total=1445,
                             axis=gm.AXIS_H, wrap_at=1277)
        sp, st = step["sp"], step["st"]

    assert sp >= 0 and st <= 1276


# --- the active window ---------------------------------------------------------
#
# With a standard back porch there is a real active window inside the raster, and
# the picture belongs in that rather than centred on the whole raster with the
# write latency as its only floor.
#
# The blanking is symmetric -- the back porch reserved at BOTH ends. That
# guarantees a front porch rather than assuming the encoder tolerates none, it is
# derived rather than measured off one TV, and it lands within 30 px of the
# 110..2189 state confirmed working on the bench at a 2301 raster.


def test_the_active_window_narrows_the_room_at_both_ends():
    """A back porch larger than the write floor becomes the binding constraint.

    Which is the point of it: at a 1918 raster the write floor is about 105 and
    the standard back porch is 140, so the latency now lands INSIDE the blanking
    instead of eating active video.
    """
    raster = 1918
    without = gm.max_display_window(raster, gm.AXIS_H)
    with_bp = gm.max_display_window(raster, gm.AXIS_H, active_start=140)

    assert with_bp < without
    assert with_bp == (raster - 2) - 2 * 140


def test_an_active_start_below_the_write_floor_changes_nothing():
    """The write floor is physical and the back porch cannot argue with it.

    So the room is bounded by whichever is LARGER, and a small back porch is
    simply not the binding constraint.
    """
    raster = 1918
    floor = gm.AXIS_H.window_sp_min + gm.AXIS_H.start_const
    unbounded = gm.max_display_window(raster, gm.AXIS_H)

    assert gm.max_display_window(raster, gm.AXIS_H, active_start=floor - 20) == unbounded
    assert gm.max_display_window(raster, gm.AXIS_H, active_start=0) == unbounded


def test_the_picture_starts_no_earlier_than_the_back_porch():
    """Centred, but never before active video is allowed to begin."""
    raster, produced = 1918, 1638.0
    corner, window_sp = gm.place_picture(produced, raster, 1.69, gm.AXIS_H,
                                         active_start=140)
    assert corner >= 140
    assert window_sp >= gm.AXIS_H.window_sp_min

    # and symmetric: what is reserved at the near end is reserved at the far end
    assert raster - (corner + produced) == pytest.approx(corner, abs=1.5)


def test_a_picture_too_big_to_centre_starts_at_the_back_porch():
    """Overscan off the far end rather than start inside the blanking."""
    raster = 1918
    corner, _ = gm.place_picture(2400.0, raster, 2.0, gm.AXIS_H, active_start=140)
    assert corner == 140


def test_the_default_is_the_old_behaviour_exactly():
    """active_start defaults to 0, so every existing caller is unchanged.

    This is what makes the change additive: the whole geometry suite, and the
    eleven bench readings it encodes, still describe the same arithmetic.
    """
    for raster in (1445, 1918, 2301, 2877):
        assert gm.max_display_window(raster, gm.AXIS_H) == \
               gm.max_display_window(raster, gm.AXIS_H, active_start=0)
        for produced in (800.0, 1253.0, 2079.0):
            assert gm.place_picture(produced, raster, 1.5, gm.AXIS_H) == \
                   gm.place_picture(produced, raster, 1.5, gm.AXIS_H, active_start=0)
