"""The arithmetic frame_cells.py measures a clip with.

No hardware: synthetic frames whose disturbance is known are the only way to
know a reading localises to the cell that moved rather than reporting a
plausible number over the whole picture.

The tests that matter here are the ones where ONE cell moves and the rest are
still. A band spanning the picture averages such a cell away, which is the
whole reason this instrument exists.
"""

import numpy as np
import pytest

from frame_cells import breathing, cell_grid, measure, moving

SIZE = 64
SHAPE = (192, 320)
PATCH = (64, 64)


def grating(width, height, period=5.0, shift=0.0, amplitude=40.0, level=128.0):
    """A patch of fine vertical detail, sampled `shift` columns to the right."""
    x = np.arange(width) - shift
    row = amplitude * (np.sin(2 * np.pi * x / period) + 0.4 * np.sin(2 * np.pi * x / 13.0))
    return np.tile(level + row, (height, 1))


def background(shape=SHAPE):
    """Static detail everywhere, so a still cell is measurable rather than
    merely empty -- a still cell that cannot be measured proves nothing about
    whether the moving one was singled out."""
    y, x = np.indices(shape)
    return 120.0 + 30.0 * np.sin(x / 7.0) + 20.0 * np.sin(y / 9.0)


def clip(patches, at=PATCH, size=SIZE, shape=SHAPE, frames=None):
    """One frame per patch, each laid over the static background."""
    built = []
    for patch in patches:
        frame = background(shape)
        if patch is not None:
            frame[at[1]:at[1] + size, at[0]:at[0] + size] = patch
        built.append(frame)
    return np.asarray(built, np.float32)


def alternating(shifts, size=SIZE):
    return [grating(size, size, shift=s) for s in shifts]


def cell_at(result, x, y):
    for cell in result["cells"]:
        if cell["x"] == x and cell["y"] == y:
            return cell
    raise AssertionError(f"no cell at {x},{y}")


def test_the_grid_covers_the_box_and_ends_on_its_far_edge():
    grid = cell_grid((0, 320, 0, 192), size=64)
    assert (0, 0) in grid
    assert max(x for x, _ in grid) + 64 == 320
    assert max(y for _, y in grid) + 64 == 192


def test_a_flat_cell_is_not_measurable():
    """A cell with no horizontal detail cannot report a displacement, and its
    noise must not be rankable against a cell that can."""
    frames = np.full((8,) + SHAPE, 128.0, np.float32)
    result = measure(frames, size=SIZE, box=(0, 320, 0, 192))
    assert not any(cell["measurable"] for cell in result["cells"])


def test_a_cell_that_moves_reports_the_shift_it_was_given():
    """Alternating 0 and 0.4 columns has a spread of 0.2 columns."""
    frames = clip(alternating([0.0, 0.4] * 6))
    cell = cell_at(measure(frames, size=SIZE, box=(0, 320, 0, 192)), *PATCH)
    assert cell["move_sd"] == pytest.approx(0.2, abs=0.04)


def test_only_the_moving_cell_is_reported_as_moving():
    frames = clip(alternating([0.0, 0.4] * 6))
    result = measure(frames, size=SIZE, box=(0, 320, 0, 192))
    assert [(c["x"], c["y"]) for c in moving(result["cells"])] == [PATCH]


def test_a_cell_whose_contrast_breathes_without_moving_is_reported():
    """A sub-pixel disturbance on detail near the sampling limit modulates
    contrast rather than displacing it, so displacement alone is blind to it."""
    patches = [grating(SIZE, SIZE, amplitude=a) for a in [40.0, 28.0] * 6]
    result = measure(clip(patches), size=SIZE, box=(0, 320, 0, 192))
    assert [(c["x"], c["y"]) for c in breathing(result["cells"])] == [PATCH]


def test_a_gain_change_over_the_whole_frame_singles_out_no_cell():
    """The dongle's own gain moves every cell's contrast together, so a cell is
    only breathing relative to the rest of the picture."""
    frames = clip(alternating([0.0] * 12))
    frames = frames * np.array([1.0, 0.95] * 6, np.float32)[:, None, None]
    result = measure(frames, size=SIZE, box=(0, 320, 0, 192))
    assert breathing(result["cells"]) == []


def test_a_noisy_flat_cell_does_not_outrank_a_real_disturbance():
    """A flat cell's spatial spread IS its noise, so the ratio of the two runs
    to tens of per cent on a picture that is not moving at all -- measured on
    the dongle at 0.15..0.28 over the test card's white circle against 0.095 at
    the grating that was genuinely shimmering. Ranked on the ratio alone the
    still part of the picture wins."""
    noise = np.random.default_rng(7).normal(0.0, 0.5, (12,) + SHAPE)
    patches = [grating(SIZE, SIZE, amplitude=a) for a in [40.0, 28.0] * 6]
    frames = clip(patches) + noise
    frames[:, 0:SIZE, 0:SIZE] = 200.0 + noise[:, 0:SIZE, 0:SIZE]
    result = measure(frames, size=SIZE, box=(0, 320, 0, 192))
    assert [(c["x"], c["y"]) for c in breathing(result["cells"])] == [PATCH]


def test_a_cells_contrast_spread_is_reported_in_grey_levels_too():
    """The fraction is what the eye reads, and it divides by a contrast that
    can itself collapse -- so the absolute spread travels beside it, or a
    washed-out cell reads as a worse one."""
    patches = [grating(SIZE, SIZE, amplitude=a) for a in [40.0, 28.0] * 6]
    cell = cell_at(measure(clip(patches), size=SIZE, box=(0, 320, 0, 192)), *PATCH)
    assert cell["cstd"] == pytest.approx(cell["cmod"] * cell["contrast"], rel=1e-6)
