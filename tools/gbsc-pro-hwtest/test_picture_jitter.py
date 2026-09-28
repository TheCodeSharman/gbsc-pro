"""The arithmetic picture_jitter.py measures a clip with.

No hardware: a synthetic profile shifted by a known amount is the only way to
know the estimator reports the shift it was given rather than a plausible
number.
"""

import numpy as np
import pytest

from picture_jitter import band_starts, displacement, gain_for


def profile(shift=0.0, length=400):
    """A profile with edges at several scales, sampled `shift` to the right."""
    x = np.arange(length) - shift
    return (np.sin(x * 0.11) + 0.5 * np.sin(x * 0.37) + np.sin(x * 0.03))


def edges(shift=0.0, length=400):
    """Step edges, which a test card presents and sinusoids do not. Its
    correlation peak is a different shape, so it reads at a different gain."""
    x = np.arange(length) - shift
    value = np.zeros(length)
    for at in (60, 140, 150, 160, 240, 250, 300):
        value += 1.0 / (1.0 + np.exp(-(x - at) * 2.0)) * (1 if at % 20 else -1)
    return value


def measured(content, shift):
    raw, _ = displacement(content(0.0), content(shift))
    return raw / gain_for(content(0.0))


def test_an_unshifted_profile_reports_no_movement():
    assert displacement(profile(), profile())[0] == pytest.approx(0.0, abs=0.01)


def test_a_sub_column_shift_is_resolved():
    assert measured(profile, 0.4) == pytest.approx(0.4, abs=0.03)


def test_the_same_shift_reads_the_same_on_different_content():
    """The raw estimator's gain is the content's spectrum -- 1.24 on sinusoids
    against 0.435 on step edges -- so two framings of one source are comparable
    only once each is divided by its own."""
    assert measured(edges, 0.4) == pytest.approx(measured(profile, 0.4), abs=0.05)


def test_a_shift_is_reported_with_its_sign():
    assert measured(profile, -0.4) == pytest.approx(-0.4, abs=0.03)


def test_a_shift_past_the_search_is_not_bracketed():
    """An unbracketed peak is the estimator running out of search, which reads
    as a clean small movement if the caller does not ask."""
    _, bracketed = displacement(profile(0.0), profile(9.0), maxlag=3)
    assert bracketed is False


def test_the_bands_span_the_picture_without_leaving_it():
    starts = band_starts(100, 900, bands=4, span=100)
    assert starts[0] == 100
    assert starts[-1] + 100 == 900
    assert starts == sorted(starts)


def test_one_band_starts_at_the_top():
    assert band_starts(40, 500, bands=1, span=100) == [40]
