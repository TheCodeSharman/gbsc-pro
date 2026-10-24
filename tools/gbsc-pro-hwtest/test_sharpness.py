"""The global sharpness measure, against frames whose answer is known."""

import numpy as np

import sharpness


def _grid(width=400, height=200, period=8, levels=(40, 200)):
    """Vertical bars: horizontal structure, no vertical structure."""
    frame = np.full((height, width), levels[0], np.float32)
    frame[:, ::period] = levels[1]
    return frame


def _lit(frame):
    return (0, frame.shape[1] - 1, 0, frame.shape[0] - 1)


def test_a_blurred_frame_is_less_sharp_than_the_frame_it_came_from():
    sharp = _grid()
    blurred = np.stack([sharp, np.roll(sharp, 1, axis=1)]).mean(axis=0)

    assert (sharpness.measure(blurred[None], _lit(blurred))["mean_dx"]
            < sharpness.measure(sharp[None], _lit(sharp))["mean_dx"])


def test_vertical_bars_carry_no_vertical_gradient():
    bars = _grid()
    reading = sharpness.measure(bars[None], _lit(bars))

    assert reading["mean_dy"] == 0.0
    assert reading["mean_dx"] > 0.0


def test_an_excluded_row_band_does_not_reach_the_reading():
    flat = np.full((200, 400), 100, np.float32)
    with_bars = flat.copy()
    with_bars[80:120, ::8] = 240

    assert sharpness.measure(with_bars[None], _lit(flat),
                             exclude_rows=(80, 120))["mean_dx"] == 0.0


def test_the_reading_is_taken_inside_the_lit_box_only():
    frame = np.zeros((200, 400), np.float32)
    frame[50:150, 100:300] = _grid(200, 100)

    assert (sharpness.measure(frame[None], (100, 299, 50, 149))["mean_dx"]
            > sharpness.measure(frame[None], _lit(frame))["mean_dx"])


def test_the_peak_reading_follows_the_sharpest_edges_not_the_average():
    """Most of a test card is flat, so the mean is mostly flat card while the
    sharpest few percent is where a softening of 1 px features would land."""
    mostly_flat = _grid(period=40, levels=(100, 240))

    reading = sharpness.measure(mostly_flat[None], _lit(mostly_flat))
    assert reading["p99_dx"] > 10 * reading["mean_dx"]


def test_a_clip_reads_as_sharp_as_its_frames_not_as_their_mean_frame():
    """A picture that shimmers has sharp frames and a blurred average, and the
    sharpness of the frames is the question -- so a clip of two offset grids
    must read like either of them rather than like their blur."""
    sharp = _grid()
    offset = np.roll(sharp, 1, axis=1)
    clip = np.stack([sharp, offset])

    reading = sharpness.measure(clip, _lit(sharp))["mean_dx"]
    blurred = sharpness.measure(clip.mean(axis=0)[None], _lit(sharp))["mean_dx"]
    assert reading > 1.5 * blurred
    assert abs(reading - sharpness.measure(sharp[None],
                                           _lit(sharp))["mean_dx"]) < 1.0
