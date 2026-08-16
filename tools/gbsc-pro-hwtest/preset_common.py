#!/usr/bin/env python3
"""What every preset table agrees on, and what it does not.

    python3 tools/gbsc-pro-hwtest/preset_common.py
    python3 tools/gbsc-pro-hwtest/preset_common.py --show-varying

A "preset" is twelve tables of 432 bytes each, loaded wholesale on every mode
change. docs/investigations/preset-abandonment-audit.md establishes that most of that is not a
preset at all: the great majority of the bytes are IDENTICAL in all twelve
tables, which makes them static chip bring-up wearing a preset's clothes.

This is the script that measures the split, so the number can be re-derived
rather than quoted; test_preset_common.py asserts it has not moved.

**THE BRING-UP HAS TO BE NAMED REGISTER WRITES, NOT A BLOCK OF ANONYMOUS ONES**,
before preset loading can be switched off. CLAUDE.md's "preset loads leave things
behind" trap is a yellow-tinted picture traced to DAC_RGBS_B0ENZ being cleared by
a bulk table load that nothing patched back -- invisible precisely because the
byte had no name at the point it was written.
"""

import argparse
import json
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code", "gbs-control")

# The scaling presets the sketch still compiles. ofw_RGBS and ofw_ypbpr are
# deliberately excluded: they differ in ways nobody has explained -- IF_LINE_ST
# 0x18, and they are the only two tables that ENABLE HBOUT/VBOUT -- so folding
# them in contaminates "identical" with a disagreement that is real.
#
# The two downscale tables stay in ARCHIVED_PRESETS below: preset_tables.json is
# a historical record and the 306/126 split is a statement about the twelve that
# shipped.
SCALING_PRESETS = [
    "pal_240p", "pal_768x576", "pal_1280x720", "pal_1280x1024",
    "pal_1920x1080",
    "ntsc_240p", "ntsc_720x480", "ntsc_1280x720", "ntsc_1280x1024",
    "ntsc_1920x1080",
]

# What the archive holds, which is what shipped. Do not prune this when a table
# goes -- the archive is the record of the thing being replaced.
ARCHIVED_PRESETS = SCALING_PRESETS + ["pal_downscale", "ntsc_downscale"]

# `0x7C, // s0_40` -- the value and the address the table itself declares.
#
# **ONE OR TWO HEX DIGITS.** The low sixteen of each segment are labelled
# `// s1_0` .. `// s1_F`, single-digit, while everything from 0x10 up is
# two-digit. Requiring two silently drops 16 bytes from each of segments 1, 3, 4
# and 5 -- 64 of 432 -- invisibly, because what remains still parses cleanly and
# still gives a plausible split. Segment 0's range starts at 0x40, so it comes out
# whole and hides the loss further.
ENTRY = re.compile(r"(0[xX][0-9a-fA-F]+|\d+)\s*,?\s*//\s*s(\d)_([0-9a-fA-F]{1,2})\b")


# **The archive is the source, and the headers are on their way out.**
#
# Every tool that audits the preset tables reads the preset tables, so deleting
# the twelve headers from the firmware would blind the checks that prove the
# deletion was safe -- the agreed/varying split below would stop working.
# That is the wrong order:
# the audit trail has to outlive the thing it audits.
#
# So the twelve tables are archived here as measured data, and this file reads
# the archive. It is an archaeological record of what upstream gbs-control
# shipped, NOT a source of truth about the chip -- nothing may be configured
# from it, and a disagreement between it and Tv5725:: is a result rather than a
# discrepancy. See docs/chip-initialisation.md, "Code first".
ARCHIVE = os.path.join(HERE, "preset_tables.json")


def read_table_from_header(name):
    """One preset as {(segment, register): value}, parsed from its .h file.

    The labels are trusted over positional arithmetic on purpose. The tables
    cover four disjoint ranges in segment 0 alone, so reconstructing addresses
    by counting bytes needs the range list to be right, and a wrong range list
    fails silently by shifting every field after it -- the same trap the
    preferences reader fell into (see applySavedInputSource's comment).

    Kept after the headers are deleted so the archive's provenance stays
    reproducible for as long as any checkout still has them.
    """
    path = os.path.join(SKETCH, f"{name}.h")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    table = {}
    for value, segment, register in ENTRY.findall(text):
        table[(int(segment), int(register, 16))] = int(value, 0)
    if not table:
        raise SystemExit(f"{name}.h: no labelled register bytes found")
    return table


def headers_present():
    """Whether this checkout still has the .h files to compare against.

    Asked of SCALING_PRESETS, which is what the sketch compiles -- NOT of the
    archive. An all-or-nothing guard over both sets skips the archive check for
    every table as soon as one header goes, so the survivors silently stop being
    pinned.
    """
    return all(os.path.exists(os.path.join(SKETCH, f"{n}.h"))
               for n in SCALING_PRESETS)


def read_table(name):
    """One preset as {(segment, register): value}, from the archive."""
    with open(ARCHIVE, "r", encoding="utf-8") as f:
        tables = json.load(f)["tables"]
    if name not in tables:
        raise SystemExit(f"{name}: not in {ARCHIVE}")
    return {(int(k.split(":")[0]), int(k.split(":")[1], 16)): v
            for k, v in tables[name].items()}


def write_archive(path):
    """Regenerate the archive from the headers. Needs them to still be here."""
    if not headers_present():
        raise SystemExit(
            "the twelve preset headers are gone from this checkout, so the "
            "archive cannot be regenerated from them -- it is the record now")
    tables = {n: {f"{s}:{r:02x}": v
                  for (s, r), v in sorted(read_table_from_header(n).items())}
              for n in SCALING_PRESETS}
    with open(path, "w", encoding="utf-8") as f:
        json.dump({
            "what": "The twelve gbs-control scaling preset tables, as shipped, "
                    "archived when they were deleted from the firmware. An "
                    "archaeological record: nothing is configured from this.",
            "tables": tables,
        }, f, indent=1, sort_keys=True)
        f.write("\n")


def split(presets):
    """(agreed, varying) -- addresses every table writes the same, and the rest.

    An address missing from any table counts as varying: "they agree" has to
    mean every table wrote it and wrote it identically, or the bring-up would
    freeze a value some mode never asked for.

    **THE POPULATION IS THE ARGUMENT'S KEYS, NOT SCALING_PRESETS.** This took a
    `presets` dict and then iterated the module-level list regardless, which was
    invisible while one list existed and wrong the moment there were two: on
    2026-08-14 it was handed all twelve archived tables and quietly compared ten,
    reporting 330 agreed where the reviewed finding is 306. A helper that ignores
    the argument it is given cannot be asked a different question.
    """
    names = sorted(presets)
    everywhere = set(presets[names[0]])
    for name in names[1:]:
        everywhere &= set(presets[name])

    agreed, varying = {}, {}
    for address in sorted(everywhere):
        values = {presets[n][address] for n in names}
        if len(values) == 1:
            agreed[address] = values.pop()
        else:
            varying[address] = {n: presets[n][address] for n in names}

    for address in sorted(set().union(*(set(p) for p in presets.values())) - everywhere):
        varying[address] = {n: presets[n].get(address) for n in names}
    return agreed, varying


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--show-varying", action="store_true",
                    help="list every address the tables disagree on, with each value")
    ap.add_argument("--json", metavar="PATH", help="write the split as JSON")
    ap.add_argument("--archive", metavar="PATH", nargs="?", const=ARCHIVE,
                    help="regenerate the table archive from the .h files and exit")
    args = ap.parse_args()

    if args.archive:
        write_archive(args.archive)
        print(f"  archived {len(SCALING_PRESETS)} tables to {args.archive}")
        return 0

    presets = {name: read_table(name) for name in SCALING_PRESETS}
    agreed, varying = split(presets)

    sizes = {len(t) for t in presets.values()}
    print(f"  {len(SCALING_PRESETS)} scaling tables, "
          f"{'all ' + str(sizes.pop()) if len(sizes) == 1 else str(sorted(sizes))} labelled bytes each")
    print()
    print(f"  identical in all twelve   {len(agreed):4d}   static bring-up")
    print(f"  disagree somewhere        {len(varying):4d}   genuinely mode-dependent")
    print()

    by_segment = defaultdict(lambda: [0, 0])
    for address in agreed:
        by_segment[address[0]][0] += 1
    for address in varying:
        by_segment[address[0]][1] += 1
    print("  per segment:")
    for segment in sorted(by_segment):
        agreed_n, varying_n = by_segment[segment]
        print(f"    s{segment}   {agreed_n:4d} agreed   {varying_n:4d} varying")

    if args.show_varying:
        print("\n  --- the addresses no bring-up can freeze ---")
        for address, values in sorted(varying.items()):
            shown = ", ".join(
                f"{n.replace('_', '')[:11]}={'-' if v is None else hex(v)}"
                for n, v in values.items())
            print(f"    s{address[0]}_{address[1]:02x}   {shown}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({
                "agreed": {f"{s}:{r:02x}": v for (s, r), v in agreed.items()},
                "varying": sorted(f"{s}:{r:02x}" for (s, r) in varying),
            }, f, indent=2, sort_keys=True)
        print(f"\n  wrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
