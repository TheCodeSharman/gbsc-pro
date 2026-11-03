"""Every firmware constant the tools read, asked for by name. No hardware.

**THIS IS WHAT A MIRROR COULD NOT DO.** The values used to be copied into
framing_decomposition and the test that was meant to pin them asserted the copy
against itself, so `TransmittedWindowDelayPx` moving to 6 left the mirror at 20
without a failure anywhere. Read out of the C++, a value that moves moves with
it -- and a name the firmware RENAMES fails here, which is the half a mirror
cannot cover however the numbers are compared.

Each name below is one a tool asks for, so this test is the inventory: adding a
read means adding the name here.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import firmware_constants as fc

OUTPUT_MODE_INTEGERS = ("WorkingCeilingHz", "EngineCeilingHz", "EncoderCeilingHz",
                        "MaxHorizontalTotal", "FrontPorchMinPx",
                        "TransmittedWindowDelayPx", "HsyncStartPx",
                        "HorizontalTotalMax")


@pytest.mark.parametrize("name", OUTPUT_MODE_INTEGERS)
def test_every_output_mode_constant_the_tools_read_is_in_the_header(name):
    assert fc.integer(fc.OUTPUT_MODE_H, name) > 0


def test_a_name_the_firmware_does_not_state_is_refused_rather_than_defaulted():
    with pytest.raises(KeyError):
        fc.integer(fc.OUTPUT_MODE_H, "NoSuchConstant")


def test_the_scale_unity_is_the_ten_bit_registers_own():
    assert fc.integer(fc.SCALE_H, "Unity") == 1024


def test_the_output_mode_table_states_a_raster_for_every_mode_offered():
    rows = fc.output_modes()
    assert set(rows) == {"1080p", "1024p", "960p", "720p", "576p", "480p"}
    for name, row in rows.items():
        assert len(row) == 10, name
        assert all(value > 0 for value in row), name


def test_the_write_start_carries_both_axes_and_a_floor():
    constant, per_magnification, floor = fc.write_start("Horizontal")
    assert (constant, per_magnification) > (0.0, 0.0)
    assert floor >= 0
    assert fc.write_start("Vertical") != fc.write_start("Horizontal")


def test_the_axes_differ_in_granularity_and_margin():
    # The horizontal capture position moves in twos and the vertical in ones;
    # the margins are the other way round. Both are read rather than assumed,
    # since a tool that converts a margin needs the firmware's own.
    granularity_h, margin_h = fc.axis("Horizontal")[:2]
    granularity_v, margin_v = fc.axis("Vertical")[:2]
    assert (granularity_h, margin_h) == (2, 1)
    assert (granularity_v, margin_v) == (1, 2)


def test_the_clock_seeds_come_back_in_rate_order_with_a_rate_for_each():
    seeds = fc.clock_seeds()
    assert len(seeds) == 7
    assert list(seeds) == sorted(seeds)
    assert seeds[0] == 40_500_000 and seeds[-1] == 162_000_000


# --- the divider the engine would solve ------------------------------------
#
# The bench RiscPC at 320x256@50: 311 lines from the register, line doubled,
# 4x oversampling. Measured on the unit, solved and latched -- PLLAD_MD 2200
# against STATUS_SYNC_PROC_HTOTAL 2200.
BENCH_LINES, BENCH_RATE, BENCH_OVERSAMPLE = 311, 50.0, 4


def test_a_doubled_bench_line_lands_on_the_capture_path_cap():
    """2200 rather than the 2551 the crossover row affords. The capture path
    stops writing video past sample 2236..2256 of a doubled line, measured with
    the VDS line delay bypassed -- so it is the capture path and not the
    filter. docs/known-issues.md"""
    assert fc.expected_divider(
        BENCH_LINES, BENCH_OVERSAMPLE, BENCH_RATE, doubled=True) == 2200


def test_the_eleven_bit_counter_is_what_holds_an_undoubled_line():
    """Which is why the doubled cap is measured on a doubled line only: at the
    bench rate an undoubled line is held at 2046 by the counter, below the cap,
    so the divider cannot reach it to show the band. The counter takes the RAW
    line, so only the decimation separates it from the divider."""
    counter = fc.integer(fc.INPUT_FORMATTER_H, "LineCounterMax")
    undoubled = fc.expected_divider(BENCH_LINES, BENCH_OVERSAMPLE, BENCH_RATE,
                                    doubled=False)

    assert undoubled <= counter
    assert undoubled < fc.integer(fc.SAMPLING_CLOCK_H, "DoubledLineSampleLimit")


def test_a_doubled_line_may_ask_for_twice_what_the_counter_holds():
    """The counter is fed the DECIMATED line, so doubling buys the divider an
    octave the counter never sees -- which is how it reaches the cap at all."""
    assert fc.expected_divider(BENCH_LINES, BENCH_OVERSAMPLE, BENCH_RATE,
                               doubled=True) > fc.integer(
        fc.INPUT_FORMATTER_H, "LineCounterMax") // 2


def test_a_line_rate_that_was_never_measured_is_not_a_divider():
    """recommendedDivider() returns 0 rather than a default: a divider written
    from a measurement that did not happen takes the sync processor with it."""
    assert fc.expected_divider(0, 4, 50.0, doubled=True) == 0


def test_an_unsigned_constant_is_read_rather_than_refused():
    """`static const uint32_t MaxSampleRateHz = 162000000u;` -- the suffix is
    part of the spelling, and a reader insisting on bare digits refuses the
    name as though the firmware did not state it."""
    assert fc.integer(fc.ADC_H, "MaxSampleRateHz") == 162000000
