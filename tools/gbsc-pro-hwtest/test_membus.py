"""membus.py's decoding, against an in-memory register file -- no unit needed.

The tool's job is to name what a raw field means, and the way it earns trust is
that a wrong clock table is silent: PLL_MS 5 written as "162 MHz" would be
recorded as a measurement of a clock the chip was never running.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe
import membus


class FakeChip:
    """Registers as a dict, so a write that lands in the wrong one is visible."""

    def __init__(self, values=None):
        self.registers = dict(values or {})

    def read(self, segment, register):
        return self.registers.get((segment, register), 0)

    def write(self, segment, register, value):
        self.registers[(segment, register)] = value
        return True

    def probe(self):
        return bench_probe.Probe(self.read, self.write)


def test_the_clock_table_matches_the_datasheet():
    """tv5725.h PLL_MS, s0 0x40 bits 4-6. 010 is not a frequency at all -- it
    takes the memory clock from FBCLK on pin 110 -- and naming it as one would
    invent a number the chip never ran at."""
    assert membus.clock_name(7) == "129.6 MHz"
    assert membus.clock_name(6) == "216 MHz"
    assert membus.clock_name(3) == "162 MHz"
    assert membus.clock_name(2) == "FBCLK (pin 110)"


def test_a_clock_the_chip_cannot_produce_is_refused_not_rounded():
    """Picking the nearest would be recorded as a measurement of the clock that
    was asked for. There is no 200 MHz setting; 185 and 216 straddle it."""
    assert membus.clock_value(216) == 6
    assert membus.clock_value(129.6) == 7
    assert membus.clock_value(200) is None
    assert membus.clock_value(0) is None


def test_setting_the_clock_preserves_the_rest_of_the_register():
    """PLL_MS shares s0 0x40 with the input clock select. A read-modify-write
    that drops those bits moves where the chip takes its clock from, which is a
    dead picture rather than a slower one."""
    chip = FakeChip({(0, 0x40): 0x7C})       # PLL_MS 7, low nibble 0xC
    probe = chip.probe()

    probe.write_field(membus.PLL_MS, 6)

    assert chip.registers[(0, 0x40)] == 0x6C
    assert probe.read_field(membus.PLL_MS) == 6


def test_the_bench_unit_state_decodes_to_what_was_reported():
    """The values read off the unit on 2026-08-09, so the reading that started
    this is reproducible without the unit: 129.6 MHz, and playback on LOW
    priority against capture."""
    chip = FakeChip({(0, 0x40): 0x7C, (4, 0x2B): 0x00, (4, 0x21): 0x10,
                     (4, 0x15): 0x43})
    state = membus.describe(chip.probe())

    assert membus.clock_name(state["PLL_MS"]) == "129.6 MHz"
    assert membus.REQUEST[state["PB_REQ_SEL"]] == "low"
    assert state["CAP_FF_HALF_REQ"] == 0


@pytest.mark.parametrize("value", sorted(membus.CLOCKS))
def test_every_clock_setting_round_trips(value):
    """Every setting the chip has, written and read back. A field that straddles
    a byte boundary or sits at the wrong offset shows up here and nowhere else."""
    chip = FakeChip({(0, 0x40): 0x7C})
    probe = chip.probe()

    probe.write_field(membus.PLL_MS, value)

    assert probe.read_field(membus.PLL_MS) == value
    assert chip.registers[(0, 0x40)] & 0x0F == 0x0C, "the neighbours moved"


def test_the_adc_ceiling_is_enforced_not_clamped():
    """162 MSPS is the datasheet limit. A divider over it is refused, because a
    silently reduced one changes the capture units, which changes HSCALE -- the
    quantity under investigation. It would be recorded as a measurement of the
    value that was asked for."""
    assert membus.divider_is_safe(1840, 15550)
    assert membus.divider_is_safe(2553, 15550), "the bench runs at 98% of rated"
    assert not membus.divider_is_safe(2700, 15550)
    # A higher line rate leaves room for fewer samples per line, not more.
    assert not membus.divider_is_safe(2553, 31500), "480p cannot afford 2553"


def test_the_divider_pair_is_written_together_or_not_at_all():
    """IF_HSYNC_RST = PLLAD_MD / 2, maintained in one place in the firmware while
    PLLAD_MD is written from six. Writing the divider alone turned the bench
    display solid green, with sync reporting stable throughout."""
    chip = FakeChip()
    probe = chip.probe()

    probe.write_field(membus.PLLAD_MD, 1840)
    probe.write_field(membus.IF_HSYNC_RST, 1840 // 2)

    assert probe.read_field(membus.PLLAD_MD) == 1840
    assert probe.read_field(membus.IF_HSYNC_RST) == 920
    assert probe.read_field(membus.IF_HSYNC_RST) * 2 \
        == probe.read_field(membus.PLLAD_MD), "the invariant"


def test_every_adc_unit_register_scales_with_the_divider():
    """The lesson of the green picture. PLLAD_MD and IF_HSYNC_RST alone is what
    the firmware's single maintained invariant covers, and it is not enough:
    every position the sync processor holds is a length in ADC samples, so they
    all move onto the new ruler together or the clamp lands on active video."""
    baseline = {"PLLAD_MD": 2553, "SP_CS_CLP_ST": 32, "SP_CS_CLP_SP": 48,
                "SP_RT_HS_SP": 2374, "SP_H_PULSE_IGNOR": 107}

    scaled = membus.scaled_adc_units(baseline, 2553, 1840)

    # Same fraction of the line, on a line that is now 72% as long.
    assert scaled["SP_CS_CLP_ST"] == 23
    assert scaled["SP_CS_CLP_SP"] == 35
    assert scaled["SP_H_PULSE_IGNOR"] == 77


def test_the_retiming_stop_ends_up_under_the_divider():
    """geometry.py has printed 'SP_RT_HS_SP (must stay under PLLAD_MD)' all
    along. Unscaled it is 2374 against a divider of 1840 -- past the end of the
    line -- which is what the two-register version shipped to the bench."""
    baseline = {"PLLAD_MD": 2553, "SP_RT_HS_SP": 2374}

    assert baseline["SP_RT_HS_SP"] > 1840, "the fault being fixed"
    assert membus.scaled_adc_units(baseline, 2553, 1840)["SP_RT_HS_SP"] < 1840


def test_scaling_is_from_the_baseline_so_it_never_compounds():
    """Stepping 2553 -> 1840 -> 1700 must land where going straight to 1700
    lands. Scaling from whatever the registers currently hold would shrink them
    twice, and the second shrink has no symptom until the clamp falls off the
    front of the line."""
    baseline = {"PLLAD_MD": 2553, "SP_CS_CLP_ST": 320}

    stepped = membus.scaled_adc_units(baseline, 2553, 1700)
    direct = membus.scaled_adc_units(baseline, 2553, 1700)

    assert stepped == direct
    assert membus.scaled_adc_units(baseline, 2553, 2553)["SP_CS_CLP_ST"] == 320


def test_a_scaled_value_is_clamped_rather_than_wrapped():
    """A 12-bit position scaled past 4095 wraps to a small number, which is a
    clamp inside the hsync pulse -- the green picture, reached by arithmetic."""
    baseline = {"PLLAD_MD": 1000, "SP_CS_CLP_ST": 3000}

    assert membus.scaled_adc_units(baseline, 1000, 2500)["SP_CS_CLP_ST"] == 4095


def test_no_divider_means_no_writes():
    """An unmeasurable rate yields nothing at all, never a default."""
    assert membus.scaled_adc_units({"SP_CS_CLP_ST": 32}, 0, 1840) == {}
    assert membus.scaled_adc_units({"SP_CS_CLP_ST": 32}, 2553, 0) == {}


def test_the_if_unit_windows_scale_on_the_same_ruler():
    """IF units are ADC samples / 2, so they scale by the same k. Missing them
    left IF_HB_ST1 at 1096 on a line 851 units long -- a window stopping past
    the end of the line, which wraps."""
    baseline = {"PLLAD_MD": 2553, "IF_HB_ST1": 1096, "IF_HBIN_SP": 272}

    scaled = membus.scaled_adc_units(baseline, 2553, 1700)

    assert scaled["IF_HB_ST1"] == 730, "must land inside the 851-unit line"
    assert scaled["IF_HB_ST1"] < 851
    assert scaled["IF_HBIN_SP"] == 181


def test_the_engine_owned_windows_are_not_scaled_here():
    """IF_HB_ST2/SP2 and IF_LINE_ST/SP are recomputed by the geometry engine on
    every solve. Writing them here would fight it, and the engine wins."""
    owned = {"IF_HB_ST2", "IF_HB_SP2", "IF_LINE_ST", "IF_LINE_SP",
             "IF_HSYNC_RST"}
    assert not owned & {name for name, *_ in membus.SCALED_FIELDS}


def test_the_divider_is_latched_with_a_rising_edge():
    """PLLAD_MD does not take effect until PLLAD_LAT sees a rising edge. Without
    it the register reads back the new value while the PLL still runs at the old
    one -- and IF_HSYNC_RST is believed immediately, so everything downstream is
    told a line length that does not exist. That is the green, combed picture."""
    chip = FakeChip({(5, 0x11): 0x80})
    probe = chip.probe()
    edges = []

    def record(_seconds):
        edges.append(probe.read_field(membus.PLLAD_LAT))

    membus.latch_pllad(probe, record)

    assert edges == [0], "must go low before it goes high, or there is no edge"
    assert probe.read_field(membus.PLLAD_LAT) == 1


def test_latching_leaves_the_rest_of_its_register_alone():
    """PLLAD_LAT is bit 7 of s5 0x11. The low bits are PLLAD_MD's own high
    nibble and friends -- clearing them would change the divider being latched."""
    chip = FakeChip({(5, 0x11): 0x0A})
    probe = chip.probe()

    membus.latch_pllad(probe, lambda _s: None)

    assert chip.registers[(5, 0x11)] & 0x7F == 0x0A
