"""gbs_unit's ModeServ helpers, against canned replies. No hardware."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit

LISTING = """OK MODES 4
X320 Y256 C256 F50
X320 Y256 C16 F50
X640 Y480 C256 F60
X640 Y480 C256 F60
X800 Y600 C256 F60
"""


def test_a_reply_is_ok_only_when_it_says_so():
    assert gbs_unit.modeserv_ok("OK ModeServ 12 PatLib 12")
    assert not gbs_unit.modeserv_ok("FAIL no such mode")
    assert not gbs_unit.modeserv_ok(None)
    assert not gbs_unit.modeserv_ok("")


def test_the_mode_listing_yields_each_256_colour_mode_once():
    assert gbs_unit.parse_modes(LISTING) == ["X320 Y256 C256 F50", "X640 Y480 C256 F60",
                                             "X800 Y600 C256 F60"]


def test_a_mode_the_machine_cannot_drive_is_never_offered():
    """A monitor definition may state a mode VIDC20 cannot clock. Asked for it
    the machine lands somewhere else entirely and the scaler then measures a
    raster nobody chose, so the listing every sweep is built from leaves it
    out."""
    listing = LISTING + "X1920 Y1080 C256 F50\nX1920 Y1080 C256 F60\n"
    assert gbs_unit.parse_modes(listing) == gbs_unit.parse_modes(LISTING)


def test_an_empty_listing_yields_no_modes():
    assert gbs_unit.parse_modes("") == []
    assert gbs_unit.parse_modes("OK MODES 0\n") == []
