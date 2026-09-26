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

from gbs_unit import ACQUIRE_LIMIT_S, select_input, wait_for_acquisition

# What the first selection is given before the second follows it. The mux has
# moved by then, which is what makes detection claim the source immediately.
REPEAT_GAP_S = 1.5


def test_a_repeated_selection_still_acquires(host, on_vga):
    select_input(host, "ypbpr")
    time.sleep(REPEAT_GAP_S)
    select_input(host, "ypbpr")

    waited = wait_for_acquisition(host, "ypbpr")

    assert waited is not None, (
        "a repeated selection never acquired inside %.0fs: the absence run has "
        "stopped retrying the teardown, so the engine gets one attempt and then "
        "sits absent with the source present" % ACQUIRE_LIMIT_S)
