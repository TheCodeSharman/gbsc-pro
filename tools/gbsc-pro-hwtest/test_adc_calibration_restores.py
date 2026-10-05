"""The ADC's gain-measurement path is off unless the auto-gain feature asked.

`calibrateAdcOffset()` borrows the chip at boot: it switches `DEC_TEST_ENABLE`
on, drives the decimator test output through the test bus to find each
channel's offset, and holds the result. Of everything it borrows, that one bit
is the only field whose only other writer is `applyStoredAdcGain()`, inside the
preset path -- so when `applyPresets()` goes, nothing would switch it back and
the ADC would run its measurement path for the life of the boot.

Measured 2026-10-05 on the bench unit, settled dumps either side of
`calibrate-adc`: the two boots differ in `PA_SP_S`, `VDS_HSYNC_RST`,
`VDS_HB_ST`, `VDS_DIS_HB_ST` and `VDS_HSCALE` -- the searched sampling phase
and the raster solved from a measured field rate -- and in nothing the
calibration touches. Every other borrowed field has an owner that runs without
a preset load.

**THIS PASSES AGAINST THE PRESET PATH TOO**, which is the point: it states the
invariant that path currently supplies, so removing it cannot take the
invariant with it silently.

docs/investigations/what-the-adc-calibration-borrows.md
"""

from gbs_unit import get_json, read_named, setting, wait_for

# Long enough for a boot to acquire and present; a selection on this bench is
# seconds and a cold ypbpr boot is about thirty.
SETTLE_SECONDS = 60.0


def _acquired(host):
    status, payload = get_json(host, "/geometry", timeout=3)
    return status == 200 and payload is not None and payload.get("state") == "acquired"


def test_the_gain_measurement_follows_the_auto_gain_preference(host):
    # Waited for rather than skipped on: a verdict taken off an unsolved chip
    # reads green and is not.
    assert wait_for(lambda: _acquired(host), timeout=SETTLE_SECONDS)

    auto_gain = setting(host, "auto-gain")
    assert auto_gain is not None, "the unit's preferences carry no auto-gain key"

    wanted = 1 if str(auto_gain).strip() == "1" else 0
    assert read_named(host, "DEC_TEST_ENABLE") == wanted
