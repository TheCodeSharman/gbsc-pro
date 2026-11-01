"""The arithmetic picture_match.py scores a frame with.

No hardware: a synthetic card, duplicated or sheared by a known amount, is the
only way to know the judge rejects the states it exists to reject rather than
returning a plausible number. A bench frame cannot supply that, because what is
wrong with it is the thing under test.

The duplicated case is the one that matters. Every measure that asks whether a
frame MOVES passes it, so a judge that cannot fail it is no better than the ones
it replaces.
"""

import numpy as np

import picture_match as pm


def card(width=1920, height=1080):
    """A frame with structure at several horizontal scales, as the card has."""
    across = np.arange(width)
    row = (120.0 + 50.0 * np.sin(across * 0.013) + 30.0 * np.sin(across * 0.071)
           + 40.0 * np.sin(across * 0.0031))
    down = 1.0 + 0.1 * np.sin(np.arange(height) * 0.02)
    return np.outer(down, row).astype(np.float32)


def duplicated(frame, copies=2):
    """The picture repeated across the line, which is what a counter dividing
    the real line by an exact factor emits."""
    width = frame.shape[1]
    one = frame[:, : width // copies]
    return np.tile(one, (1, copies))[:, :width]


def sheared(frame, per_row=3):
    """Every row displaced further than the last, stable frame to frame."""
    out = np.empty_like(frame)
    for y in range(frame.shape[0]):
        out[y] = np.roll(frame[y], (y * per_row) % frame.shape[1])
    return out


def magnified(frame, by):
    """The same content shown larger, as a capture narrower than the line gives."""
    width = frame.shape[1]
    x = np.arange(width) / by
    return np.stack([np.interp(x, np.arange(width), row) for row in frame])


def test_a_frame_matches_itself_at_unity_scale():
    ref = card()
    corr, scale, shift = pm.match(ref, ref)
    assert corr > 0.99
    assert abs(scale - 1.0) < 0.01
    assert shift == 0


def test_a_duplicated_frame_is_rejected():
    ref = card()
    corr, _, _ = pm.match(duplicated(ref), ref)
    assert corr < 0.9


def test_a_sheared_frame_is_rejected():
    ref = card()
    corr, _, _ = pm.match(sheared(ref), ref)
    assert corr < 0.9


def test_noise_is_rejected():
    ref = card()
    noise = np.random.default_rng(1).normal(120, 40, ref.shape).astype(np.float32)
    corr, _, _ = pm.match(noise, ref)
    assert corr < 0.5


def test_a_magnified_frame_matches_and_names_its_scale():
    ref = card()
    corr, scale, _ = pm.match(magnified(ref, 1.25), ref)
    assert corr > 0.9
    assert abs(scale - 1.25) < 0.03


def two_axis_card(width=640, height=480):
    """A card with real structure on BOTH axes, which `card()` has only across."""
    across = np.arange(width)
    down = np.arange(height)
    row = 50.0 * np.sin(across * 0.013) + 30.0 * np.sin(across * 0.071)
    col = 50.0 * np.sin(down * 0.021) + 30.0 * np.sin(down * 0.083)
    return (120.0 + np.add.outer(col, row)).astype(np.float32)


def stacked(frame, copies=2):
    """The picture repeated DOWN the frame, which is what a line counter
    wrapping more than once per source line emits."""
    height = frame.shape[0]
    one = frame[: height // copies]
    return np.tile(one, (copies, 1))[:height]


# A column-only metric reports a vertically duplicated frame as clean, and that
# is not hypothetical: the counter sets the output line rate, so the very fault
# a scaling ratio provokes is invisible across and plain down.
def test_a_vertically_duplicated_frame_is_rejected_down_and_not_across():
    ref = two_axis_card()
    doubled = stacked(ref)

    across, _, _ = pm.match(doubled, ref, axis="h")
    down, _, _ = pm.match(doubled, ref, axis="v")

    assert across > 0.99
    assert down < 0.9


def compressed(frame, by):
    """The content squeezed into the left `by` of the line, the rest left at the
    frame's own mean -- which is what a window placed over a minified line with
    nothing written past it shows."""
    width = frame.shape[1]
    kept = int(width * by)
    out = np.full_like(frame, frame.mean())
    x = np.arange(kept) / by
    out[:, :kept] = np.stack(
        [np.interp(x, np.arange(width), row) for row in frame])
    return out


# The search has to reach below the compression being looked for: at the old
# floor of 0.55 a working 3:1 reported as 0.55, which reads as no compression
# having happened.
def test_a_compression_past_the_old_floor_is_named_rather_than_clamped():
    ref = card()
    corr, scale, _ = pm.match(compressed(ref, 1.0 / 3.0), ref)

    assert corr > 0.9
    assert abs(scale - 1.0 / 3.0) < 0.03


def test_detail_falls_when_the_picture_is_filtered():
    ref = card()
    softened = np.stack([np.convolve(row, np.ones(5) / 5, "valid") for row in ref])
    assert pm.detail(softened) < pm.detail(ref[:, 2:-2])


def test_detail_is_blind_to_position():
    ref = card(width=2000)
    moved, held = ref[:, 37:1957], ref[:, :1920]
    assert abs(pm.detail(moved) - pm.detail(held)) < 0.02
