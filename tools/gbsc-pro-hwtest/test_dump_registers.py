"""A restore has to make the values it wrote take effect.

`PLLAD_MD` is behind a latch: the ADC PLL keeps running on its old divider until
`PLLAD_LAT` sees a rising edge, so a snapshot restored across a change of divider
leaves the PLL unlocked while every register reads back exactly right -- a black
screen with H sync detected and V sync absent, and a restore reporting every
register verified. `resetPLLAD()` is the sequence that makes it take.

The same applies to the two phase registers, which the datasheet documents the
same way: "this bit's rising edge is used to latch".
"""
import json

import pytest

import dump_registers


class FakeUnit:
    """Register state plus the order it was written in.

    A latch is a rising edge, so the final value cannot show whether it
    happened -- `PLLAD_LAT` reads 1 both before and after. Only the history
    can, which is why this records one.
    """

    def __init__(self):
        self.regs = {}
        self.history = []

    def read(self, segment, reg):
        return self.regs.get((segment, reg), 0)

    def write(self, segment, reg, value):
        self.regs[(segment, reg)] = value
        self.history.append((segment, reg, value))

    def bit_history(self, segment, reg, bit):
        """Every value that bit took, in order, with repeats collapsed."""
        seen = []
        for s, r, v in self.history:
            if (s, r) == (segment, reg):
                b = (v >> bit) & 1
                if not seen or seen[-1] != b:
                    seen.append(b)
        return seen


@pytest.fixture
def unit(monkeypatch):
    import gbs_unit

    fake = FakeUnit()
    monkeypatch.setattr(gbs_unit, "write_reg",
                        lambda host, s, r, v: fake.write(s, r, v))
    monkeypatch.setattr(dump_registers, "read_reg",
                        lambda host, s, r: fake.read(s, r))
    return fake


@pytest.fixture
def snapshot(tmp_path):
    """A snapshot that moves PLLAD_MD -- the case that needs the latch."""
    path = tmp_path / "known-good.json"
    path.write_text(json.dumps({
        "note": "known good",
        "config": {"5:0x12": 0xF9, "5:0x13": 0x09},  # PLLAD_MD = 2553
    }))
    return str(path)


def test_a_restore_latches_the_adc_pll(unit, snapshot):
    """Without the rising edge on PLLAD_LAT the restored divider never loads,
    and the unit sits with H sync and no V sync -- a black screen whose
    registers all read back correct."""
    dump_registers.restore("host", snapshot, segments={5}, repeat=1)

    assert unit.bit_history(5, 0x11, 7)[-2:] == [0, 1], (
        "PLLAD_LAT must fall and rise after the registers are written; "
        f"saw {unit.bit_history(5, 0x11, 7)}")


@pytest.mark.parametrize("reg, name", [(0x18, "PA_ADC_LAT"), (0x19, "PA_SP_LAT")])
def test_a_restore_latches_the_sampling_phases(unit, snapshot, reg, name):
    """A snapshot carries PA_ADC_S and PA_SP_S, and both are behind the same
    kind of rising-edge latch as the divider."""
    dump_registers.restore("host", snapshot, segments={5}, repeat=1)

    assert unit.bit_history(5, reg, 7)[-2:] == [0, 1], (
        f"{name} must fall and rise; saw {unit.bit_history(5, reg, 7)}")


def test_the_bench_source_reads_locked_even_though_its_v_bit_is_clear():
    """STATUS_16 = 0x02 with SP_VTOTAL 308 is the RiscPC's normal working
    state: H active, V active CLEAR, and a perfectly good picture. The old
    indicator required bit 3 and so could never fire on this source -- every
    restore printed as failed, which cost a wrong diagnosis on 2026-08-14."""
    assert dump_registers.is_locked(0x02, 308)


def test_a_sync_processor_counting_nothing_is_not_locked():
    """The black screen after a restore across a change of PLLAD_MD: same
    STATUS_16 as the working state, but the vertical counter reads zero. That
    is the difference the indicator has to show."""
    assert not dump_registers.is_locked(0x02, 0)


def test_a_restore_that_skips_segment_5_does_not_touch_the_latches(unit, tmp_path):
    """The latches all live in segment 5. Restoring only geometry should not
    disturb the ADC PLL, which is exactly the case the default --segments=5
    was never written for."""
    path = tmp_path / "geometry-only.json"
    path.write_text(json.dumps({"note": "", "config": {"3:0x04": 0x3E}}))

    dump_registers.restore("host", str(path), segments={3}, repeat=1)

    assert unit.bit_history(5, 0x11, 7) == [], (
        "a segment-3 restore wrote to the ADC PLL register")
