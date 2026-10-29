"""What the soak counts as a harmful answer."""

from phase_after_input_change import HARMFUL, harmful


def test_the_harmful_band_is_harmful():
    for phase in range(HARMFUL[0], HARMFUL[1] + 1):
        assert harmful(phase, HARMFUL)


def test_the_good_arc_runs_THROUGH_ZERO_and_costs_nothing():
    # Peak grating contrast over two acquisitions at 1280x720@60 pass-through,
    # one PA_ADC_S sweep per sync phase: PA_SP_S 30, 0 and 4 all reach 53.3
    # where 11 reaches 27.8 and 16 reaches 32.2. So the quiet family is an arc
    # through zero and a phase below the harmful band is not a displaced
    # answer -- scoring by absence from the settled 29..31 called 0 and 4
    # failures, which is how a run of good arrivals read as bad.
    # docs/investigations/the-sync-phase-sets-the-adc-families-ceiling.md
    for phase in (0, 4, 23, 30, 31):
        assert not harmful(phase, HARMFUL)


def test_the_boundary_phase_is_not_counted_against_the_pick():
    # 6 reaches 52.9 against 30's 53.3, so it is inside the arc; 7 reaches
    # 50.8 and is the first value the sweep's middle lands on when the board
    # is displaced.
    assert not harmful(6, HARMFUL)
    assert harmful(7, HARMFUL)
