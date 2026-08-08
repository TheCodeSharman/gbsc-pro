"""analyse_sweep's sync-width summary, against synthetic samples. No hardware.

This is the measurement that decides whether the hsync duty can carry a third
term of the preset key. Statically it looks strong -- two tight clusters over 33
committed snapshots, stable across PLLAD_MD 1856..3072 -- but every one of those
is a settled state, and HPERIOD_IF's failure rates are known precisely because
somebody swept it rather than snapshotting it.

So the sharp case here is the committed sweeps, which predate the field
entirely. A summary that reported "spread 0" for them would be read as the
stability this is trying to establish, from runs that never measured it.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyse_sweep

HTOTAL = 1856


def settled(widths, htotal=HTOTAL):
    return [{"hlow": w, "htotal": htotal} for w in widths]


def test_a_sweep_predating_the_field_reports_no_sync_width():
    """The committed sweeps have no hlow at all. None, never a spread of 0."""
    assert analyse_sweep.sync_width([{"hperiod": 431, "vtotal": 311}]) is None


def test_the_spread_is_the_range_of_the_settled_readings():
    assert analyse_sweep.sync_width(settled([226, 227, 226, 227]))["spread"] == 1


def test_the_commonest_width_is_reported_not_the_mean():
    """A dropped read is one sample, and a mean would smear it across the
    answer. The key matches on a bucket, so the mode is what it wants."""
    assert analyse_sweep.sync_width(settled([226, 226, 226, 998]))["hlow"] == 226


def test_the_duty_is_the_width_over_the_line_plus_one():
    """The sync processor's counters are zero-based, so HTOTAL is one short --
    the firmware already does this +1 in ratioHs."""
    assert analyse_sweep.sync_width(settled([226]))["duty"] == 226 / 1857


def test_a_partial_record_is_not_a_measurement():
    """hlow without htotal cannot give a duty, and half an answer here is
    worse than none -- the ratio is the whole point of the quantity."""
    assert analyse_sweep.sync_width([{"hlow": 226}]) is None
