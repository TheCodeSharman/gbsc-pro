"""The engine must CHOOSE the ADC's sampling phase, not guess it.

`PA_ADC_S` is where in the ADC clock the converter takes its sample.
`Adc::acquirePhase()` writes the middle of its five-bit field, which is not a
sampling instant, and nothing searches it. So on a source sampled below two
ADC samples per pixel the finest grating lands anywhere between clear and
glitching, re-rolling per acquisition with every register the same --
`docs/investigations/the-adc-sampling-phase-is-a-lottery.md`.

**WHAT IS ASSERTED IS THE SHARE OF THE GRATING THAT CHANGES BETWEEN FRAMES,
AND THE OTHER TWO CANDIDATES BOTH FAIL.**

A static source sampled at a static instant must give an identical frame every
time. It does not, and which quantity to measure that by is not obvious:

- **Contrast, absolutely.** Decided by sampling density before the phase gets a
  say -- 77.6..81.6 grey levels on 640x480@60 against 6.4..10.5 on
  1280x1024@60, at one divider. A floor meaning "clear" on the first mode is
  unreachable on the last whatever the phase does.
- **Change between frames, absolutely.** The change is jitter, and jitter
  barely moves with the phase: 7.7 grey levels at the clear phase against 10.0
  at the guessed one, a 1.3x window on a picture whose difference is obvious
  across a room.
- **The SHARE of the grating that changes.** The same two phases separate
  **3.7x** here, because the jitter is constant while the signal it rides on is
  not. A sample landing mid-pixel gives maximum contrast and is insensitive to
  jitter; one landing on a pixel edge gives minimum contrast and swings between
  two pixel values. That ratio is what the eye integrates.

**AND IT IS THE ONE WITH A BAR TAKEN FROM THE PICTURE RATHER THAN FROM A
MEASUREMENT.** The two readings below were graded by eye at the panel, which
is why a single absolute number works here and no per-mode table or field walk
is needed.

Captured at the dongle's native 1920x1080 whatever the source emits: asking
for the source's own size gets a resampled frame and the resampler's filtering
then answers the question.
"""

import time
import urllib.request

import numpy as np
import pytest

import wedge
from gbs_unit import read_fields, wait_for
from hdmi_capture import BLACK, device, frames, luma

pytestmark = pytest.mark.source_mode

WIDTH, HEIGHT = 1920, 1080
FRAMES = 24
WARMUP = 8

# /phase is deferred to loop() like every command route.
SETTLE_S = 1.0

# The share of the fine grating that may change between frames.
#
# **GRADED BY EYE AT THE PANEL**, at 1280x720@60 on `vga` in pass-through,
# divider 2038, PM5544 with ANIM OFF, one acquisition:
#
#   0.154   PA_ADC_S 2, found by hand    "perfectly clear" -- subpixel movement
#                                        visible only from very close
#   0.567   PA_ADC_S 16, the engine's    "high frequency glitching", obvious
#
# Every mode measured at its own phase, for the headroom this leaves:
# 640x480@60 0.049..0.090, 800x600@60 0.042, 1024x768@60 0.084,
# 1280x1024@60 0.281..0.314. The last is close to this bar and cannot be
# improved by choosing better -- its field swings 1.09x, so there is nothing
# to choose and the reading is sampling density. Lower this bar and that mode
# fails for a reason no phase search can fix.
MOVING_SHARE_CEILING = 0.35

MODES = [
    ("640x480@60", "MODE X640 Y480 C256 F60", 480),
    ("800x600@60", "MODE X800 Y600 C256 F60", 600),
    ("1024x768@60", "MODE X1024 Y768 C256 F60", 768),
    ("1280x720@60", "MODE X1280 Y720 C256 F60", 720),
    ("1280x1024@60", "MODE X1280 Y1024 C256 F60", 1024),
]


def _route(host, path):
    return urllib.request.urlopen(f"http://{host}{path}", timeout=8).read().decode()


def _lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), (
        "the capture is entirely black. Ask the board before the connector: "
        "s0_46's resets, then s0_45 and s0_49. hdmi_capture.py")
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def _settle(host, modeserv, request, picture_lines):
    """Poll the COUNT, not /geometry's state, which is stale across a source
    change: it answers `acquired` from the source that just went away."""
    reply = modeserv(request)
    assert "F-1" not in reply, (
        f"the source read back {reply!r}: it is not driving a usable raster, so "
        f"nothing measured here would be about the scaler")
    seen = []

    def held():
        seen.append(read_fields(host, ["STATUS_SYNC_PROC_VTOTAL"])
                    .get("STATUS_SYNC_PROC_VTOTAL"))
        run = seen[-4:]
        return (len(run) == 4 and run[0] and len(set(run)) == 1
                and run[0] >= picture_lines)

    assert wait_for(held, timeout=45.0, interval=0.5), (
        f"the sync processor never held a count of at least {picture_lines} "
        f"for {request!r}; it read {seen[-8:]}")


def _wait_for_picture(dev):
    """The dongle re-locks seconds after the engine has finished, and a clip
    taken before that is all black. It WAITS rather than skipping: a run that
    skipped here would be green without having looked at anything."""
    assert wait_for(lambda: float(luma(frames(2, dev, WIDTH, HEIGHT,
                                              warmup=12)).mean()) > BLACK,
                    timeout=40.0, interval=0.0), (
        "the dongle delivered nothing but black for 40s. Ask the board before "
        "the connector -- hdmi_capture.py")


@pytest.mark.parametrize("name,request_,picture_lines", MODES,
                         ids=[m[0] for m in MODES])
def test_the_engine_samples_at_a_phase_that_holds_the_grating_still(
        host, source, modeserv, name, request_, picture_lines):
    dev = device()

    # THE PHASE IS GIVEN BACK BEFORE THE MODE CHANGE, not after. Clearing does
    # not itself make the engine re-choose, so a hold left by an earlier test
    # or by a hand sweep would be measured as the engine's own pick; the solve
    # the mode change causes is what re-chooses.
    _route(host, "/phase?clear")
    time.sleep(SETTLE_S)
    _settle(host, modeserv, request_, picture_lines)
    modeserv("PATTERN PM5544")
    modeserv("ANIM OFF")
    _wait_for_picture(dev)

    held = read_fields(host, ["PA_ADC_S", "PLLAD_MD"])
    greys = luma(frames(FRAMES, dev, WIDTH, HEIGHT, warmup=WARMUP))
    lit = _lit_box(greys[0])
    box = wedge.locate(greys[0], lit)
    contrast, movement = wedge.score(greys, box)
    _, floor = wedge.score(greys, wedge.flattest(greys[0], lit, box))

    assert contrast > 0, f"{name}: the fine wedge is flat, so nothing can be judged"
    share = movement / contrast

    assert share <= MOVING_SHARE_CEILING, (
        f"{name}: {share:.3f} of the fine grating changes between frames, over "
        f"{MOVING_SHARE_CEILING} -- {movement:.2f} grey levels of movement "
        f"against {contrast:.2f} of contrast, where the flattest card clear of "
        f"it in the same frames moves {floor:.2f}. The card's animation is off, "
        f"so none of that is the source's. PA_ADC_S {held['PA_ADC_S']} at "
        f"PLLAD_MD {held['PLLAD_MD']}: nothing searches that phase, "
        f"Adc::acquirePhase() writes the middle of the field. Walk it with "
        f"adc_phase_sweep.py -- "
        f"docs/investigations/the-adc-sampling-phase-is-a-lottery.md")
