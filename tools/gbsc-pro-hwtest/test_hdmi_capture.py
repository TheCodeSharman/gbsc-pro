"""Unit tests for the emitted-frame measurement -- `pytest tools/gbsc-pro-hwtest/`.

These need no unit: the dongle is mocked out by handing `borders()` a frame.
"""

import numpy as np

from hdmi_capture import BLACK, borders


def frame(width=1920, height=1080):
    return np.zeros((height, width), np.float32)


def test_a_filled_frame_has_no_margin():
    f = frame()
    f[:, :] = 128
    assert borders(f)["left"] == 0


def test_a_black_bar_is_measured_where_the_picture_starts():
    f = frame()
    f[:, 313:] = 128
    edge = borders(f)
    assert edge["left"] == 313
    assert edge["width"] == 1920 - 313


def test_a_one_column_artefact_at_the_edge_is_not_the_picture():
    """The dongle puts a narrow bright column at the very left of some frames.

    Taking the FIRST lit column reported a 313 px bar as a 3 px one, which is a
    break reported as a fill -- and every fill/break count taken through it was
    wrong in that direction.
    """
    f = frame()
    f[:, 3] = 200           # the artefact
    f[:, 313:] = 128        # the real picture
    assert borders(f)["left"] == 313


def test_a_narrow_run_of_artefact_columns_is_not_the_picture():
    f = frame()
    f[:, 3:6] = 200
    f[:, 313:] = 128
    assert borders(f)["left"] == 313


def test_a_sparse_hot_pixel_column_is_not_the_picture():
    """A column lit in a handful of its rows is noise, not content."""
    f = frame()
    f[0:5, 50] = 255
    f[:, 313:] = 128
    assert borders(f)["left"] == 313


def test_an_all_black_frame_reports_no_picture():
    edge = borders(frame())
    assert edge["width"] == 0 and edge["height"] == 0


def test_the_right_edge_is_measured_the_same_way():
    f = frame()
    f[:, 100:1800] = 128
    f[:, 1917] = 200        # artefact at the right edge
    edge = borders(f)
    assert edge["left"] == 100
    assert edge["right"] == 1920 - 1800


def test_a_dark_but_lit_picture_still_counts():
    f = frame()
    f[:, 200:] = BLACK + 2
    assert borders(f)["left"] == 200
