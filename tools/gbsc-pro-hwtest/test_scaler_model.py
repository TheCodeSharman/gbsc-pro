"""scaler_model against the bench readings it exists to be compared with.

The write start's two terms and the placement rule, checked against the eleven
far-edge readings of 2026-08-05 and the pixel-perfect state of 2026-08-06. This
module mirrors what the firmware engine decides, so these are the tests that go
with it when the measurement jigs no longer need a reference of their own -- they
are NOT a check on the firmware, which owns the real arithmetic.
"""

import pytest

import geometry_math as gm
import scaler_model as sm


ORIGIN_H = [(1.0009775171065494, 80), (2.0, 105), (3.2, 135)]


ORIGIN_V = [(1.0009775171065494, 1), (2.0, 2), (3.4133333333333336, 3)]


@pytest.mark.parametrize("magnification,expected", ORIGIN_H)
def test_the_horizontal_write_start_matches_every_reading(magnification, expected):
    """It is not a constant. It walks right at ~25 px per unit magnification,
    which is ~25 INPUT samples of run-up the scaler consumes before it writes
    anything -- IF_HS_TAP11_BYPS names an 11-tap filter."""
    assert abs(sm.AXIS_H.origin_offset(magnification) - expected) < 1.0


@pytest.mark.parametrize("magnification,expected", ORIGIN_V)
def test_the_vertical_write_start_matches_every_reading(magnification, expected):
    """Nearly flat, because the vertical reads whole lines from a line buffer and
    has no interpolator to feed. ~30x smaller than the horizontal term."""
    assert abs(sm.AXIS_V.origin_offset(magnification) - expected) < 1.0


@pytest.mark.parametrize("magnification,recorded", [(1.001, 80), (1.575, 93)])
def test_the_readings_that_were_recorded_as_unexplained_also_fit(magnification,
                                                                 recorded):
    """solve_axis's docstring recorded 80 at x1.001 and 93 at x1.575 and said
    nothing predicted it; ORIGIN_OFFSET_H carried 78 and 94 as irreconcilable.
    One formula, four magnifications -- they were never in conflict, they were
    measured at different scales."""
    assert abs(sm.AXIS_H.origin_offset(magnification) - recorded) <= 1.5


def test_the_bezel_is_not_the_write_start():
    """CORNER_H 129 and CORNER_V 63 were measured at one magnification each and
    treated as constants. 63 is also PANEL_VISIBLE_TOP, and the vertical write
    start is 2 lines after VDS_VB_SP, not 26 -- so 37 + 26 = 63 was the panel's
    top edge, not the scaler's."""
    assert sm.AXIS_V.origin_offset(2.0) < 5
    assert sm.AXIS_H.origin_offset(1.58) == pytest.approx(94, abs=1.0)


def test_the_picture_is_centred_on_the_raster():
    """Centred, not pinned to an assumed panel edge.

    Where a TV stops showing is a property of the TV, and this file describes a
    scaler. PANEL_VISIBLE_LEFT was 127 and measured 90 on the bench display;
    the next panel will differ again. Centring needs no such number and leaves
    the user free to find their own edges with pan and scale.
    """
    corner, window_sp = sm.place_picture(produced=845, raster_total=1445,
                                         magnification=2.0, axis=sm.AXIS_H)

    assert corner == 300
    assert 1445 - (corner + 845) == corner, "margins must match either side"


def test_centring_moves_the_memory_window_not_the_picture():
    """The corner is where the picture goes; VDS_?B_SP is whatever puts the write
    start there. At x2 the scaler starts 105 px after the register."""
    corner, window_sp = sm.place_picture(produced=845, raster_total=1445,
                                         magnification=2.0, axis=sm.AXIS_H)

    assert window_sp + sm.AXIS_H.origin_offset(2.0) == corner


def test_a_picture_too_wide_to_centre_is_pinned_as_far_over_as_it_goes():
    """A picture wider than the raster cannot be centred -- it starts at the write
    floor and overscans off the far end. The floor is VDS_?B_SP's minimum, not
    zero: below 8 the horizontal display corrupts, measured on the bench
    2026-08-06."""
    corner, window_sp = sm.place_picture(produced=1400, raster_total=1445,
                                         magnification=2.0, axis=sm.AXIS_H)

    assert window_sp == sm.AXIS_H.window_sp_min
    assert corner == round(sm.AXIS_H.window_sp_min
                           + sm.AXIS_H.origin_offset(2.0))


def test_the_memory_window_is_never_placed_where_the_picture_corrupts():
    """VDS_HB_SP below 8 corrupts the display -- a hard floor, and 0 is inside
    it. Nothing computed here may return a value that breaks the picture."""
    for magnification in (1.001, 1.416, 2.0, 3.2, 4.0):
        _, window_sp = sm.place_picture(produced=1400, raster_total=1445,
                                        magnification=magnification,
                                        axis=sm.AXIS_H)

        assert window_sp >= 8, f"x{magnification}: VDS_HB_SP {window_sp}"


MEASURED_H = [(798, 1023, 785), (798, 800, 1014), (400, 1023, 386),
              (400, 512, 811), (200, 320, 680)]


MEASURED_V = [(511, 1023, 487), (511, 700, 723), (511, 512, 997),
              (300, 1023, 275), (300, 512, 575), (200, 300, 658)]


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
    assert abs(_recorded_far_edge(capture, scale, sm.AXIS_H, "h")
               - recorded) <= sm.AXIS_H.margin


@pytest.mark.parametrize("capture,scale,recorded", MEASURED_V)
def test_a_pure_multiply_reproduces_every_vertical_reading(capture, scale,
                                                           recorded):
    assert abs(_recorded_far_edge(capture, scale, sm.AXIS_V, "v")
               - recorded) <= sm.AXIS_V.margin


def test_the_old_two_term_model_is_not_quietly_restored():
    """`produced = (capture - c) x m - k` fitted these same readings to 0.43 px
    and was wrong anyway: it was measuring a fixed span from a moving origin, so
    it absorbed the origin's magnification term as a loss and split it across
    both ends. It cannot be distinguished from the truth by residuals alone --
    only by measuring the near edge, which is what settled it. So this guards the
    shape rather than the fit."""
    assert not hasattr(sm.AXIS_H, "capture_offset")
    assert not hasattr(sm.AXIS_H, "output_loss")


def test_the_two_axes_differ_only_in_where_the_write_starts():
    """The old model had them as different SHAPES -- the horizontal needing both
    terms, the vertical only one. They are the same shape. What differs is the
    pipeline latency before the first write: ~25 px per unit magnification for an
    11-tap horizontal filter, ~1 line for a vertical line buffer."""
    assert sm.AXIS_H.start_per_mag > 20 * sm.AXIS_V.start_per_mag


def test_the_pixel_perfect_alignment_is_reproduced_at_its_own_magnification():
    """All four sides aligned by eye, 2026-08-05: VDS_HB_SP 35, picture starting
    at 129, at x1.58. 129 is this formula evaluated at that magnification, not a
    constant corner -- holding it fixed while the scale changes puts scratch on
    the screen."""
    assert 35 + sm.AXIS_H.origin_offset(1.58) == pytest.approx(129, abs=1.0)


def test_the_vertical_corner_was_the_bezel_all_along():
    """CORNER_V was 63 and PANEL_VISIBLE_TOP is 63, measured the same way -- creep
    the near edge until something vanishes -- and 37 + 26 = 63 made them agree.
    Measured properly, with the picture moved well clear of the bezel first, the
    vertical write start is 2 lines after VDS_VB_SP. So 63 was where the panel
    stops showing, not where the scaler starts writing."""
    assert sm.AXIS_V.origin_offset(2.0) < 5
    assert 37 + sm.AXIS_V.origin_offset(2.0) < gm.PANEL_VISIBLE_TOP


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
    corner, window_sp = sm.place_picture(produced, raster, 1.69, sm.AXIS_H,
                                         active_start=140)
    assert corner >= 140
    assert window_sp >= sm.AXIS_H.window_sp_min

    # and symmetric: what is reserved at the near end is reserved at the far end
    assert raster - (corner + produced) == pytest.approx(corner, abs=1.5)


def test_a_picture_too_big_to_centre_starts_at_the_back_porch():
    """Overscan off the far end rather than start inside the blanking."""
    raster = 1918
    corner, _ = sm.place_picture(2400.0, raster, 2.0, sm.AXIS_H, active_start=140)
    assert corner == 140
