"""The wedge instrument, against frames whose answer is known."""

import numpy as np
import pytest

import wedge


def _card(width=1000, height=400, period=4, run=(0.30, 0.50)):
    """A band of grating inside flat card, at `run` of the width."""
    frame = np.full((height, width), 120, np.float32)
    left, right = int(run[0] * width), int(run[1] * width)
    frame[:, left:right:period] = 230
    return frame


def _box(frame):
    return (0, frame.shape[1] - 1, 0, frame.shape[0] - 1)


def test_the_grating_is_found_where_it_was_drawn():
    frame = _card()
    left, right, _, _ = wedge.locate(frame, _box(frame))

    assert 300 <= left <= 340
    assert right < 500


def test_a_frame_with_no_grating_in_it_refuses_to_locate_one():
    flat = np.full((400, 1000), 120, np.float32)

    with pytest.raises(AssertionError, match="no grating"):
        wedge.locate(flat, _box(flat))


def test_the_flattest_patch_is_not_the_grating():
    frame = _card()
    grating = wedge.locate(frame, _box(frame))
    flat = wedge.flattest(frame, _box(frame), grating)

    assert flat[1] <= grating[0] or flat[0] >= grating[1]
    assert wedge.score(frame[None], flat)[0] < wedge.score(frame[None], grating)[0]


def test_the_flattest_patch_matches_the_gratings_shape():
    frame = _card()
    grating = wedge.locate(frame, _box(frame))
    flat = wedge.flattest(frame, _box(frame), grating)

    assert flat[1] - flat[0] == grating[1] - grating[0]
    assert (flat[2], flat[3]) == (grating[2], grating[3])


def test_a_static_patch_carries_no_movement():
    frame = _card()
    still = np.stack([frame, frame])

    assert wedge.score(still, wedge.locate(frame, _box(frame)))[1] == 0.0


def test_movement_is_how_far_a_pixel_travels_between_frames():
    frame = _card()
    clip = np.stack([frame, frame + 10.0])

    assert wedge.score(clip, wedge.locate(frame, _box(frame)))[1] == pytest.approx(5.0)
