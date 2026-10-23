"""The re-acquisition soak's verdict, against canned samples. No hardware.

The fault it exists to catch is per-ACQUISITION: the sampling clock either
locks when the source is acquired or never does, and the picture follows. So
one reading says nothing and the measurement is a run of acquisitions.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reacquire_soak as soak

# What the instrument reads on a healthy acquisition and on a dead one,
# measured on the bench at 25 ms over 10 s: the RISC PC reads 1 in 86.5% of 392
# samples with 93 transitions, and a Wii acquisition that did not lock reads 0
# in 769 of 769 with none.
HEALTHY = [1] * 865 + [0] * 135
DEAD = [0] * 769


def test_a_locked_acquisition_passes():
    assert soak.lock_share(HEALTHY) > soak.LOCKED_SHARE
    assert soak.locked(HEALTHY)


def test_an_acquisition_that_never_locked_fails():
    """Not one sample of 769 reads 1, where a healthy one reads 1 in six of
    seven -- so the two are not near a threshold, they are the two ends."""
    assert soak.lock_share(DEAD) == 0.0
    assert not soak.locked(DEAD)


def test_a_bit_that_is_a_duty_cycle_is_not_judged_on_one_reading():
    """STATUS_MISC_PLLAD_LOCK flickers about nine times a second on a picture
    judged perfect, so a single sample has better than a one in eight chance of
    reading 0 while everything is right."""
    assert not soak.enough([0])
    assert not soak.enough([1] * (soak.LEAST_SAMPLES - 1))
    assert soak.enough([1] * soak.LEAST_SAMPLES)


def test_an_acquisition_with_too_few_samples_is_not_a_verdict():
    """The console drops bursts, so a run that collected almost nothing is a
    missed window rather than a dead sampling clock -- and reporting it as a
    failure is how a flaky capture reads as a firmware fault."""
    assert soak.judge([0] * 3, phase=16)["verdict"] == soak.UNRESOLVED
    assert soak.judge(DEAD, phase=16)["verdict"] == soak.UNLOCKED
    assert soak.judge(HEALTHY, phase=16)["verdict"] == soak.LOCKED


def test_the_summary_counts_only_resolved_acquisitions():
    """A soak's figure is locked-of-judged, not locked-of-attempted: an
    acquisition the instrument missed belongs in neither half."""
    runs = [soak.judge(HEALTHY, 16), soak.judge(DEAD, 9),
            soak.judge([1] * 2, 16), soak.judge(HEALTHY, 16)]
    got = soak.summarise(runs)
    assert got["judged"] == 3
    assert got["locked"] == 2
    assert got["unresolved"] == 1


def test_the_phases_chosen_are_reported_so_a_lottery_is_visible():
    """Which phase each acquisition landed on is the evidence that the choice
    is a lottery rather than a constant, so the summary carries the set."""
    runs = [soak.judge(HEALTHY, 16), soak.judge(soak.DEAD_SAMPLE * 700, 9),
            soak.judge(HEALTHY, 16)]
    assert soak.summarise(runs)["phases"] == {16: 2, 9: 1}


def test_a_restart_run_selects_the_source_before_restarting(monkeypatch):
    """**A RESTART COMES BACK ON THE SAVED INPUT, WHICHEVER THAT IS.** The
    route restarts and never selects, so a run labelled ypbpr measured vga for
    six acquisitions and scored them against a ypbpr baseline -- which read as
    the chosen phase regressing from 16 to 0 when 0 is simply vga's own answer.

    Selected BEFORE the restart rather than after, so the boot still installs
    the ADC PLL from nothing with no post-boot input change in the way: the
    selection persists to /preferences.txt and the boot restores it."""
    order = []
    monkeypatch.setattr(soak.urllib.request, "urlopen",
                        lambda url, timeout=0: order.append(url) or _NoBody())
    monkeypatch.setattr(soak.gbs_unit, "select_input",
                        lambda host, want: order.append(f"select:{want}"))
    monkeypatch.setattr(soak.time, "sleep", lambda _: None)
    monkeypatch.setattr(soak.gbs_unit, "get_json",
                        lambda host, path: (200, {"cv": 525, "lineRateHz": 31472}))

    assert soak.reacquire("unit", "ypbpr", "vga", 1.0, via=soak.VIA_RESTART)
    assert order == ["select:ypbpr", "http://unit/restart"]


def test_a_run_that_came_back_on_another_input_is_not_a_verdict(monkeypatch):
    """A measurement taken on a source nobody asked for belongs in neither
    half of the figure, exactly as a missed capture does."""
    monkeypatch.setattr(soak.gbs_unit, "setting", lambda host, key: "vga")
    assert not soak.measured_wanted_source("unit", "ypbpr")
    monkeypatch.setattr(soak.gbs_unit, "setting", lambda host, key: "ypbpr")
    assert soak.measured_wanted_source("unit", "ypbpr")


def test_an_output_run_moves_the_output_and_leaves_the_input_alone(monkeypatch):
    """**THE OUTPUT DECIDES THE DIVIDER, SO IT DECIDES WHAT THE ADC PLL HAS TO
    LOCK ON.** A landing re-solves the raster, the raster sets the divider
    ceiling and the divider is installed afresh -- all without the source
    moving, which is what makes this a different test from an input bounce.

    It must not touch the input: a run that re-selected the source would be
    measuring a fresh acquisition as well, and the two could not be told apart.
    """
    chosen, selected, asked = [], [], []
    monkeypatch.setattr(soak.gbs_unit, "choose_output",
                        lambda host, output: chosen.append(output) or True)
    monkeypatch.setattr(soak.gbs_unit, "select_input",
                        lambda host, want: selected.append(want))
    monkeypatch.setattr(soak.urllib.request, "urlopen",
                        lambda url, timeout=0: asked.append(url) or _NoBody())
    monkeypatch.setattr(soak.time, "sleep", lambda _: None)
    monkeypatch.setattr(soak.gbs_unit, "get_json",
                        lambda host, path: (200, {"cv": 525, "lineRateHz": 31472}))

    assert soak.reacquire("unit", "ypbpr", "vga", 1.0, via=soak.VIA_OUTPUT, leg=0)
    assert chosen == [soak.OUTPUT_ROTATION[0]]
    assert selected == []
    assert asked == []


def test_the_output_rotation_returns_to_the_mode_it_started_from(monkeypatch):
    """A hunt that ends on whichever mode its last leg happened to reach leaves
    the bench somewhere nobody chose. Every other leg is 1080p, so a run of any
    even length lands back there."""
    assert soak.OUTPUT_ROTATION[1] == "1080p"
    assert len(soak.OUTPUT_ROTATION) % 2 == 0
    assert all(m == "1080p" for m in soak.OUTPUT_ROTATION[1::2])


def test_an_input_run_bounces_away_and_back(monkeypatch):
    asked, selected = [], []
    monkeypatch.setattr(soak.urllib.request, "urlopen",
                        lambda url, timeout=0: asked.append(url) or _NoBody())
    monkeypatch.setattr(soak.gbs_unit, "select_input",
                        lambda host, want: selected.append(want))
    monkeypatch.setattr(soak.time, "sleep", lambda _: None)
    monkeypatch.setattr(soak.gbs_unit, "get_json",
                        lambda host, path: (200, {"cv": 525, "lineRateHz": 31472}))

    assert soak.reacquire("unit", "ypbpr", "vga", 1.0, via=soak.VIA_INPUT)
    assert selected == ["vga", "ypbpr"]
    assert asked == []


class _NoBody:
    def read(self):
        return b""
