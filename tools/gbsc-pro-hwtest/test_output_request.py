"""An output resolution request lands whatever the RGBHV scaling flag reads.

The request handler used to divert a resolution letter to the bypass choice
whenever scalingRgbhv() read true, and applied nothing; a source mode change
leaves the flag reading true, so the first request after one was saved and
not applied, and only the next one landed. docs/known-issues.md
"""

import time

import pytest

from gbs_unit import (OUTPUT_COMMANDS, OUTPUT_FRAME_LINES, acquired_and_settled,
                      choose_output, get, handed_over, read_named, wait_for,
                      want_route)

# A mode change takes seconds to reach the engine, and `state` is stale across
# one -- so a settle check taken straight after the command answers about the
# source that just left.
SOURCE_SETTLE_SECONDS = 12.0

BENCH_MODE = "X320 Y256 C256 F50"
OTHER_MODE = "X640 Y480 C256 F60"


def one_request(host, output, attempts=3):
    """One resolution request. Sent again only where the unit gave no answer
    at all, since a request with no answer never reached the handler."""
    for _ in range(attempts):
        status = get(host, f"/uc?{OUTPUT_COMMANDS[output]}")[0]
        if status != 0:
            return status
        time.sleep(2.0)
    return 0


def raster_carries(host, output, timeout=30.0):
    lines = OUTPUT_FRAME_LINES[output] - 1
    return wait_for(lambda: read_named(host, "VDS_VSYNC_RST") == lines, timeout=timeout)


@pytest.mark.source_mode
def test_one_output_request_lands_after_a_source_mode_change(host, source, preset_save, modeserv):
    modeserv(f"MODE {OTHER_MODE}")
    time.sleep(SOURCE_SETTLE_SECONDS)
    assert wait_for(lambda: acquired_and_settled(host), timeout=60.0), \
        f"{OTHER_MODE} never acquired"

    # **ON THE SCALING ROUTE, where a raster exists to carry the request.**
    # 525 lines at 60 Hz suits pass-through and a source with nothing stored
    # takes it, and `VideoSourceAcquisition::setOutputResolution()` returns
    # early there by design -- the resolution is where the output returns when
    # the source stops qualifying, so the VDS raster is not rewritten and the
    # request reads as refused. docs/rgbhv-bypass-trap.md
    scaled = want_route(host, False, tries=4, settle=SOURCE_SETTLE_SECONDS)
    assert scaled, f"{OTHER_MODE} would not scale, so no raster carries a request: {scaled}"

    before = read_named(host, "VDS_VSYNC_RST")
    other = "720p" if before != OUTPUT_FRAME_LINES["720p"] - 1 else "1080p"
    try:
        assert one_request(host, other) == 200
        assert raster_carries(host, other), (
            f"{other} was asked for once after a source mode change and the raster "
            f"stayed on {read_named(host, 'VDS_VSYNC_RST') + 1} lines")
    finally:
        choose_output(host, "1080p")
        modeserv(f"MODE {BENCH_MODE}")
        modeserv("PATTERN CARD")
        modeserv("ANIM OFF")


@pytest.mark.source_mode
def test_a_resolution_request_leaves_passthrough(host, source, preset_save, modeserv):
    """Choosing an output resolution takes a passed-through source off bypass.

    A resolution is a command to scale, so one request must leave pass-through.
    The scaled route is stored through the acquisition, which re-answers the
    route and drops the chip off bypass -- storing it on the VideoPath alone
    does not, because nothing re-reads a stored route while the source stands
    still. The path in force is read off DAC_RGBS_BYPS2DAC/OUT_SYNC_SEL, not off
    the preference /geometry reports wanting. docs/rgbhv-bypass-trap.md

    This is also what keeps the menu's pass-through state in step with its
    toggle: while the chip passed through on a scaled route, the row read ON off
    the chip and the toggle read OFF off the route, so turning it off took two
    presses.
    """
    modeserv(f"MODE {OTHER_MODE}")
    time.sleep(SOURCE_SETTLE_SECONDS)
    assert wait_for(lambda: acquired_and_settled(host), timeout=60.0), \
        f"{OTHER_MODE} never acquired"

    # PUT it in pass-through and confirm the PATH carries it, not just the wish.
    assert want_route(host, True), f"{OTHER_MODE} would not pass through"
    assert wait_for(lambda: handed_over(host), timeout=30.0), \
        "pass-through was asked for but the path never carried it"

    try:
        # choose_output waits for the mode's own raster, which a chip left in
        # bypass never carries -- so the bug fails this on both its counts: the
        # resolution does not land and the path stays bypassed.
        assert choose_output(host, "1080p"), \
            "1080p was chosen once from pass-through and its raster never came"
        assert wait_for(lambda: handed_over(host) is False, timeout=15.0), \
            "the output stayed on the bypass path after a resolution was chosen"
    finally:
        choose_output(host, "1080p")
        modeserv(f"MODE {BENCH_MODE}")
        modeserv("PATTERN CARD")
        modeserv("ANIM OFF")
