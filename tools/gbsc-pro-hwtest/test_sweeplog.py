"""sweeplog's segment-0 decode, against synthetic registers. No hardware.

The sweep exists to catch a register latching a stable WRONG value, so the
decode itself has to be beyond suspicion: a mask that lets a neighbouring
nibble through produces a number that is stable, confident and wrong, which is
the one failure every health check in this project scores as healthy.

The two twelve-bit fields both share their high register with something else --
HTOTAL's top nibble sits in 0x18 and HLOW_LEN's in 0x1A -- so each case below
puts junk in the nibble that must be masked off.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sweeplog

# The bench unit in RGBHV bypass against the 800x600 source, 2026-08-08:
# HPERIOD_IF 177, SP_VTOTAL 627, HLOW_LEN 226 of HTOTAL 1856.
HPERIOD, VTOTAL, HLOW_LEN, HTOTAL = 177, 627, 226, 1856


def a_segment_zero(hperiod=HPERIOD, vtotal=VTOTAL,
                   hlow=HLOW_LEN, htotal=HTOTAL, junk=0xF0):
    """256 bytes of segment 0 with the measured quantities in place, and `junk`
    in every high nibble the decode is supposed to ignore."""
    s0 = bytearray(256)
    s0[6:10] = (hperiod & 0x1FF).to_bytes(4, "little")
    s0[0x17] = htotal & 0xFF
    s0[0x18] = junk | ((htotal >> 8) & 0x0F)
    s0[0x19] = hlow & 0xFF
    s0[0x1A] = junk | ((hlow >> 8) & 0x0F)
    s0[0x1B:0x1D] = (vtotal & 0x7FF).to_bytes(2, "little")
    return bytes(s0)


def test_the_sync_low_time_is_decoded_from_its_twelve_bits():
    assert sweeplog.decode(a_segment_zero())["hlow"] == HLOW_LEN


def test_the_line_total_is_decoded_from_its_twelve_bits():
    assert sweeplog.decode(a_segment_zero())["htotal"] == HTOTAL


def test_a_full_width_sync_low_survives_the_mask():
    """0xFFF is a legal reading, so the mask must not be mistaken for a range
    check -- HLOW_LEN railing high is one of the states worth catching."""
    assert sweeplog.decode(a_segment_zero(hlow=0xFFF))["hlow"] == 0xFFF


def test_the_line_period_still_decodes():
    assert sweeplog.decode(a_segment_zero())["hperiod"] == HPERIOD


def test_the_frame_total_still_decodes():
    assert sweeplog.decode(a_segment_zero())["vtotal"] == VTOTAL
