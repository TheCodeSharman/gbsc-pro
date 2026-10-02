"""A source selected on either input is acquired inside a few seconds.

LEAVING LOW POWER IS A FLAG, NOT AN ACT, and that is what this guards. The power
path holds every block of the part in reset and puts the ADC PLL in reset with
its clock enable off; detection then succeeds, clears the flag and returns.
Nothing built the chip back up, because the only thing that ever did was the
sketch's preset-load block and the engine no longer loads presets.

The sync processor counts in ADC clocks, so a chip left that way measures
nothing: `STATUS_SYNC_PROC_VTOTAL` and `HTOTAL` read 0 whatever the source is
doing, which is indistinguishable from a dead cable. What got a picture in the
end was the recovery ladder, no earlier than the first acquisition's grace
window -- so every second of the wait was the ladder doing the engine's job, and
the five block resets no rung touches left the output black after it.

Measured on the bench before the repair: `vga` 8.5 s, `ypbpr` 32.8 s. `vga`
detects on its first pass and never enters low power, so its PLL is never held
and it looks almost healthy; `ypbpr`'s first detection returns nothing, the power
path runs, and the rest follows from that. **Both legs are asserted because the
asymmetry is the evidence**, and a test watching only the slow input would pass
as soon as the fast one regressed to match it.

**Repeated, because one leg's timing is not the feature.** The teardown is
entered per selection that finds no source immediately, so a round trip that
happened to detect on its first pass exercises nothing.

**THE TWO SOURCES HAVE TO BE TELLABLE APART, AND AT THE EVERYDAY BENCH MODE
THEY ARE NOT.** A stalled selection keeps the previous source's solve and still
reports `state: acquired`, so the reported line rate is the only thing
separating "landed" from "never moved". With the RiscPC at 320x256@50 that is
15625 Hz and the Wii in 576i measures 15549 -- 76 Hz apart, inside
`RATE_TOLERANCE_HZ`, so no wait on a rate can tell which input is on. The
RiscPC is therefore driven to 800x600@60 for the run and put back afterwards.

**`vga`'s RATE IS KNOWN BY CONSTRUCTION AND THE WII'S IS LEARNED.** The run
commands the RiscPC into exactly the mode `gbs_unit.SOURCES` assumes, so that
entry is right rather than hoped for. Nothing can do the same for the Wii: its
output mode is a bench setting with no readback, and 480i, 480p and 576i measure
nothing like each other -- so it is measured, against `vga`'s rate, because
until the report leaves that one the solve on display is still the RiscPC's.

**TWO TESTS, BECAUSE THE REPAIR AND THE TARGET ARE NOT THE SAME CLAIM.** What
the build-up owns is that the chip is MEASURABLE again promptly, and that is
asserted directly: the sync processor counts in ADC clocks, so
`STATUS_SYNC_PROC_HTOTAL` matching `PLLAD_MD` is the one honest witness that the
ADC PLL is running, and the five `s0_46` block resets say the part can emit.
Measured 1.6 s after a selection with the repair in, against 16.6 s of
`HTOTAL` 0 without it.

The wall clock to an acquisition is the whole path's target and is NOT met on
`ypbpr`: the ADC PLL locks at the reference divider inside two seconds and
`STATUS_SYNC_PROC_VTOTAL` then sits at 97 for fifteen more, because the
sync-on-green path does not follow the source until the recovery ladder lifts
the SOG floor and widens the coast -- and no rung may run inside
`FirstAcquisitionGraceMs`. That is a separate fault with its own entry in
docs/known-issues.md, so the wall-clock test is carried as a strict xfail: it
flips to passing the day that one is fixed, and fails the run if it starts
passing unnoticed.

docs/investigations/the-ladder-never-restarts-the-adc-pll.md
"""

import time

import pytest

from gbs_unit import (RATE_TOLERANCE_HZ, SOURCES, geometry_gated, get_json,
                      modeserv_ok, read_fields, select_input,
                      wait_for_acquisition)

# How many round trips one run makes.
ROUND_TRIPS = 2

# What a leg is given before it is called a failure. Acquisition is seconds on
# both inputs, and this is the pass mark the repair was built to rather than a
# measurement of a healthy unit.
ACQUIRE_LIMIT_S = 10.0

# How long a leg is watched before it is reported as never arriving. Long enough
# that a merely slow one is reported with its real time, which is the difference
# between "the ladder is still in the loop" and "the source is not there".
REPORT_LIMIT_S = 60.0

# What the learning phase allows. Wider than REPORT_LIMIT_S, because the first
# selection of a run is the one that pays for everything the bench left behind.
LEARN_LIMIT_S = 90.0

INPUTS = ("ypbpr", "vga")

# How long a teardown is given to be undone. The repair does it in the pass that
# follows, and without it the chip stayed unmeasurable for 16.6 s.
UNDONE_LIMIT_S = 5.0

# How long each input is watched for a teardown and its repair.
WATCH_S = 30.0

# What the sync processor's count is allowed to differ from the divider. Locked
# it reports the divider back, 0 or +-1 on every healthy state measured; a PLL
# that is not running reads 0 and a free-running one is hundreds out.
HTOTAL_TOLERANCE = 4

# The five blocks the power path holds and only the bring-up releases. Held, the
# part emits nothing while every configuration register reads correct.
HELD_BLOCKS = ("SFTRST_DEINT_RSTZ", "SFTRST_MEM_FF_RSTZ", "SFTRST_MEM_RSTZ",
               "SFTRST_FIFO_RSTZ", "SFTRST_OSD_RSTZ")

MEASURABLE_FIELDS = ("PLLAD_MD", "STATUS_SYNC_PROC_HTOTAL") + HELD_BLOCKS


def _measurable(host):
    """Whether the chip can be measured and could emit, in one read.

    One request for all seven, because they are only comparable to each other
    when they come from the same pass.
    """
    got = read_fields(host, list(MEASURABLE_FIELDS))
    if got is None:
        return None
    if any(got.get(name) != 1 for name in HELD_BLOCKS):
        return False
    counted, divider = got["STATUS_SYNC_PROC_HTOTAL"], got["PLLAD_MD"]
    return counted != 0 and abs(counted - divider) <= HTOTAL_TOLERANCE

# The mode the RiscPC runs for the duration, and the one the bench keeps it in.
# 800x600@60 is 37.9 kHz, clear of anything the Wii emits and the rate
# gbs_unit.SOURCES names for this input, so the rate says which input is
# acquired. At the bench mode it does not: 320x256@50 is 15625 Hz and the Wii in
# 576i measures 15549.
RUN_MODE = "MODE X800 Y600 C256 F60"
BENCH_MODE = "MODE X320 Y256 C256 F50"


def _acquired_rate(host):
    """The line rate the engine is holding an acquisition at, else None."""
    status, payload = get_json(host, "/geometry")
    if status != 200 or payload is None or payload.get("state") != "acquired":
        return None
    return payload.get("lineRateHz") or None


def _wait_for_rate(host, rate_hz, limit_s):
    """Seconds until the engine holds an acquisition AT `rate_hz`, else None."""
    started = time.monotonic()
    while time.monotonic() - started < limit_s:
        got = _acquired_rate(host)
        if got is not None and abs(got - rate_hz) <= RATE_TOLERANCE_HZ:
            return time.monotonic() - started
        time.sleep(0.25)
    return None


def _learn_other_rate(host, want, differing_from):
    """Select `want` and return the rate it settles at, or None.

    `differing_from` is the rate the OTHER input measures: until the report
    leaves it, the solve on display is still that source's.
    """
    select_input(host, want)
    started = time.monotonic()
    while time.monotonic() - started < LEARN_LIMIT_S:
        got = _acquired_rate(host)
        if got is not None and abs(got - differing_from) > RATE_TOLERANCE_HZ:
            return got
        time.sleep(0.25)
    return None


@pytest.fixture
def vga_at_the_end(host):
    """Leave the unit on `vga`, which is where the bench keeps it. Takes no
    source, so this one runs without --modeserv."""
    yield
    select_input(host, "vga")


@pytest.fixture
def bench_source_restored(host, modeserv):
    """Put the source back the way the bench keeps it, and the unit on `vga`.

    The mode change repaints the card itself, so nothing else is needed to
    leave a still picture behind.
    """
    yield
    modeserv(BENCH_MODE)
    modeserv("ANIM OFF")
    select_input(host, "vga")


def _watch_one_teardown(host, want):
    """Select `want` and return (seconds the chip was unmeasurable, or None).

    None means no teardown was seen, which is the honest answer for an input
    that detects on its first pass and never enters the power path: there is
    nothing there to time.
    """
    select_input(host, want)
    torn_at = None
    started = time.monotonic()
    while time.monotonic() - started < WATCH_S:
        ok = _measurable(host)
        if ok is None:
            pass
        elif torn_at is None:
            if not ok:
                torn_at = time.monotonic()
        elif ok:
            return time.monotonic() - torn_at
        time.sleep(0.4)
    return None if torn_at is None else WATCH_S


def test_a_torn_down_chip_is_built_back_up_by_the_next_pass(host, source,
                                                            vga_at_the_end):
    """The chip the power path tore down is measurable again within seconds.

    Asserted on the registers rather than on an acquisition, because this is the
    whole of what the build-up owns: the sync processor counts in ADC clocks, so
    a count that matches the divider is the one witness that the ADC PLL is
    running, and the five block resets are what decides whether the part can
    emit at all.
    """
    undone = {want: _watch_one_teardown(host, want) for want in INPUTS}

    seen = {w: s for w, s in undone.items() if s is not None}
    assert seen, (
        "neither input tore the chip down inside %.0fs, so there is nothing "
        "here to time. The power path runs on a selection whose first detection "
        "finds no sync; check both sources are on." % WATCH_S)

    slow = {w: s for w, s in seen.items() if s > UNDONE_LIMIT_S}
    assert not slow, (
        "a teardown was still not undone after %.1fs: %s. Leaving low power is "
        "a flag, not an act: the blocks stay held and the ADC PLL stays in "
        "reset until something builds the chip back up, and until then the sync "
        "processor counts nothing. Every input: %s"
        % (UNDONE_LIMIT_S,
           "; ".join(f"{w} {round(s, 1)}s" for w, s in slow.items()),
           "; ".join(f"{w} {'no teardown' if s is None else str(round(s, 1)) + 's'}"
                     for w, s in undone.items())))


@pytest.mark.xfail(strict=True,
                   reason="ypbpr holds STATUS_SYNC_PROC_VTOTAL at 97 for ~15s "
                          "after the ADC PLL locks: the sync-on-green path does "
                          "not follow the source until the recovery ladder's "
                          "SOG rungs run, and none may run inside "
                          "FirstAcquisitionGraceMs. docs/known-issues.md")
def test_either_input_is_acquired_without_the_recovery_ladder(
        host, source, modeserv, bench_source_restored):
    if geometry_gated(host):
        pytest.skip("/geometry is gated out of this build")

    reply = modeserv(RUN_MODE)
    assert modeserv_ok(reply), f"ModeServ refused {RUN_MODE}: {reply!r}"
    modeserv("ANIM OFF")

    # NOT A SKIP, either of them. Both bench sources being present is the
    # promise --source carries, and a run that skipped because one of them did
    # not acquire would report green for exactly the fault this guards.
    rate = {"vga": SOURCES["vga"]}
    select_input(host, "vga")
    assert wait_for_acquisition(host, "vga", limit_s=LEARN_LIMIT_S) is not None, (
        "vga never reported an acquisition at %d Hz inside %.0fs, so there is "
        "nothing to time. The source was commanded into %s, which is that rate."
        % (rate["vga"], LEARN_LIMIT_S, RUN_MODE))

    rate["ypbpr"] = _learn_other_rate(host, "ypbpr", differing_from=rate["vga"])
    assert rate["ypbpr"] is not None, (
        "ypbpr never reported an acquisition of its own inside %.0fs, so there "
        "is nothing to time -- vga's %d Hz solve was still on display. Check "
        "the Wii is on." % (LEARN_LIMIT_S, rate["vga"]))

    # Back to vga before the first trip, or trip 1's ypbpr leg is the state the
    # learning phase left behind and times 0.0 s against a selection that moved
    # nothing. Every leg below has to be a transition.
    select_input(host, "vga")
    assert wait_for_acquisition(host, "vga", limit_s=LEARN_LIMIT_S) is not None, (
        "vga did not come back after the Wii was read, so the round trips would "
        "start from an unknown input")

    taken = []

    for trip in range(1, ROUND_TRIPS + 1):
        for want in INPUTS:
            select_input(host, want)
            taken.append((trip, want,
                          _wait_for_rate(host, rate[want], REPORT_LIMIT_S)))

    legs = "; ".join(f"trip {t} {w} "
                     f"{'never' if s is None else str(round(s, 1)) + 's'}"
                     for t, w, s in taken)

    never = [(t, w) for t, w, s in taken if s is None]
    assert not never, (
        "an input never acquired inside %.0fs: %s. Every leg: %s"
        % (REPORT_LIMIT_S, "; ".join(f"trip {t} {w}" for t, w in never), legs))

    slow = [(t, w, s) for t, w, s in taken if s > ACQUIRE_LIMIT_S]
    assert not slow, (
        "an input took longer than %.0fs to acquire: %s. A wait of tens of "
        "seconds is the recovery ladder rebuilding what the power path tore "
        "down, not a source settling. Every leg: %s"
        % (ACQUIRE_LIMIT_S,
           "; ".join(f"trip {t} {w} {round(s, 1)}s" for t, w, s in slow), legs))
