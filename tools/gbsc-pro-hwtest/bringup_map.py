#!/usr/bin/env python3
"""What a static bring-up block has to write, resolved to field names.

    python3 tools/gbsc-pro-hwtest/bringup_map.py
    python3 tools/gbsc-pro-hwtest/bringup_map.py --subsystem VDS_

The plan this serves is docs/investigations/preset-abandonment-audit.md: twelve preset tables
collapse to one static bring-up block, and the block should be *named* register
writes rather than a byte array, so the code says what it is configuring.
Michael, 2026-08-13: "ideally we'd have classes and methods for each tv5725
subsystem as part of the engine. This would let us write self documenting code."

This computes the set. Three filters, and the middle one is the whole point.

1.  **What every scaling table agrees on.** preset_common.split() -- an address
    only counts when all twelve wrote it and wrote it identically.

2.  **Minus what the firmware already writes at runtime.** This is the filter
    that is easy to miss and expensive to miss. Of the 452 named fields inside
    the agreed set, **136 are written at runtime** by the sketch or the engine,
    and a bring-up that also wrote them would either fight them or clobber
    state that is not the chip's at all:

        GBS_OPTION_SCANLINES_ENABLED     s1_2c[0]
        GBS_OPTION_SCALING_RGBHV         s1_2c[1]
        GBS_OPTION_PALFORCED60_ENABLED   s1_2c[2]
        GBS_RUNTIME_FTL_ADJUSTED         s1_2c[4]

    Those four are **gbs-control's own option storage** parked in a spare
    register -- not TV5725 configuration in any sense. Writing them from a
    bring-up resets the user's scanlines, RGBHV-scaling and PAL-60 preferences
    every mode change. IF_LINE_ST is the same shape of mistake: Engine::write()
    owns it, so the bring-up must not.

3.  **Minus everything undocumented.** Michael: "if they are truly undefined we
    have no business touching them at all." Addresses past the end of the
    documented register set, and undocumented holes inside it, are dropped
    rather than reproduced. Note that "every table writes 0x00" is NOT proof the
    reset default is 0 -- if one of them defaults non-zero, not writing it
    changes behaviour, and the bench is where that shows up.

Names come from tv5725.h, not from the datasheet extraction: the header is what
the firmware writes through, and CLAUDE.md's rule is that where the two disagree
the header wins. As of 2026-08-13 the header is COMPLETE with respect to
RD-5725-1.1 -- of the bring-up bytes it does not fully name, zero have datasheet
bits it is missing. What is left unnamed is undocumented, not unextracted.
"""

import argparse
import collections
import glob
import json
import os
import re
import sys

import preset_common as pc

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code", "gbs-control")
HEADER = os.path.join(SKETCH, "tv5725.h")

DECL = re.compile(
    r"UReg<\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*>"
    r"\s*([A-Za-z_][A-Za-z0-9_]*)"
)
WRITE = re.compile(r"GBS::([A-Za-z_][A-Za-z0-9_]*)::write")

SUBSYSTEM = [
    ("VDS_", "video display scaler"),
    ("MEM_", "SDRAM bus"),
    ("SP_", "sync processor"),
    ("ADC_", "ADC"),
    ("PLLAD", "ADC PLL"),
    ("PLL", "display PLL"),
    ("IF_", "input formatter"),
    ("CAP_", "capture"),
    ("PB_", "playback"),
    ("WFF_", "write FIFO"),
    ("RFF_", "read FIFO"),
    ("DAC_", "DACs"),
    ("PAD_", "pads"),
    ("SFTRST_", "soft resets"),
    ("OUT_", "output"),
    ("INT_", "interrupts"),
    ("GPIO_", "GPIO"),
]


def header_fields():
    """(seg, reg, lo, width, name) for every UReg declared in tv5725.h."""
    src = open(HEADER, encoding="utf-8", errors="replace").read()
    return [(int(s, 16), int(r, 16), int(lo), int(w), n)
            for s, r, lo, w, n in DECL.findall(src)]


def runtime_written():
    """Every field the firmware writes outside a preset table."""
    patterns = ["*.ino", "*.cpp", "*.h", os.path.join("src", "**", "*.cpp"),
                os.path.join("src", "**", "*.h")]
    names = set()
    for pattern in patterns:
        for path in glob.glob(os.path.join(SKETCH, pattern), recursive=True):
            if os.path.basename(path) == "tv5725.h":
                continue
            text = open(path, encoding="utf-8", errors="replace").read()
            names.update(WRITE.findall(text))
    return names


def datasheet_names():
    """Every field name RD-5725-1.1 declares, from the header tooling's merge.

    Used only to break ties: where two header names cover the same bits, the
    block writes the one a reader can find in the datasheet. Missing file is not
    fatal -- the tie-break degrades to "keep the wider field".
    """
    merged = os.path.join(HERE, "..", "tv5725-header", "merged.json")
    try:
        with open(merged) as f:
            return set(json.load(f))
    except (OSError, ValueError):
        return set()


def subsystem_of(name):
    for prefix, label in SUBSYSTEM:
        if name.startswith(prefix):
            return prefix, label
    return "misc", "everything else"


def bringup_fields():
    """[(subsystem, name, seg, reg, lo, width, value)] the block must write."""
    tables = {name: pc.read_table(name) for name in pc.SCALING_PRESETS}
    agreed, _ = pc.split(tables)
    reference = tables["pal_1920x1080"]
    runtime = runtime_written()

    out = []
    for seg, reg, lo, width, name in header_fields():
        span = (lo + width + 7) // 8
        addrs = [(seg, reg + i) for i in range(span)]
        if not all(a in agreed for a in addrs):
            continue
        # Owned at runtime, or not a chip register at all.
        if name in runtime or name.startswith("GBS_"):
            continue
        raw = 0
        for i, addr in enumerate(addrs):
            raw |= reference[addr] << (8 * i)
        value = (raw >> lo) & ((1 << width) - 1)
        prefix, _ = subsystem_of(name)
        out.append((prefix, name, seg, reg, lo, width, value))

    # **WHERE TWO NAMES COVER THE SAME BITS, THE DATASHEET'S NAME WINS.**
    # Michael, 2026-08-13: "can we use only the datasheet registers in the case
    # of overlap?" -- which is the right rule for a block whose whole purpose is
    # to be readable: every line can then be looked up in RD-5725-1.1.
    #
    # gbs-control carries its own names for some fields, and they are not
    # wrong, only local:
    #
    #     INT_CONTROL_RST_SOGBAD    is  INT_RST_0
    #     SDRAM_RESET_CONTROL       is  MEM_INI_REG
    #     ADC_TEST_0C_BIT1          is  bit 1 of ADC_TEST
    #
    # The last is a containment rather than an alias, and it is the one that
    # actually matters: writing both ADC_TEST and ADC_TEST_0C_BIT1 makes the
    # result depend on which lands last. They agree today. A bring-up should not
    # rely on that.
    documented = datasheet_names()
    spans = [(f[2], f[3], f[4], f[5], f[1]) for f in out]
    drop = set()
    for seg, reg, lo, width, name in spans:
        for seg2, reg2, lo2, width2, name2 in spans:
            if name == name2 or seg != seg2:
                continue
            here = (reg * 8 + lo, reg * 8 + lo + width)
            there = (reg2 * 8 + lo2, reg2 * 8 + lo2 + width2)
            if not (there[0] <= here[0] and here[1] <= there[1]):
                continue  # `name` is not inside `name2`
            if there == here:
                # Exact alias: keep whichever the datasheet declares.
                if name not in documented and name2 in documented:
                    drop.add(name)
            elif name2 in documented or name not in documented:
                # Contained in a wider field, and the wider one is usable.
                drop.add(name)
    out = [f for f in out if f[1] not in drop]
    return sorted(out, key=lambda f: (f[0], f[2], f[3], f[4]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subsystem", help="only this prefix, e.g. VDS_")
    args = parser.parse_args()

    fields = bringup_fields()
    if args.subsystem:
        fields = [f for f in fields if f[0] == args.subsystem]

    labels = dict(SUBSYSTEM)
    grouped = collections.OrderedDict()
    for field in fields:
        grouped.setdefault(field[0], []).append(field)

    for prefix, rows in grouped.items():
        print(f"// --- {labels.get(prefix, 'everything else')} "
              f"({len(rows)} fields) ---")
        for _, name, seg, reg, lo, width, value in rows:
            call = f"GBS::{name}::write(0x{value:X});"
            print(f"    {call:<52}// s{seg}_{reg:02x}[{lo + width - 1}:{lo}]")
        print()

    print(f"// {len(fields)} fields total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
