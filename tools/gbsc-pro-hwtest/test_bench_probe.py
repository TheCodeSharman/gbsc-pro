"""bench_probe against a fake register file. No hardware.

The read-modify-write here is the code that touches a live unit. Every geometry
field on this chip is a bitfield sharing its register with something else -- VDS_DIS_HB_SP is 12 bits at offset 4 of a pair whose bottom nibble
belongs to a different control -- so a write that does not preserve its
neighbours corrupts a register nobody was looking at, and the symptom lands
somewhere unrelated. That is the failure documented in docs/preset-load-clobber.md
arriving by a different route.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe


class FakeUnit:
    """An in-memory register file with the same surface as the HTTP one.

    A fake rather than a mock: the assertions are on what ends up in the
    registers, which is what the chip would see, not on which calls were made.
    """

    def __init__(self, registers=None):
        self.registers = dict(registers or {})

    def read(self, segment, register):
        return self.registers.get((segment, register), 0)

    def write(self, segment, register, value):
        self.registers[(segment, register)] = value
        return True


SPEC = ("VDS_DIS_HB_SP", 3, 0x11, 4, 12)


def test_writing_a_field_leaves_its_neighbours_alone():
    unit = FakeUnit({(3, 0x11): 0xAF, (3, 0x12): 0x5C})
    probe = bench_probe.Probe(unit.read, unit.write)

    probe.write_field(SPEC, 0x123)

    assert unit.registers[(3, 0x11)] & 0x0F == 0x0F


def test_a_field_reads_back_what_was_written_to_it():
    unit = FakeUnit({(3, 0x11): 0xAF, (3, 0x12): 0x5C})
    probe = bench_probe.Probe(unit.read, unit.write)

    probe.write_field(SPEC, 0x123)

    assert probe.read_field(SPEC) == 0x123


def test_a_value_too_wide_for_the_field_is_clamped_not_wrapped():
    """Wrapping would silently put the display edge at the far side of the
    raster -- a picture that vanishes rather than an error."""
    unit = FakeUnit()
    probe = bench_probe.Probe(unit.read, unit.write)

    probe.write_field(SPEC, 0xFFFF)

    assert probe.read_field(SPEC) == 0xFFF


def test_a_negative_value_is_clamped_to_zero():
    unit = FakeUnit()
    probe = bench_probe.Probe(unit.read, unit.write)

    probe.write_field(SPEC, -5)

    assert probe.read_field(SPEC) == 0


def test_a_dropped_read_is_reported_rather_than_guessed():
    """A read that fails over HTTP must not come back as a plausible number --
    that is how a geometry value gets recorded from a register nobody read."""
    probe = bench_probe.Probe(lambda segment, register: None,
                              lambda segment, register, value: True)

    assert probe.read_field(SPEC) is None
