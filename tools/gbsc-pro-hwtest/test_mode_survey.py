"""mode_survey's arithmetic. No hardware."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mode_survey


def test_the_line_rate_is_measured_against_the_chips_own_27_mhz():
    """The same conversion Tv5725::SamplingLog::lineRateFromHPeriod does, and
    the reason HPERIOD_IF can be trusted while the ADC divider is wrong: it owes
    nothing to the ADC PLL."""
    assert round(mode_survey.line_rate(431)) == 15625


def test_a_hperiod_of_zero_is_not_a_line_rate():
    """0 is a rail, not a measurement, and dividing by it would make one up."""
    assert mode_survey.line_rate(0) == 0.0
