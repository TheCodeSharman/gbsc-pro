"""measure_produced's register setup, against a fake register file. No hardware.

The creeping is the user's eye and cannot be tested. Everything it creeps FROM
can be, and has to be: `produced` is reported as `edge - corner`, so a corner
placed from the wrong raster, or from a raster read after it was needed, does not
fail loudly -- it returns a plausible number that is wrong by however far the
corner moved. That is the exact failure mode docs/scaler-geometry-model.md was
written about, arriving through the tool that measured it.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe
import measure_produced

from test_bench_probe import FakeUnit

# The bench raster. VDS_HSYNC_RST counts from zero like every other counter on
# this chip, so the register holds one less than the total.
RASTER_TOTAL = 1716

# One point from the matrix, worked through by hand so the test knows the answer
# independently of the code under test:
#
#     produced = 798 x 1024 / 1023               = 798.780
#     corner   = (1716 - 798.780) / 2            = 459
#     offset   = 55 + 25 x (1024 / 1023)         =  80.024
#     window_sp = 459 - 80.024                   = 379
CAPTURE, SCALE = 798, 1023
CORNER, WINDOW_SP = 459, 379


def a_frozen_unit(raster_total=RASTER_TOTAL):
    """A register file with nothing in it but the raster, which is the one thing
    `measure` must read rather than assume."""
    unit = FakeUnit()
    bench_probe.Probe(unit.read, unit.write).write_field(
        measure_produced.FIELDS["h"]["rst"], raster_total - 1)
    return unit


@pytest.fixture
def bench():
    unit = a_frozen_unit()
    return bench_probe.Probe(unit.read, unit.write)


def creep_stops_at(monkeypatch, edge):
    """Stand in for the eye. The human half of this tool is the one thing here
    that genuinely cannot be exercised without a bench and a screen."""
    monkeypatch.setattr(measure_produced, "creep_edge",
                        lambda *args, **kwargs: edge)


def test_the_picture_is_centred_on_the_raster_the_unit_reports(bench, monkeypatch):
    creep_stops_at(monkeypatch, CORNER + 799)

    measure_produced.measure(bench, "h", CAPTURE, SCALE)

    assert bench.read_field(measure_produced.FIELDS["h"]["dis_sp"]) == CORNER


def test_the_memory_window_puts_the_write_start_on_that_corner(bench, monkeypatch):
    """The corner is only where the picture begins if VDS_HB_SP is set so that
    `origin_offset(m)` after it lands there. Inheriting a window instead is what
    put 41 px of the previous frame down the left of the screen on 2026-08-05."""
    creep_stops_at(monkeypatch, CORNER + 799)

    measure_produced.measure(bench, "h", CAPTURE, SCALE)

    assert bench.read_field(measure_produced.FIELDS["h"]["win_sp"]) == WINDOW_SP


def test_produced_is_the_edge_measured_from_the_write_start(bench, monkeypatch):
    creep_stops_at(monkeypatch, CORNER + 799)

    assert measure_produced.measure(bench, "h", CAPTURE, SCALE) == 799


def test_a_raster_that_will_not_read_gives_no_measurement(monkeypatch):
    """Rather than a plausible number taken from a register nobody read. The
    unit drops HTTP reads under load, and this tool runs a six-point matrix."""
    unreachable = bench_probe.Probe(lambda segment, register: None,
                                    lambda segment, register, value: True)
    creep_stops_at(monkeypatch, 1258)

    assert measure_produced.measure(unreachable, "h", CAPTURE, SCALE) is None
