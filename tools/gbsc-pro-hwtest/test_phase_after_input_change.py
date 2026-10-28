"""What the soak counts as a displaced answer."""

from phase_after_input_change import displaced


SETTLED = (29, 31)


def test_the_settled_bands_own_answers_are_not_displaced():
    for phase in range(SETTLED[0], SETTLED[1] + 1):
        assert not displaced(phase, SETTLED)


def test_the_transient_bands_answers_are_displaced():
    assert displaced(7, SETTLED)
    assert displaced(19, SETTLED)


def test_an_answer_below_the_transient_band_is_displaced_too():
    # SCORED BY ABSENCE FROM THE SETTLED BAND, NOT BY MEMBERSHIP OF THE
    # TRANSIENT ONE. A run on 2026-10-29 read PA_SP_S 4 against a settled band
    # of 29..31 and scored it good, because 4 sits below the 6..19 the transient
    # band was measured at -- so an answer displaced further than any seen
    # before reads as the healthiest kind there is.
    assert displaced(4, SETTLED)
    assert displaced(0, SETTLED)
