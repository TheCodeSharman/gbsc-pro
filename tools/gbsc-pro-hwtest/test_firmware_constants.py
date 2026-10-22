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
