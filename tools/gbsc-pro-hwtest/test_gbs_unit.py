"""gbs_unit's pure helpers, with no unit attached.

/geometry reports the engine's framing AND what it measured of the source, and
those grow independently. A helper that compares the whole body against a
framing breaks the moment a field is added to the report -- which is how 16
tests in test_geometry_pads.py came to error at once with the framing sitting
correctly at its default.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import DEFAULT_FRAMING, framing_of


def test_the_framing_is_projected_out_of_the_report():
    body = {"zh": 3, "zv": -2, "ph": 13, "pv": 0,
            "lineRateHz": 15575, "lowLineRate": True}

    assert framing_of(body) == {"zh": 3, "zv": -2, "ph": 13, "pv": 0}


def test_a_report_carrying_a_measurement_still_reads_as_the_default_framing():
    body = dict(DEFAULT_FRAMING, lineRateHz=15575, lowLineRate=True)

    assert framing_of(body) == DEFAULT_FRAMING


def test_nothing_read_is_not_a_framing():
    assert framing_of(None) is None
