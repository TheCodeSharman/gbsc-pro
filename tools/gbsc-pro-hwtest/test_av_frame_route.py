"""The AV module frame probe.

`ASW_01`-`ASW_04` decide what is actually connected to the ADC input the scaler
has selected, they live on the HC32, and they appear in no register dump. Every
other route that reaches them -- `/input`, the OLED, the IR handler -- moves the
scaler in the same breath, so nothing on the board can ask what the HC32 alone
accounts for.

`/avframe` sends the 7-byte frame and nothing else. The property that makes it
an instrument is the one asserted here: the scaler does not move.
"""

import pytest

from gbs_unit import get, read_named, setting


INPUT_NAMES = ("rgbs", "rgsb", "vga", "ypbpr", "sv", "av")

# ADC_INPUT_SEL for each name. The RGB pins carry the first three and the YUV
# pins the last three, so a frame sent for the other group is what would move
# the register if the route touched it.
RGB_PIN_NAMES = ("rgbs", "rgsb", "vga")


def test_the_av_frame_route_refuses_what_it_does_not_recognise(host):
    """A bad or missing src is a 400, never a frame.

    The frame reaches a part that cannot be read back, so a name nobody
    recognises must not be guessed at -- a wrong one re-routes the analog
    switches with nothing on the board able to report it.

    Safe without --source because nothing is sent on this path.
    """
    status, body = get(host, "/avframe")
    assert status == 400, f"missing src returned {status}, body {body!r}"

    for bad in ("component", "YPBPR", "", "1", "0x70"):
        status, body = get(host, f"/avframe?src={bad}")
        assert status == 400, (
            f"src={bad!r} returned {status} rather than refusing it: {body!r}")


def test_the_av_frame_route_names_the_frame_it_queued(host):
    """A 200 carries the byte, so a capture records what was sent.

    The reply is the only record there is: the HC32 acknowledges nothing, and
    `ESP_RXD` is not wired to it, so no readback exists on this board.

    Sending the input already in use asks the HC32 for the routing it is already
    holding, which is why this needs no opt-in.
    """
    chosen = setting(host, "input")
    if chosen not in INPUT_NAMES:
        pytest.skip(f"no input stored to send safely: {chosen!r}")

    status, body = get(host, f"/avframe?src={chosen}")
    assert status == 200, f"{chosen} was refused: {status} {body!r}"
    assert chosen in body, f"the reply does not name what it queued: {body!r}"
    assert "frame" in body, f"the reply does not carry the byte: {body!r}"


def test_the_av_frame_route_leaves_the_scaler_alone(host, source):
    """The frame goes to the HC32 and `ADC_INPUT_SEL` does not move.

    This is what separates the probe from `/input`, which selects the ADC mux,
    forces a re-detection and saves the choice. A frame for the OTHER pin group
    is the discriminating case: were the route to touch the scaler at all, that
    register would follow it.

    The frame is put back before returning, so the bench is left routed the way
    it was found.
    """
    chosen = setting(host, "input")
    if chosen not in INPUT_NAMES:
        pytest.skip(f"no input stored to restore afterwards: {chosen!r}")

    other = "ypbpr" if chosen in RGB_PIN_NAMES else "vga"
    before = read_named(host, "ADC_INPUT_SEL")

    try:
        status, body = get(host, f"/avframe?src={other}")
        assert status == 200, f"{other} was refused: {status} {body!r}"

        after = read_named(host, "ADC_INPUT_SEL")
        assert after == before, (
            f"ADC_INPUT_SEL moved {before} -> {after} on an /avframe for "
            f"{other}: the route is not confined to the AV module")
    finally:
        get(host, f"/avframe?src={chosen}")
