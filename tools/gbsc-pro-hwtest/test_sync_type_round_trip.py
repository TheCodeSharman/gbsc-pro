"""A source that changes its sync type is re-acquired, both ways.

The RiscPC sets its sync type from CMOS at one line count -- 311 at 320x256@50 --
so `SYNC 0` and `SYNC 1` move neither the count nor the field rate, and nothing
in `source moved` separates them. The engine has to notice the arrangement
itself, and when it does not the failure is silent in every instrument but the
picture: the console reports the source correctly hundreds of times a minute
while `/geometry` sits at `state: absent`, the sync pad is never driven back and
the output stays black.

Measured on the bench before the repair, 320x256@50 on `vga`: the return to
separate sync left `STATUS_SYNC_PROC_HTOTAL` wandering 3218..3251 against a
`PLLAD_MD` of 2200, so every duty reading was refused as UNLOCKED and the solve
never completed. 482 measurement passes in 60 s, no progress, `sync pad: away`
with no `driven` after it. The escalation ladder climbed to its last rung
without clearing it; `/sc?~` cleared it at once.

**Both legs are asserted, because both are the same feature.** The composite leg
is the slow and variable direction and has its own history -- 4.8 to 28.4 s
across six trials -- so a test that only watched the return would pass whenever
the composite leg failed to acquire at all, which is the state in which the
return cannot wedge.

**Repeated, because the wedge is not deterministic.** It needs the composite leg
to have acquired first: that is what runs the sync processor's preparation with
serrated sync in force. A single round trip that never acquired on composite
exercises nothing.

docs/known-issues.md, "A sync-type change arms no re-probe, so the held answer
outlives the source".
"""

import pytest

from gbs_unit import (acquired_and_settled, geometry_gated, mode_serv,
                      modeserv_ok, select_input)

# How many round trips one run makes. Enough that the composite leg acquires at
# least once, which is the wedge's precondition, without turning an acceptance
# test into a soak.
ROUND_TRIPS = 3

# What each leg is given before it is called a failure. Far outside a healthy
# acquisition, which is 4..8 s on this input, and far inside the state this
# guards against, which did not recover at all in 100 s of watching.
SETTLE_LIMIT_S = 75.0

# ModeServ's argument for each arrangement.
SEPARATE, COMPOSITE = "0", "1"


def _set_sync(modeserv, which):
    reply = modeserv(f"SYNC {which}")
    assert modeserv_ok(reply), f"ModeServ refused SYNC {which}: {reply!r}"


@pytest.fixture
def separate_sync_at_the_end(request):
    """Put the source back on separate sync however the test leaves it. The
    bench keeps it there, and a run that ended mid-round-trip would otherwise
    leave the next session diagnosing the source."""
    yield
    where = request.config.getoption("--modeserv")
    if where:
        mode_serv(where, f"SYNC {SEPARATE}")


def test_a_sync_type_round_trip_re_acquires(host, source, modeserv,
                                            separate_sync_at_the_end):
    if geometry_gated(host):
        pytest.skip("/geometry is gated out of this build")

    # On `vga`, which is the one input the RiscPC reaches and the only one whose
    # sync type is the SOURCE's to set. Settled by what the engine reports rather
    # than by a line rate, because the RiscPC runs this test at whatever mode the
    # bench left it in.
    select_input(host, "vga")
    if not acquired_and_settled(host, limit_s=SETTLE_LIMIT_S):
        pytest.skip("vga does not settle: check the source is on")

    failures = []

    for trip in range(1, ROUND_TRIPS + 1):
        _set_sync(modeserv, COMPOSITE)
        if not acquired_and_settled(host, limit_s=SETTLE_LIMIT_S):
            failures.append(f"trip {trip}: composite sync never settled")

        _set_sync(modeserv, SEPARATE)
        if not acquired_and_settled(host, limit_s=SETTLE_LIMIT_S):
            failures.append(f"trip {trip}: the return to separate sync never settled")

    assert not failures, (
        "a sync-type change left the engine unable to solve the source inside "
        "%.0fs: %s. The source is measured correctly throughout -- what does not "
        "recover is the sync processor's configuration for the arrangement it "
        "has left." % (SETTLE_LIMIT_S, "; ".join(failures)))
