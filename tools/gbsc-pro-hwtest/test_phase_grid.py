"""What the phase grid refuses to average and refuses to conclude."""

import numpy as np
import pytest

import phase_grid


def clip(*brightness):
    """A clip of frames, each a flat RGB field at the given level."""
    return np.stack([np.full((8, 8, 3), level, np.uint8) for level in brightness])


@pytest.fixture
def clips(monkeypatch):
    queued = []
    monkeypatch.setattr(phase_grid, "frames",
                        lambda *a, **k: queued.pop(0))
    monkeypatch.setattr(phase_grid, "lit_box",
                        lambda grey: None if grey.mean() < 24 else (0, 7, 0, 7))
    monkeypatch.setattr(phase_grid.time, "sleep", lambda _: None)
    return queued


def test_a_clip_is_retaken_while_any_frame_is_black(clips):
    clips.append(clip(0, 200, 200))
    clips.append(clip(200, 200, 200))

    greys, retries = phase_grid.lit_clip(dev=None)

    assert retries == 1 and greys.min() > 24


def reading(contrast):
    return (contrast, 0.0, 627, 0, 0)


def grid(**rows):
    return {(int(sp.lstrip("sp")), adc): [reading(contrast)]
            for sp, row in rows.items()
            for adc, contrast in zip((0, 4), row)}


def test_the_peak_comparison_is_stated_for_a_run_the_console_certified():
    lines = phase_grid.report(grid(sp0=(30.0, 31.0), sp4=(41.0, 40.0)),
                              phases=(0, 4), syncPhases=(0, 4), quiet=True)

    assert "reachable peak by PA_SP_S: 0:31.0, 4:41.0" in "\n".join(lines)


def test_a_run_that_crossed_a_solve_states_no_peak_comparison():
    lines = phase_grid.report(grid(sp0=(30.0, 31.0), sp4=(41.0, 40.0)),
                              phases=(0, 4), syncPhases=(0, 4), quiet=False)

    assert "reachable peak" not in "\n".join(lines)
