"""HD bypass claims the HD bypass DAC route, and keeps it.

`runSyncWatcher()` steers an RGBHV source between scaling and RGBHV bypass on
`rto->videoStandardInput` alone, and 14 and 15 name the OUTPUT as much as the
source -- so a source switched to HD bypass reads as one of them, the steering
decides it is stranded in the wrong bypass, and `applyPresets()` puts the DACs
back on the scaler a second or two later.

**A register dump taken at the switch reads correct**, because the switch does
claim the route and only the next pass of the loop takes it away -- so the fault
is visible only to a reader that keeps looking.

    pytest test_hd_bypass.py --host=<ip> --preset-save -v

`--preset-save` because `/sc?K` persists `presetPreference`; this puts it back.
It leaves the unit on the scaling path.
"""

import time

import pytest

from gbs_unit import fs_read, get, read_fields, restore_preset_preference

# How long to wait for the switch to write the route at all. The switch resets
# blocks and reloads the chip, so it is not instant.
CLAIM_TIMEOUT = 12.0

# How long the route has to survive afterwards. The steering that used to clear
# it lands about two seconds in; ten covers a source that settles slowly and so
# defers the pass that carries it.
HOLD_SECONDS = 10.0

SAMPLE_INTERVAL = 0.5


def route_bits(host):
    return read_fields(host, ["DAC_RGBS_BYPS2DAC", "DAC_RGBS_ADC2DAC"])


def wait_for_claim(host):
    """The switch has written the HD bypass route. Returns when it has."""
    deadline = time.monotonic() + CLAIM_TIMEOUT
    while time.monotonic() < deadline:
        if route_bits(host)["DAC_RGBS_BYPS2DAC"] == 1:
            return
        time.sleep(SAMPLE_INTERVAL)
    pytest.fail(
        f"DAC_RGBS_BYPS2DAC never reached 1 within {CLAIM_TIMEOUT:.0f}s of /sc?K, "
        "so the HD bypass switch did not claim the route at all"
    )


def test_hd_bypass_keeps_the_route_it_claimed(host, preset_save):
    original = fs_read(host, "/preferencesv2.txt")[:1]

    try:
        status, body = get(host, "/sc?K")
        assert status == 200, f"/sc?K answered {status}: {body}"

        wait_for_claim(host)

        # It is claimed. Now it has to STAY claimed: the bug writes it and takes
        # it away, so a single read after the switch cannot see the difference.
        dropped_at = None
        started = time.monotonic()
        while time.monotonic() - started < HOLD_SECONDS:
            if route_bits(host)["DAC_RGBS_BYPS2DAC"] != 1:
                dropped_at = time.monotonic() - started
                break
            time.sleep(SAMPLE_INTERVAL)

        assert dropped_at is None, (
            f"DAC_RGBS_BYPS2DAC went back to 0 {dropped_at:.1f}s after the HD "
            "bypass switch claimed it -- the RGBHV steering read the switch as a "
            "source stranded in RGBHV bypass and reloaded a scaled preset"
        )
    finally:
        get(host, "/sc?~")
        restore_preset_preference(host, original)
