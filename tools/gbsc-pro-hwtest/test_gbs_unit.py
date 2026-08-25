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
from gbs_unit import framing_of


def test_the_framing_is_projected_out_of_the_report():
    body = {"oh": 217, "eh": 590, "ov": 91, "ev": 412,
            "ch": 1033, "cv": 622,
            "poh": 2101, "peh": 5711, "pov": 1463, "pev": 6624,
            "lineRateHz": 15575, "lowLineRate": True}

    assert framing_of(body) == {"oh": 217, "eh": 590, "ov": 91, "ev": 412}


def test_the_capturable_region_is_not_part_of_the_framing():
    """`ch` and `cv` are the denominator the units are taken against, so they
    move with the source while the framing has not been touched. Projecting
    them would read a mode change as the user having reframed."""
    narrower = {"oh": 217, "eh": 590, "ov": 91, "ev": 412, "ch": 890, "cv": 500}
    wider = dict(narrower, ch=1033, cv=622)

    assert framing_of(narrower) == framing_of(wider)


def test_nothing_read_is_not_a_framing():
    assert framing_of(None) is None
