"""The state grammar `collapse_legs.py` screens candidates with.

No hardware: the parse is what decides whether a quantity is poked by name or
asked for through the route that survives the engine's re-solve, and getting
that wrong is how a control gets published as a state.
"""

from collapse_legs import fields_of, held_of, tokens_of


def test_default_asks_for_nothing():
    assert tokens_of("default") == []
    assert fields_of("default") == {}
    assert held_of("default", "dlt:") is None
    assert held_of("default", "coast:") is None


def test_a_named_field_is_poked():
    assert fields_of("SP_JITTER_SYNC=1") == {"SP_JITTER_SYNC": "1"}


def test_the_held_quantities_are_not_named_fields():
    """`SP_DLT_REG` written by name is taken back part way through a leg, so the
    threshold has to reach the chip through /sepwidth. A grammar that let it
    through as a poke would read as a state and be a control."""
    state = "dlt:48,coast:0/2"
    assert fields_of(state) == {}
    assert held_of(state, "dlt:") == "48"
    assert held_of(state, "coast:") == "0/2"


def test_a_candidate_rides_on_top_of_the_assay():
    state = "dlt:48,SP_JITTER_SYNC=1"
    assert held_of(state, "dlt:") == "48"
    assert fields_of(state) == {"SP_JITTER_SYNC": "1"}
    assert held_of(state, "coast:") is None
