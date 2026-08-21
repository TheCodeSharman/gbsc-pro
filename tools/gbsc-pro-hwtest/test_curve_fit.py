"""curve_fit, against fits whose answer is known.

Synthetic points with a known slope and intercept must come back as that slope
and intercept, or the fit is not measuring what it claims. Nothing here touches
the scaler: these are the least-squares helpers the bench jigs decide with, and
they outlive whatever model is being fitted.
"""

import pytest

import curve_fit as cf


def test_the_fit_recovers_a_loss_it_was_given():
    """The deficit is linear in magnification -- capture x m - produced = c x m + k
    -- so a straight-line fit recovers both terms. Synthetic points with a known
    c and k must come back as that c and k, or the fit is not measuring what it
    claims."""
    points = [(capture, scale, (capture - 4.0) * 1024 / scale - 20.0)
              for capture, scale in ((400, 1023), (400, 512), (798, 800))]

    c, k = cf.fit_loss(points)

    assert abs(c - 4.0) < 0.01
    assert abs(k - 20.0) < 0.01


def test_the_fit_needs_two_different_magnifications():
    """Every point at one scale lies on a single vertical line, so c and k cannot
    be separated -- which is exactly how the bench measurements at HSCALE 1023
    looked conclusive and were not."""
    with pytest.raises(ValueError):
        cf.fit_loss([(400, 1023, 386.0), (798, 1023, 785.0)])


def test_the_bench_vertical_pair_fits_the_measured_numbers():
    """2026-08-05, capture 511 half-lines at two scales, produced read off the
    screen by creeping VDS_DIS_VB_ST until the unwritten band vanished."""
    c, k = cf.fit_loss([(511, 550, 923.0), (511, 1023, 486.0)])

    assert 2 < c < 5, f"c = {c:.2f} half-lines"
    assert 20 < k < 25, f"k = {k:.2f} lines"


def test_a_line_fit_recovers_the_line_it_was_given():
    """Every geometry question here reduces to "is this quantity a straight line
    in that one", so the fit is shared. Synthetic points on a known line must come
    back as that line."""
    xs = [1.0, 2.0, 3.0, 4.0]
    ys = [3.0 * x + 7.0 for x in xs]

    slope, intercept = cf.fit_line(xs, ys)

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

    residuals = cf.line_residuals(xs, ys, *cf.fit_line(xs, ys))

    assert residuals.index(max(residuals)) == 2


def test_a_line_fit_refuses_points_that_share_one_x():
    """Slope and intercept are not separable from a single x, so such a fit can
    never fail -- which is exactly how a pair of horizontal readings taken at one
    scale "proved" a fixed capture loss they could not have distinguished."""
    with pytest.raises(ValueError):
        cf.fit_line([2.0, 2.0, 2.0], [1.0, 5.0, 9.0])
