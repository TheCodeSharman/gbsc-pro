"""What the preset tables agree on, pinned so it cannot drift unnoticed.

    pytest tools/gbsc-pro-hwtest/test_preset_common.py

No hardware: this reads preset_tables.json, the archive of the twelve tables.

The archive exists because the headers are being deleted from the firmware and
every tool that audits them reads them -- deleting them without archiving first
would blind these very checks. test_the_archive_still_matches_the_headers below
pins the archive against the .h files for as long as a checkout has both.

The split these assert is the whole basis of docs/investigations/preset-abandonment-audit.md and
of the bring-up block that replaces preset loading. If a table is edited, or the
parser silently stops seeing part of one, the bring-up is built from a different
set of registers than the one that was reviewed -- and nothing else would say so.
"""

import pytest

from preset_common import (ARCHIVED_PRESETS, SCALING_PRESETS, headers_present,
                           read_table, read_table_from_header, split)

# The five ranges one writeProgramArrayNew() call covers, from the audit.
PRESET_RANGES = [
    (0, 0x40, 0x5F), (0, 0x90, 0x9F),
    (1, 0x00, 0x2F),
    (3, 0x00, 0x7F),
    (4, 0x00, 0x5F),
    (5, 0x00, 0x6F),
]
PRESET_VALUE_COUNT = 432  # matches presetRegisterRanges[] in gbs-control.ino


@pytest.fixture(scope="module")
def presets():
    """All TWELVE tables, from the archive -- not the ten the sketch still has.

    Every finding these tests pin is a statement about what SHIPPED, and
    docs/investigations/preset-abandonment-audit.md rests on it. Recomputing over
    a shrinking set silently restates the finding about a different population:
    dropping the two downscale tables takes the agreed count from 306 to 330,
    because they are among the most divergent.

    test_the_archive_still_matches_the_headers is the one that asks about the
    working tree, and it uses SCALING_PRESETS for exactly that reason.
    """
    return {name: read_table(name) for name in ARCHIVED_PRESETS}


def test_every_scaling_preset_covers_exactly_the_documented_ranges(presets):
    """The address set, not just the count.

    This is the test that would have caught the parser bug on 2026-08-13: the
    low sixteen bytes of each segment are labelled with ONE hex digit
    (`// s1_0`), the rest with two, and a two-digit-only pattern dropped exactly
    16 bytes from each of segments 1, 3, 4 and 5. Segment 0 starts at 0x40, so
    it was the one segment that still came out whole -- and what remained parsed
    cleanly and produced a plausible-looking split. A count alone is weak here;
    a wrong 432 is far less likely than a wrong 368.
    """
    expected = {
        (segment, register)
        for segment, low, high in PRESET_RANGES
        for register in range(low, high + 1)
    }
    assert len(expected) == PRESET_VALUE_COUNT

    for name in SCALING_PRESETS:
        assert set(presets[name]) == expected, (
            f"{name}.h does not label exactly the preset ranges: "
            f"missing {sorted(expected - set(presets[name]))}, "
            f"unexpected {sorted(set(presets[name]) - expected)}"
        )


def test_most_of_a_preset_is_not_a_preset(presets):
    """306 of 432 bytes are identical in all twelve tables.

    That is the finding the whole abandonment rests on: a "preset" is mostly
    static chip bring-up, and only the minority that disagrees is genuinely
    mode-dependent. It reconciles with the audit's own split --
    306 + 126 = 432 = 203 + 209 + 20, where the 203 are what the engine and the
    sketch already write at runtime.
    """
    agreed, varying = split(presets)
    assert len(agreed) == 306
    assert len(varying) == 126
    assert len(agreed) + len(varying) == PRESET_VALUE_COUNT


def test_the_agreed_set_never_disagrees_with_any_table(presets):
    """The property the bring-up depends on, checked directly rather than via
    the counts: every address it will freeze really does hold that value in
    every one of the twelve tables. A bring-up that froze a value some mode
    never asked for would be a silent mode-specific regression."""
    agreed, _ = split(presets)
    for address, value in agreed.items():
        for name in SCALING_PRESETS:
            assert presets[name][address] == value, (
                f"s{address[0]}_{address[1]:02x} is {presets[name][address]:#04x} in "
                f"{name} but the agreed set claims {value:#04x}"
            )


@pytest.mark.skipif(not headers_present(),
                    reason="the twelve preset headers are gone; the archive is the record now")
@pytest.mark.parametrize("name", SCALING_PRESETS)
def test_the_archive_still_matches_the_headers(name):
    """preset_tables.json is what the .h files say, byte for byte.

    The archive is generated from the headers, and the headers are leaving the
    firmware. While a checkout has both, this is what says the copy is faithful:
    an archive that has silently drifted makes every downstream audit wrong in a
    way nothing else can detect, because the thing it is wrong about no longer
    exists to compare against.

    Skips once the headers are gone, which is the intended end state rather than
    a gap -- at that point the archive IS the record.
    """
    assert read_table(name) == read_table_from_header(name), (
        f"{name}: preset_tables.json disagrees with {name}.h -- regenerate it "
        f"with `preset_common.py --archive` and check the diff is intended")
