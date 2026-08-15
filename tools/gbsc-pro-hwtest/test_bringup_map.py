"""What the bring-up block is derived to write, pinned so the derivation cannot
silently shrink.

    pytest tools/gbsc-pro-hwtest/test_bringup_map.py

No hardware: this reads the preset headers and tv5725.h.

test_preset_common.py pins the SPLIT -- which addresses the twelve tables agree
on. This pins what bringup_map.py makes of it, which is a different question,
because the block does not write addresses. It writes FIELDS.
"""

import os
import re

import pytest

import bringup_map
import preset_common as pc

BRINGUP_DIR = os.path.join(bringup_map.SKETCH, "src", "tv5725", "bringup")


@pytest.fixture(scope="module")
def fields():
    return {f[1]: f for f in bringup_map.bringup_fields()}


@pytest.fixture(scope="module")
def tables():
    return {name: pc.read_table(name) for name in pc.SCALING_PRESETS}


def field_value(table, seg, reg, lo, width):
    raw = 0
    for i in range((lo + width + 7) // 8):
        byte = table.get((seg, reg + i))
        if byte is None:
            return None
        raw |= byte << (8 * i)
    return (raw >> lo) & ((1 << width) - 1)


def test_a_write_from_a_bypass_path_is_not_counted_as_ownership():
    """The filter that cost step 4 thirty-four bytes, asked directly.

    "The firmware writes it somewhere" was the old test and it excluded fields
    whose only writer puts the chip into low power or one of the two bypass
    modes -- states a scaling preset has to bring it BACK from. Excluded from
    the block for having an owner, and written by nobody on the scaling path,
    those fields had no owner at all once the preset tables went. CLAUDE.md's
    yellow-tint fault, DAC_RGBS_B0ENZ, is the same shape and predates step 4.

    **ASKED OF THE ATTRIBUTION RATHER THAN OF A LIVE FIELD, DELIBERATELY.** An
    earlier version named DAC_RGBS_B0ENZ and asserted it was in the block; that
    stopped being true the moment Tv5725::Chip::init() took it, which is the
    graduation working as designed. A test that has to be rewritten by every
    correct change is testing the wrong thing. The rule is what is stable.
    """
    source = """
void bypassModeSwitch_RGBHV()
{
    GBS::ONLY_BYPASS_WRITES_THIS::write(1);
}

void doPostPresetLoadSteps()
{
    GBS::THE_SCALING_PATH_WRITES_THIS::write(1);
}
"""
    attributed = dict((name, function)
                      for function, name in bringup_map.writes_by_function(source))

    assert attributed["ONLY_BYPASS_WRITES_THIS"] in bringup_map.NON_OWNING
    assert attributed["THE_SCALING_PATH_WRITES_THIS"] not in bringup_map.NON_OWNING


def test_a_brace_inside_a_comment_does_not_move_a_write_to_another_function():
    """Attribution runs over source with comments and literals blanked.

    Unstripped, gbs-control.ino desynchronised within a few hundred lines and
    every write after that point landed in the wrong function or in none. The
    failure is silent in both directions: a scaling-path write attributed to a
    bypass function would put a contested field INTO the static block, which is
    the one outcome this filter must never produce by accident.
    """
    source = """
void bypassModeSwitch_RGBHV()
{
    // an unbalanced { in a comment
    const char *s = "and another { in a string";
    GBS::STILL_THE_BYPASS_S::write(1);
}

void doPostPresetLoadSteps()
{
    GBS::STILL_THE_SCALING_PATH_S::write(1);
}
"""
    attributed = dict((name, function)
                      for function, name in bringup_map.writes_by_function(source))

    assert attributed["STILL_THE_BYPASS_S"] == "bypassModeSwitch_RGBHV"
    assert attributed["STILL_THE_SCALING_PATH_S"] == "doPostPresetLoadSteps"


def test_a_field_the_tables_disagree_on_is_not_the_bringup_s(fields, tables):
    """The other side of the same rule, and the one that keeps it honest.

    IF_HB_ST1 is 0x440, 0x448 or 0x450 depending on the table. Freezing any one
    of those into a static block writes a value some mode never asked for.
    """
    assert len({field_value(t, 1, 0x14, 0, 11) for t in tables.values()}) > 1

    assert "IF_HB_ST1" not in fields


def test_every_field_the_block_writes_is_agreed_by_all_twelve_tables(fields, tables):
    """The general form, over the whole derived set.

    bringup_map reads each value from ONE reference table (pal_1920x1080). That
    is only sound if every other table holds the same value, and nothing else
    checks it -- a widened filter that let a varying field through would take
    the reference's value and look entirely normal.
    """
    disagreed = []
    for name, (_, _, seg, reg, lo, width, value) in fields.items():
        seen = {field_value(t, seg, reg, lo, width) for t in tables.values()}
        if seen != {value}:
            disagreed.append(f"{name} (s{seg}_{reg:02x}) = {value:#x}, tables hold "
                             f"{sorted(hex(v) for v in seen if v is not None)}")

    assert not disagreed, "\n".join(disagreed)


def test_the_block_writes_nothing_the_firmware_also_writes_at_runtime(fields):
    """Two owners for one field is the competing-write problem the engine work
    exists to remove, and the block must not add new instances of it.
    """
    runtime = bringup_map.runtime_written()

    assert sorted(set(fields) & runtime) == []


def test_no_bits_have_two_owners_under_two_names(fields):
    """The same rule asked in BITS, which is the only way it is really true.

    Comparing names cannot see an alias, and an alias is precisely how a bit
    acquires two owners without anyone noticing. Until 2026-08-13 the header
    declared s0_58[0] twice -- INT_RST_0 and INT_CONTROL_RST_SOGBAD -- and the
    block wrote one while the sketch wrote the other. The by-name test above
    passed the whole time. Four bits were in that state: s0_58[0], [1], [4] and
    the whole of s4_00.

    Collapsing the aliases resolved all four by making the field runtime-owned,
    so the block correctly stopped writing them: the interrupt resets are
    pulses that rest at 0, and ResetSDRAM() sets MEM_INI_REG from
    doPostPresetLoadSteps() on every preset load.

    EQUAL SPANS ONLY. A block field strictly INSIDE a runtime-written one is
    the whole-byte-convenience-name problem -- 71 of those, e.g. PAD_BLK_OUT_ENZ
    inside PAD_CONTROL_01_0x49 -- and removing them is a separate campaign
    Michael has scoped for later. Asserting on equal spans catches a genuine
    double-owner today without pre-empting that decision.
    """
    runtime = bringup_map.runtime_written()
    by_span = {}
    for seg, reg, lo, width, name in bringup_map.header_fields():
        by_span.setdefault((seg, reg, lo, width), []).append(name)

    contested = []
    for span, names in sorted(by_span.items()):
        owners = [n for n in names if n in fields or n in runtime]
        if len(owners) > 1:
            seg, reg, lo, width = span
            contested.append(f"s{seg}_{reg:02x}[{lo + width - 1}:{lo}] {sorted(owners)}")

    assert not contested, "\n".join(contested)


def test_no_field_the_block_writes_shadows_another(fields):
    """Overlapping names are resolved, not both written.

    Where two names cover the same bits the datasheet's wins, so the block never
    writes both ADC_TEST and ADC_TEST_0C_BIT1 -- which would make the result
    depend on which line landed last. ccbdf5e is why this is a test: the
    derivation emitted `GBS::AIN::write()` for a year before anyone read it.
    """
    overlapping = []
    spans = [(f[2], f[3], f[4], f[5], f[1]) for f in fields.values()]
    for seg, reg, lo, width, name in spans:
        for seg2, reg2, lo2, width2, name2 in spans:
            if name >= name2 or (seg, reg) != (seg2, reg2):
                continue
            if lo < lo2 + width2 and lo2 < lo + width:
                overlapping.append(f"{name} and {name2} both cover s{seg}_{reg:02x}")

    assert not overlapping, "\n".join(overlapping)


def test_a_disqualified_field_is_not_reported_as_a_gap():
    """CAP_FF_STATUS is a status register the preset writes 0x1f into. Cleared
    to 0 on the bench with the picture unchanged, so the bring-up will not carry
    it and it must stop counting against the preset gap.
    """
    fields, _ = bringup_map.unowned()

    assert "CAP_FF_STATUS" not in {f[4] for f in fields}


def test_every_disqualification_still_applies_to_something():
    """The anti-graveyard guard, and the reason this is a dict and not a comment.

    A disqualification is only meaningful while the preset still writes the
    thing. Once the tables go, or once a field acquires a real owner, an entry
    here is stale -- and a stale exclusion list is indistinguishable from a
    thorough one until someone re-derives it by hand. So every name must still
    be a field the twelve tables disagree on, and every hole must still be an
    address whose unnamed bits some table sets.
    """
    stale = bringup_map.stale_disqualifications()

    assert stale == [], (
        f"these no longer describe anything the preset does: {stale}. Remove "
        "them rather than leaving the list looking more considered than it is.")


def test_the_derivation_is_empty_because_every_field_has_an_owner(fields):
    """**THE INVARIANT THAT OUTLIVES THE GENERATOR, and the one step 4 needs.**

    bringup_fields() is "what the twelve tables agree on, minus what a
    scaling-path writer owns". Every subsystem has graduated, so it comes out
    empty -- and a NON-empty answer now means a field the tables agree on that
    nothing will write once the tables are deleted. That is the precise shape of
    the fault that cost step 4 thirty-four bytes.

    It replaces the orphan guard, which asked whether a derived field belonged
    to a class that had already graduated. With all nine graduated, "belongs to
    a graduated class" and "exists at all" are the same question, and this is
    the simpler way to ask it.

    A field that legitimately has no owner yet belongs in `unowned()` and the
    --gap report, not here: that path is about fields the tables DISAGREE on,
    which a static value cannot serve.
    """
    assert sorted(fields) == []


def test_the_subsystem_list_comes_from_BringUp_cpp_not_from_a_copy():
    """Which classes are the bring-up is read out of the code that calls them.

    A hand-kept list here would drift the day a tenth subsystem is added, and
    drift silently: the new class's registers would simply be treated as
    somebody else's and drop out of the hardware check.
    """
    subsystems = bringup_map.bringup_subsystems()

    assert "Chip" in subsystems and "VideoProcessor" in subsystems
    assert len(subsystems) >= 9, (
        f"BringUp::init() calls {len(subsystems)} subsystems, which is fewer "
        "than the nine that graduated -- the parse has broken")


def test_a_field_only_the_bringup_writes_is_not_counted_as_overwritten():
    """The filter that made the old hardware test unfalsifiable, asked directly.

    **runtime_written() CANNOT ANSWER THIS ANY MORE, AND THAT IS WHY THE TEST
    IT FED DIED.** It skips src/tv5725/bringup/, a directory that no longer
    exists: the nine classes graduated into src/tv5725/*.cpp, which it globs.
    So every field the bring-up writes counts as "written at runtime", by the
    bring-up itself, and bringup_fields() correctly returns nothing --
    test_the_derivation_is_empty is the guard for that and it is the end state,
    not a fault.

    What the hardware still needs to know is different and narrower: of the
    registers the bring-up writes, which does something LATER write over? Only
    those may disagree with the block when read back off a live chip.
    """
    overwritten = bringup_map.written_outside_bringup()

    # PLLAD_MD is Tv5725::Sampling's, written on every solve, so a read-back
    # will not match anything the bring-up left.
    assert "PLLAD_MD" in overwritten
    # VDS_PK_HH_GAIN is a peaking filter coefficient in Tv5725::Vds and nothing
    # else touches it -- exactly the silent class the hardware check exists for.
    assert "VDS_PK_HH_GAIN" not in overwritten
