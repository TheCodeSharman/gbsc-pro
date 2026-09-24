"""The deinterlacer follows the source's scan type, both ways.

The fault this exists for came up green and comb-torn on **every ESP reset**: a
source wobbles by one line as it is acquired, which widened the count pair, and
a widened pair read as an alternating -- interlaced -- count for sixteen samples
while `Deinterlacer::FilteredPasses` needed two. Motion adapt engaged on a
progressive source and stayed engaged for the life of the boot.

Only the source can exercise both directions, which is why this needs
--modeserv: a unit judged against one scan type is judged against half of it.
"""
import time

import pytest

from gbs_unit import get_json, read_field, wait_for


# s2_00 carries the whole of what enable/disableMotionAdapt() decide, and the
# two write it to different values -- so one byte says which function ran.
# Deinterlacer::init() writes the same byte as disableMotionAdapt().
MOTION_ADAPT_OFF = 0xFF
MOTION_ADAPT_ON = 0x19

SETTLE_SECONDS = 30.0


def _motion_adapt(host):
    """s2_00, or None while the unit is not answering."""
    return read_field(host, 2, 0x00, 0, 8)


def _acquired(host):
    status, payload = get_json(host, "/geometry", timeout=3)
    return status == 200 and payload is not None and payload.get("state") == "acquired"


def _wait_for_motion_adapt(host, wanted):
    return wait_for(lambda: _motion_adapt(host) == wanted, timeout=SETTLE_SECONDS)


@pytest.mark.source_mode
def test_the_deinterlacer_follows_the_source_scan_type(host, modeserv):
    """Interlaced engages motion adapt, progressive releases it.

    Asserted on the register rather than the picture: a STATIC test card does
    not comb, so the panel is silent about this in both directions.
    """
    try:
        modeserv("INTERLACE ON")
        assert _wait_for_motion_adapt(host, MOTION_ADAPT_ON), (
            f"s2_00 is {_motion_adapt(host):#04x} on an interlaced source, "
            f"expected {MOTION_ADAPT_ON:#04x}. Motion adapt is what makes an "
            "interlaced picture legible"
        )

        modeserv("INTERLACE OFF")
        assert _wait_for_motion_adapt(host, MOTION_ADAPT_OFF), (
            f"s2_00 is {_motion_adapt(host):#04x} on a progressive source, "
            f"expected {MOTION_ADAPT_OFF:#04x}. Weaving a progressive picture "
            "is the green, comb-torn screen"
        )
    finally:
        modeserv("INTERLACE OFF")


@pytest.mark.reboot
@pytest.mark.source_mode
def test_a_reset_on_a_progressive_source_leaves_the_deinterlacer_alone(host, modeserv):
    """THE BOOT FAULT. A restart may not engage motion adapt on a progressive
    source -- it did on every one, and the picture came up green.

    Watched through acquisition and past it, because the engagement landed
    around eight seconds in, before the WebSocket server accepts a client.
    """
    modeserv("INTERLACE OFF")
    assert _wait_for_motion_adapt(host, MOTION_ADAPT_OFF), "not clean before the reset"

    get_json(host, "/restart", timeout=5)
    time.sleep(6)
    assert wait_for(lambda: _acquired(host), timeout=90.0), "the unit did not re-acquire"

    deadline = time.time() + 25.0
    while time.time() < deadline:
        seen = _motion_adapt(host)
        assert seen in (None, MOTION_ADAPT_OFF), (
            f"s2_00 read {seen:#04x} after a restart on a progressive source. "
            "Motion adapt engaged on its own, which is the green comb-torn boot"
        )
        time.sleep(0.4)
