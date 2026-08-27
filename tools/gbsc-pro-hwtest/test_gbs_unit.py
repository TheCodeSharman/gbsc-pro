"""gbs_unit's pure helpers, with no unit attached.

/geometry reports the engine's framing AND what it measured of the source, and
those grow independently. A helper that compares the whole body against a
framing breaks the moment a field is added to the report -- which is how 16
tests in test_geometry_pads.py came to error at once with the framing sitting
correctly at its default.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit
from gbs_unit import (field_from_named, field_spec, framing_of, read_named)


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


def test_a_field_is_read_by_name(monkeypatch):
    """The whole point: a wrong segment/register/offset/width does not error, it
    returns a plausible number. A wrong NAME cannot be read at all."""
    seen = {}

    def fake(host, path, timeout=5):
        seen["path"] = path
        return 200, {"values": [311]}

    monkeypatch.setattr(gbs_unit, "get_json", fake)
    assert read_named("unit", "STATUS_SYNC_PROC_VTOTAL") == 311

    field = gbs_unit.catalogue()["STATUS_SYNC_PROC_VTOTAL"]
    assert "{seg}.{reg}.{off}.{width}".format(**field) in seen["path"]


def test_a_name_that_is_not_a_field_raises(monkeypatch):
    """Loudly, and before any request goes out -- a typo must not read as a
    value the way an invented address does."""
    monkeypatch.setattr(gbs_unit, "get_json",
                        lambda *a, **k: pytest.fail("should not have asked the unit"))
    with pytest.raises(KeyError):
        read_named("unit", "SP_VTOTAL")


def test_a_read_that_did_not_arrive_is_not_a_value(monkeypatch):
    monkeypatch.setattr(gbs_unit, "get_json", lambda *a, **k: (200, None))
    assert read_named("unit", "PLLAD_MD") is None


def test_a_spec_comes_from_the_catalogue_rather_than_the_keyboard():
    """For the scripts that keep a (seg, reg, off, width) interface of their
    own -- bench_probe writes through one -- so the tuple is looked up rather
    than typed."""
    assert field_spec("PLLAD_MD") == (5, 0x12, 0, 12)
    assert field_spec("STATUS_SYNC_PROC_VTOTAL") == (0, 0x1B, 0, 11)


def test_a_spec_for_something_that_is_not_a_field_raises():
    with pytest.raises(KeyError):
        field_spec("PLLAD_MODULUS")


def test_a_field_is_decoded_out_of_a_segment_dump_by_name():
    """read_segment() gives one segment's bytes; this picks a field out of them
    without the caller writing the slice. HPERIOD_IF is s0 0x06, and a hand
    written 0x07 for it read VPERIOD_IF's bits instead."""
    segment0 = {0x06: 0xAF, 0x07: 0x01}
    assert field_from_named(segment0, "HPERIOD_IF") == 431


def test_a_byte_missing_from_the_dump_is_not_a_value():
    assert field_from_named({0x06: 0xAF}, "HPERIOD_IF") is None
