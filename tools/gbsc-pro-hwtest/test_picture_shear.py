"""The arithmetic picture_shear.py scores a clip with.

No hardware: a synthetic clip with a KNOWN band displaced by a known amount is
the only way to know the score reports the shear it was given rather than a
plausible number. The bench cannot supply that -- the artefact is intermittent
and alternates on about one capture granule, so a bench clip carries an unknown
amount of it.
"""

import numpy as np

import picture_shear as shear

WIDTH, HEIGHT, BANDS, PROFILE = 480, 240, 8, 120


def card(width=WIDTH, lit=140.0):
    """One row of the card, with features at several scales across it so a
    displacement has something to correlate against."""
    across = np.arange(width)
    return (lit + 40.0 * np.sin(across * 0.05) + 25.0 * np.sin(across * 0.31)
            + 30.0 * np.sin(across * 0.011)).astype(np.float32)


def still(frames=8):
    """A clip whose picture never moves."""
    return np.tile(card(), (frames, HEIGHT, 1))


def rolled(clip, frame, by, rows=None):
    """`clip` with one frame's picture moved `by` columns, over `rows` only."""
    out = clip.copy()
    where = rows if rows is not None else slice(None)
    out[frame, where] = np.roll(out[frame, where], by, axis=-1)
    return out


def score(clip):
    return shear.measure_clip(clip, bands=BANDS, width=PROFILE)


def test_a_still_clip_carries_no_displacement_and_no_shear():
    got = score(still())
    assert got["pairs"] > 0
    assert got["displaced"] == 0.0 and got["rough"] == 0.0


def test_a_whole_picture_moved_together_is_a_pan_and_not_a_shear():
    """The reason a single profile per frame cannot be the instrument: every
    band displaced alike is the picture panning, which the engine does on every
    pad press, and the lock does not follow it."""
    got = score(rolled(still(), 3, 16))
    assert got["displaced"] > 0.0
    assert got["rough"] == 0.0


def test_a_band_displaced_on_its_own_is_the_shear():
    got = score(rolled(still(), 3, 16, rows=slice(HEIGHT // 2, None)))
    assert got["rough"] > 0.0


def test_a_frame_of_flat_colour_is_judged_on_nothing():
    """A flat band's correlation peak is noise, so a clip with nothing to
    correlate must report no pair rather than report perfectly still."""
    got = score(np.full((8, HEIGHT, WIDTH), 90.0, np.float32))
    assert got["pairs"] == 0


def test_a_capture_arrives_as_rgb_and_is_read_as_luma():
    """hdmi_capture.frames() answers (count, height, width, 3); a score that
    only took greyscale would fail on the one caller it has."""
    grey = still()
    rgb = np.repeat(grey[:, :, :, None], 3, axis=3).astype(np.uint8)
    assert score(rgb)["pairs"] == score(grey)["pairs"]
