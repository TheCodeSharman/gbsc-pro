"""What the session teardown reports when it cannot put the bench back.

No hardware: the point is the REPORTING. A teardown that gives up quietly
leaves a unit whose picture is wrong and whose state reads as a fault, and
`reset_framing()` answers None rather than raising -- so a caller that ignores
the return value cannot tell a reset that ran from one that did not.
Measured once: a full run left a stored framing behind, the picture 1106 of
1920 columns wide, and the teardown said nothing.
"""

from gbs_unit import restore_bench


def ok(_address):
    return {"oh": 169, "eh": 1248}


def never(_address):
    return None


def test_a_reset_that_lands_reports_nothing():
    assert restore_bench("unit", unfreeze=lambda a: None, reset=ok) == []


def test_a_reset_that_never_lands_is_reported():
    problems = restore_bench("unit", unfreeze=lambda a: None, reset=never)

    assert len(problems) == 1
    assert "framing" in problems[0]


def test_a_reset_that_never_lands_is_retried():
    tries = []

    def counting(address):
        tries.append(address)
        return None

    restore_bench("unit", unfreeze=lambda a: None, reset=counting, attempts=3)

    assert len(tries) == 3


def test_a_reset_that_lands_on_a_later_try_reports_nothing():
    tries = []

    def second_time(address):
        tries.append(address)
        return None if len(tries) < 2 else ok(address)

    assert restore_bench("unit", unfreeze=lambda a: None, reset=second_time) == []


def test_a_raising_reset_is_reported_rather_than_propagated():
    def raising(_address):
        raise OSError("connection refused")

    problems = restore_bench("unit", unfreeze=lambda a: None, reset=raising)

    assert len(problems) == 1
    assert "connection refused" in problems[0]


def test_the_framing_is_still_reset_when_unfreezing_fails():
    def raising(_address):
        raise OSError("no route")

    tried = []

    def noting(address):
        tried.append(address)
        return ok(address)

    problems = restore_bench("unit", unfreeze=raising, reset=noting)

    assert tried == ["unit"]
    assert len(problems) == 1
    assert "no route" in problems[0]
