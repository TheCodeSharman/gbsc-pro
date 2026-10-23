"""What transition_soak.py decides between captures.

No hardware: the ordering of a transition and the judgement of an outlier are
the parts that decide whether a night's run is readable afterwards.
"""

import random

from transition_soak import LEGS, is_outlier, next_leg, plan


def state(src="vga", passthrough=False, output="1080p"):
    return dict(input=src, passthrough=passthrough, output=output)


def test_a_leg_already_in_force_needs_no_action():
    assert plan(state(), LEGS["vga-scaled-1080p"]) == []


def test_the_route_is_set_before_the_input_changes():
    """A source arriving while the other route is still preferred acquires on
    it and is then re-resolved, so the leg's own transition is the second of
    two and the first is nobody's."""
    actions = plan(state(), LEGS["ypbpr-pass"])
    assert [kind for kind, _ in actions] == ["route", "input"]


def test_a_scaled_leg_sets_its_output_last():
    """The output re-solve is the transition the capture is of, so nothing may
    follow it."""
    actions = plan(state(src="ypbpr"), LEGS["vga-scaled-720p"])
    assert actions[-1] == ("output", "720p")


def test_a_pass_through_leg_asks_for_no_output():
    """The VDS is out of circuit, so an output resolution means nothing there
    and asking for one is a transition the leg did not want."""
    actions = plan(state(), LEGS["vga-pass"])
    assert [kind for kind, _ in actions] == ["route"]


def test_the_next_leg_is_never_the_one_already_in_force():
    rng = random.Random(1)
    names = list(LEGS)
    assert all(next_leg(rng, names, "vga-pass") != "vga-pass" for _ in range(50))


def test_every_leg_comes_up_over_a_long_run():
    rng = random.Random(2)
    names = list(LEGS)
    seen, current = set(), "vga-pass"
    for _ in range(400):
        current = next_leg(rng, names, current)
        seen.add(current)
    assert seen == set(names)


def test_an_outlier_is_judged_against_its_own_leg_type():
    """Routes read at different levels, so one pooled threshold calls every leg
    of the worse route an outlier and never flags a bad one of the better."""
    assert is_outlier(0.09, [0.09, 0.10, 0.09, 0.11, 0.09, 0.10, 0.09, 0.10]) is False


def test_a_leg_far_above_its_own_type_is_an_outlier():
    assert is_outlier(0.40, [0.09, 0.10, 0.09, 0.11, 0.09, 0.10, 0.09, 0.10]) is True


def test_the_first_legs_of_a_type_are_kept_whatever_they_read():
    """Nothing to compare against yet, and a reference per type is what makes
    the rest of the night readable."""
    assert is_outlier(0.001, [0.09, 0.10]) is True


def test_a_quiet_type_does_not_make_rounding_an_outlier():
    """A floor, or a type whose median is near zero flags every leg above it."""
    assert is_outlier(0.004, [0.001] * 10) is False
