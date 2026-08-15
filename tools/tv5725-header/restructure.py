#!/usr/bin/env python3
"""Regroup tv5725.h into the register datasheet's own order.

    python3 tools/tv5725-header/restructure.py                 # report only
    python3 tools/tv5725-header/restructure.py --write

The header grew its own grouping -- STATUS, IF, VDS, PIP, OSD -- which is five
labels for 793 fields and matches nothing you can look up. RD-5725-1.1 divides
the same registers into thirteen functional chapters, and using those means a
field can be found in the header from where it sits in the PDF, and the other
way round.

The grouping is NOT by segment, which is the reason it is worth doing. Segment 0
carries STATUS, MISCELLANEOUS and OSD; segment 1 carries INPUT FORMATTER,
HD_BYPS and MODE_DETECT; segment 5 carries ADC and SYNC_PROC. Sorting by address
would interleave them and read no better than what it replaces.

Nothing about what the header declares may change. That is checked, not assumed:
the run refuses to write unless `header.compare` reports no register moved and
every field's comment text survives intact.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datasheet
import header
import layout

HERE = os.path.dirname(os.path.abspath(__file__))
REGDEF = os.path.join(HERE, "regdef.txt")
HEADER = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code",
                      "gbs-control", "tv5725.h")

MARKER = re.compile(r"^\s*/\*.*\*/\s*$")   # /* new_S*/ and friends


def split(src):
    """(prologue, interlude, epilogue) -- everything that is not a typedef.

    Marker-based splitting does not work on this file. `static const uint8_t`
    appears in detail::TVAttrs before the typedefs even start, and the OSD
    constants and the osdIcon() function sit in the MIDDLE of the register
    block, with the 98 fields added by the audit tacked on after them. So the
    boundaries are found from the typedefs themselves: everything before the
    first and after the last is kept as-is, and the C++ stranded between them is
    lifted out whole and re-emitted below the registers.
    """
    lines = src.split("\n")
    decl = [i for i, l in enumerate(lines) if header.TYPEDEF.match(l)]
    first, last = decl[0], decl[-1]

    prologue = lines[:first]
    while prologue and (header.COMMENT_ONLY.match(prologue[-1])
                        or not prologue[-1].strip()):
        prologue.pop()                # section headings; the chapters replace them

    stranded = [i for i in range(first, last + 1)
                if i not in set(decl)
                and lines[i].strip()
                and not header.COMMENT_ONLY.match(lines[i])
                # /* new_S*/ ... /* new_E*/ bracket a run of typedefs 1800
                # lines apart. Left in, they stretch the interlude across the
                # whole register block and it gets emitted twice. They marked a
                # grouping the chapters now replace, so they go.
                and not MARKER.match(lines[i])]
    interlude = lines[stranded[0]:stranded[-1] + 1] if stranded else []

    return prologue, interlude, lines[last + 1:]


def regroup(fields, chapters, owner):
    """Fields in datasheet order: chapter, then address, then bit offset."""
    by_chapter = defaultdict(lambda: defaultdict(list))
    for t, comment in fields:
        by_chapter[datasheet.place((t.seg, t.reg), owner)][(t.seg, t.reg)].append(
            (t, comment))
    out = []
    for number, title, seg in chapters:
        registers = by_chapter.get(number)
        if not registers:
            continue
        out.append((number, title, seg, [
            # widest first at a shared offset, so a whole-register convenience
            # alias introduces the fields it covers rather than landing among
            # them
            sorted(registers[k], key=lambda p: (p[0].offset, -p[0].width))
            for k in sorted(registers)]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()

    src = open(HEADER).read()
    prologue, interlude, epilogue = split(src)
    fields = header.annotated(src)
    groups = regroup(fields, datasheet.chapters(REGDEF), datasheet.owners(REGDEF))
    out = "\n".join(prologue + layout.render(groups) + ["", ""]
                    + interlude + epilogue)

    moved = header.compare(src, out)
    before, after = header.wordings(src), header.wordings(out)
    reworded = sorted(n for n in set(before) | set(after)
                      if before.get(n) != after.get(n))
    dup = header.duplicates(out)

    print(f"typedefs      {len(fields)} -> {len(header.typedefs(out))}")
    print(f"chapters      {len(groups)}")
    print(f"comment chars {sum(map(len, before.values()))} -> "
          f"{sum(map(len, after.values()))}")
    print(f"longest line  {max(len(l) for l in out.split(chr(10)))}")
    print(f"registers moved   {moved or 'none'}")
    print(f"comments reworded {reworded or 'none'}")
    print(f"duplicate names   {dup or 'none'}")

    if moved or reworded or dup:
        print("\nREFUSING TO WRITE -- this is a regrouping, so it must change "
              "neither what the header declares nor what it says.")
        return 1
    if args.write:
        open(HEADER, "w").write(out)
        print(f"\nwritten to {os.path.normpath(HEADER)}")
    else:
        print("\nlooks clean; pass --write to apply")
    return 0


if __name__ == "__main__":
    sys.exit(main())
