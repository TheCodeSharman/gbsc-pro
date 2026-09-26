"""A repeated input selection still reaches an acquisition, rather than stalling.

Selecting an input twice, before the first selection has solved, used to leave the
unit unable to see the source at all. The second selection arrives with the analog
mux settled, so detection claims the source at once while the divider is still the
previous input's -- and the teardown that repairs that was a one-shot, gated on a
flag cleared only where detection claims a source. So the engine got one attempt
and then sat at `state: absent` with the source present. Measured 6 of 6 held for
the full 90 s each was given before the repair, and 0 of 6 after, acquiring in
4.1..6.4 s.

**The pass/fail margin here is wide, which is why this is the test kept.** A
healthy acquisition takes 4..8 s and the state this guards against did not recover
at all, so the limit below sits nowhere near either. Timing an acquisition against
a threshold is the opposite: the wedge the same repair removed cost 25..32 s
against a healthy worst case of 12.6 s, and no threshold separates those reliably
enough to gate a build on.

docs/investigations/a-ypbpr-detection-that-succeeds-first-pass-skips-the-preparation.md
"""

import time

import pytest

from gbs_unit import geometry_gated, get, get_json

# The bench pair, and the line rate each measures when it is the one acquired. A
# source is identified by its rate rather than its capture height: the solved
# height varies between acquisitions of ONE source -- 525, 526 and 529 all
# measured on the Wii -- so a harness keyed on that reports false stalls.
SOURCES = {"vga": 37879, "ypbpr": 31468}
RATE_TOLERANCE_HZ = 900

# Healthy acquisitions land in 4..8 s, and the state this guards against did not
# recover at all. Generous, because a slow acquisition is not this fault.
ACQUIRE_LIMIT_S = 60.0

# What the first selection is given before the second follows it. The mux has
# moved by then, which is what makes detection claim the source immediately.
REPEAT_GAP_S = 1.5


def acquired_rate(host, want):
    """The line rate once the engine holds an acquisition OF `want`, else None.

    A stalled selection keeps the PREVIOUS source's solve and still reports
    `state: acquired`, so the rate is what separates the two.
    """
    status, payload = get_json(host, "/geometry")
    if status != 200 or payload is None:
        return None
    if payload.get("state") != "acquired":
        return None
    rate = payload.get("lineRateHz", 0)
    if abs(rate - SOURCES[want]) > RATE_TOLERANCE_HZ:
        return None
    return rate


def select(host, want):
    """/input is queued for loop(), so the 200 means understood, not selected."""
    assert "queued" in get(host, "/input?src=%s" % want)[1]


def wait_for_acquisition(host, want, limit_s=ACQUIRE_LIMIT_S):
    """Seconds until `want` is acquired, or None once `limit_s` has passed."""
    started = time.monotonic()
    while time.monotonic() - started < limit_s:
        if acquired_rate(host, want) is not None:
            return time.monotonic() - started
        time.sleep(0.25)
    return None


@pytest.fixture
def on_vga(host, source):
    """Settled on `vga`, so the divider the repeated selection has to survive is
    the same one every run.

    Skips rather than fails where a source will not lock: the Wii goes to its idle
    screen on a button press, and both being present is the operator's promise
    rather than something the unit can be asked.
    """
    if geometry_gated(host):
        pytest.skip("/geometry is gated out of this build")
    select(host, "vga")
    if wait_for_acquisition(host, "vga") is None:
        pytest.skip("vga does not acquire: check the source is on")
    yield
    select(host, "ypbpr")


def test_a_repeated_selection_still_acquires(host, on_vga):
    select(host, "ypbpr")
    time.sleep(REPEAT_GAP_S)
    select(host, "ypbpr")

    waited = wait_for_acquisition(host, "ypbpr")

    assert waited is not None, (
        "a repeated selection never acquired inside %.0fs: the absence run has "
        "stopped retrying the teardown, so the engine gets one attempt and then "
        "sits absent with the source present" % ACQUIRE_LIMIT_S)
