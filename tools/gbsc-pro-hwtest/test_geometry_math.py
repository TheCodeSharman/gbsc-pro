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


# --- the origin is measured, not assumed --------------------------------------


# --- solving an axis ----------------------------------------------------------


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


# --- the starting-point rule -------------------------------------------------


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


# --- the scale control prefers the capture over the picture size --------------


# --- the picture size is computed, never inherited ----------------------------


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

