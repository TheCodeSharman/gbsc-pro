#!/usr/bin/env python3
"""Least-squares fits for the bench measurement jigs.

Straight-line and two-term fits with their residuals. The firmware has no
equivalent and never will: it computes registers from held state, while this is
for deciding what a series of eyeball readings says. Kept apart from the model it
is used to test so that it survives the model.
"""

import math

from geometry_math import HSCALE_UNITY

def fit_line(xs, ys):
    """Least squares. Returns (slope, intercept) for y = slope x + intercept.

    Every geometry question that has been asked on this bench reduces to one of
    these -- is `produced` linear in magnification, does the write start move
    with the memory window -- so the fitting lives in one place and the meaning
    lives with the caller.

    Raises if every x is the same value: slope and intercept are then not
    separable, and a fit that cannot fail has not tested anything.
    """
    if len(xs) < 2:
        raise ValueError("need at least two measurements")
    if len(set(round(x, 9) for x in xs)) < 2:
        raise ValueError("every point is at the same x, so the slope and the "
                         "intercept cannot be separated -- vary it")
    n = len(xs)
    sx, sy = sum(xs), sum(ys)
    denominator = n * sum(x * x for x in xs) - sx * sx
    slope = (n * sum(x * y for x, y in zip(xs, ys)) - sx * sy) / denominator
    return slope, (sy - slope * sx) / n


def line_residuals(xs, ys, slope, intercept):
    """How far each measurement sits off the fitted line, in y's units."""
    return [y - (slope * x + intercept) for x, y in zip(xs, ys)]


def fit_loss(points):
    """Fit the two loss terms to measurements of `produced`.

    `points` is [(capture, scale, produced)]. Returns (c, k) where

        produced = (capture - c) x 1024 / scale - k

    c is in the axis's input units, k in output units. The fit is a straight
    line, because the deficit is linear in magnification:

        capture x m - produced = c x m + k        with m = 1024/scale

    so the slope is c and the intercept is k.

    TWO POINTS ARE NOT EVIDENCE. Any two land exactly on some line, so a
    two-point fit cannot disconfirm the model -- and three separate readings this
    evening looked confirmed on exactly that basis and were later refuted. Use
    three or more distinct magnifications and read `loss_residuals`.
    """
    if len(points) < 2:
        raise ValueError("need at least two measurements")
    xs = [HSCALE_UNITY / scale for _, scale, _ in points]
    ys = [capture * HSCALE_UNITY / scale - produced
          for capture, scale, produced in points]
    if len(set(round(x, 9) for x in xs)) < 2:
        raise ValueError(
            "every point is at the same magnification, so c and k cannot be "
            "separated -- vary the scale, not just the capture")
    return fit_line(xs, ys)


def loss_residuals(points, c, k):
    """How far each measurement sits from the fitted line, in output units.

    This is what says whether the model is right. Residuals within the +-1 that
    floor() and an eye judgement can account for mean the line holds; a
    systematic pattern in them means it does not, however well the fit reads.
    """
    return [produced - ((capture - c) * HSCALE_UNITY / scale - k)
            for capture, scale, produced in points]

