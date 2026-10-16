"""The auto-gain walk converges on a settled source and then holds.

`Tv5725::AutoGain` backs the three analog gains off while the converter's own
level saturates, and it only ever backs off -- so every step it takes wrongly
is one nothing takes back. Two failures put the picture somewhere no later pass
recovers, and both are states a register read can see:

- the walk running away to the end of the register, which is what a step taken
  before the previous one reaches the measurement does. It took the gain from
  the seed to 0xff in about a second, leaving a dark picture.
- the walk creeping, a step at a time, on a picture that has stopped clipping.

So this asserts the settled state rather than the convergence: the gains agree
with each other, they are off the rail, and once quiet they stay quiet across a
window far longer than the walk's own settling time.

Quiescence is waited for rather than assumed. A source arriving re-seeds the
gain and starts the walk again, so a reading taken at the moment the engine
acquires catches a walk that is working and reads as one that is broken.

A source showing nothing bright never moves the gains at all, which passes here
and should -- what is being pinned is that the walk does not move them WRONGLY.

docs/investigations/the-auto-gain-loop-read-a-selector-with-no-level.md
"""

import time

import pytest

from gbs_unit import get_json, read_fields, setting, wait_for

# Long enough for a boot or a selection to acquire and present.
SETTLE_SECONDS = 60.0

# How long the gains have to stand still before the walk counts as converged,
# and how long it is given to get there. Convergence from the seed is about a
# second; the budget is loose because a source arriving restarts it.
QUIET_SECONDS = 20.0
CONVERGE_SECONDS = 120.0

# Far longer than AutoGain::SettleMs, so a walk that is still stepping is seen
# stepping. The creep this catches ran at about a step a minute.
HOLD_SECONDS = 90.0

GAINS = ["ADC_RGCTRL", "ADC_GGCTRL", "ADC_BGCTRL"]

# The top of the register. The walk clamps here rather than wrapping, so this
# is where a runaway comes to rest.
RAIL = 0xFF


def _acquired(host):
    status, payload = get_json(host, "/geometry", timeout=3)
    return status == 200 and payload is not None and payload.get("state") == "acquired"


def _quiet_gain(host):
    """The gains once they have stood still for QUIET_SECONDS, else None."""
    held = None
    since = time.time()
    deadline = time.time() + CONVERGE_SECONDS
    while time.time() < deadline:
        reading = read_fields(host, GAINS)
        if reading != held:
            held = reading
            since = time.time()
        elif time.time() - since >= QUIET_SECONDS:
            return held
        time.sleep(2.0)
    return None


def test_the_auto_gain_walk_settles_and_holds(host):
    # Waited for rather than skipped on: a verdict taken off an unsolved chip
    # reads green and is not.
    assert wait_for(lambda: _acquired(host), timeout=SETTLE_SECONDS)

    if str(setting(host, "auto-gain") or "").strip() != "1":
        pytest.skip("auto gain is off on this unit, so there is no walk to judge")

    before = _quiet_gain(host)
    assert before is not None, (
        f"the gains never stood still for {QUIET_SECONDS:.0f}s inside "
        f"{CONVERGE_SECONDS:.0f}s, so the walk is not converging"
    )
    assert len(set(before.values())) == 1, (
        f"the three gains disagree ({before}); the walk moves them together so "
        "the grey balance the colour space set is kept"
    )
    assert before["ADC_GGCTRL"] != RAIL, (
        "the gain is at the top of the register, which is where a walk that "
        "stepped faster than its measurement comes to rest"
    )

    time.sleep(HOLD_SECONDS)

    after = read_fields(host, GAINS)
    assert after == before, (
        f"the gain moved from {before} to {after} over {HOLD_SECONDS:.0f}s after "
        "it had already gone quiet; the walk is creeping, and it cannot step back"
    )
