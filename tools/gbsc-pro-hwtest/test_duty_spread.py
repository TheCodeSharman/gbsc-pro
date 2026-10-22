"""Unit tests for duty_spread's pure parts -- `pytest test_duty_spread.py`.

No unit needed: what is worth pinning is the tearing rule and the pulse/
complement choice, both of which decide whether a reading is counted at all.
"""

from duty_spread import source_samples, torn


def test_a_pulse_narrower_than_its_complement_is_the_pulse():
    assert source_samples(166, 2200) == 166


def test_a_high_active_source_reports_the_pulse_and_not_the_line_minus_it():
    # Counted through the inversion the register holds the line MINUS the
    # pulse, which is the complement. The narrower of the two is the pulse.
    assert source_samples(2200 - 166, 2200) == 166


def test_a_reading_at_or_past_the_line_is_left_alone():
    # The vertical interval's broad pulse lands in the same register on
    # composite and exceeds the line, so there is no complement to take. It is
    # excluded by the pulse band rather than by being folded into one.
    assert source_samples(2730, 2200) == 2730


def test_a_value_whose_two_bytes_are_each_another_reading_s_is_torn():
    # The 2x2 crossing: 0x099 and 0xA65 are the genuine pair, and the two
    # crossings appear because the second byte is clocked out after the first.
    seen = {0x099: 100, 0xA65: 100, 0x065: 3, 0xA99: 2}
    assert torn(0x065, seen)
    assert torn(0xA99, seen)


def test_the_genuine_pair_is_not_torn():
    seen = {0x099: 100, 0xA65: 100, 0x065: 3, 0xA99: 2}
    assert not torn(0x099, seen)
    assert not torn(0xA65, seen)


def test_a_lone_excursion_sharing_no_byte_is_not_torn():
    # 156 beside 166 and 165: its low byte 0x9C belongs to no other reading, so
    # it is a real measurement rather than a crossing, and excluding it would
    # hide a genuine excursion.
    seen = {166: 1133, 156: 3, 165: 1}
    assert not torn(156, seen)
