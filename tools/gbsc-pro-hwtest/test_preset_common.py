"""What the preset tables agree on, pinned so it cannot drift unnoticed.

    pytest tools/gbsc-pro-hwtest/test_preset_common.py

No hardware: this reads the preset headers in the sketch.

The split these assert is the whole basis of docs/investigations/preset-abandonment-audit.md and
of the bring-up block that replaces preset loading. If a table is edited, or the
parser silently stops seeing part of one, the bring-up is built from a different
set of registers than the one that was reviewed -- and nothing else would say so.
"""

import pytest

from preset_common import SCALING_PRESETS, read_table, split

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
    return {name: read_table(name) for name in SCALING_PRESETS}


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
