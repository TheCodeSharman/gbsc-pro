#!/usr/bin/env python3
"""The bit diagram above each register's table, as a second opinion on slices.

    import diagram
    slices = diagram.slices("regdef.txt")     # (seg, reg) -> [(name, hi, lo, fhi, flo)]

RD-5725-1.1 states a wide field's bit slices in three places and they disagree:
the bit diagram, the Bit/Name table rows, and the `bit[hi:lo]` inside the
Function text. `merge_slices` reads the table rows, so where those are wrong it
is wrong -- VDS_HB_ST comes out 10 bits at s3_05 instead of 12 at s3_04, and a
field declared narrower than it is truncates every write through it in silence.

CLAUDE.md, "The datasheet contradicts itself": cross-check all three, and do NOT
promote the diagram to sole authority -- SP_H_CST_SP has the wrong slice in its
diagram and the right one in its table. This module supplies vote number one.

The diagram is a column-aligned graphic:

                 7        6        5        4        3        2        1        0
     Bit                 VDS_HB_SP [3:0]                 VDS_HB_ST [11:8]

Rather than reconstruct pixel columns, labels are read left to right and given
register bits from 7 downward, each taking as many bits as its `[fhi:flo]` slice
declares. Left-to-right is most-significant-first, so the assignment is forced
as long as the widths account for all eight bits -- and when they do not, the
register is reported as unparsed rather than guessed at. That check is what
makes this usable as a vote: it declines rather than inventing.
"""

import re

REG_HDR = re.compile(r"REG\s+S(\d)_([0-9A-Fa-f]{2})\s*,\s*(R/W|RO|WO)", re.I)
# The ruler line: 7 6 5 4 3 2 1 0, widely spaced, nothing else on it.
RULER = re.compile(r"^\s*7(\s+6)(\s+5)(\s+4)(\s+3)(\s+2)(\s+1)(\s+0)\s*$")
TBL_HDR = re.compile(r"^(\s+)Bit(\s+)Name(\s+)Function\s*$", re.I)
# `VDS_HB_ST [11:8]`, `MEM_MODE_REG [7:0]`, or a bare `RESERVED`.
LABEL = re.compile(r"([A-Z][A-Z0-9_]{2,})\s*(?:\[\s*(\d+)\s*(?::\s*(\d+))?\s*\])?")


def _labels(text):
    """Every label in a diagram body, left to right, with its declared slice."""
    out = []
    for m in LABEL.finditer(text):
        name = m.group(1)
        if m.group(2) is None:
            out.append((name, None, None))
            continue
        fhi = int(m.group(2))
        flo = int(m.group(3)) if m.group(3) is not None else fhi
        out.append((name, max(fhi, flo), min(fhi, flo)))
    return out


def slices(path):
    """(seg, reg) -> [(name, reg_hi, reg_lo, field_hi, field_lo)].

    Registers whose labels do not account for exactly eight bits are omitted:
    a partial diagram cannot place its fields, and a guess here would be a
    third disagreeing opinion rather than a tie-breaker.
    """
    lines = open(path, errors="replace").read().split("\n")
    out = {}
    seg = reg = None
    for i, line in enumerate(lines):
        m = REG_HDR.search(line)
        if m and "...." not in line:
            seg, reg = int(m.group(1)), int(m.group(2), 16)
            continue
        if seg is None or not RULER.match(line):
            continue

        # The diagram body runs from under the ruler to the Bit/Name table.
        body = []
        for j in range(i + 1, min(i + 8, len(lines))):
            if TBL_HDR.match(lines[j]) or REG_HDR.search(lines[j]):
                break
            body.append(lines[j])
        found = _labels(" ".join(body))
        if not found:
            continue

        # Assign register bits from 7 downward, most significant label first.
        placed, bit = [], 7
        for name, fhi, flo in found:
            width = 8 if fhi is None and len(found) == 1 else (
                None if fhi is None else fhi - flo + 1)
            if width is None:          # a RESERVED among others: cannot place
                placed = None
                break
            lo = bit - width + 1
            if lo < 0:
                placed = None
                break
            placed.append((name, bit, lo,
                           fhi if fhi is not None else 7,
                           flo if flo is not None else 0))
            bit = lo - 1
        if placed and bit == -1:
            out[(seg, reg)] = placed
    return out


def fields(path):
    """name -> (seg, reg, offset, width), reconciled across the registers a
    field spans. The anchor is the piece carrying field bit 0, matching
    merge_slices' rule so the two are directly comparable."""
    per = {}
    for (seg, reg), placed in slices(path).items():
        for name, rhi, rlo, fhi, flo in placed:
            if name == "RESERVED":
                continue
            per.setdefault(name, []).append((seg, reg, rlo, fhi, flo))
    out = {}
    for name, pieces in per.items():
        if len({p[0] for p in pieces}) != 1:
            continue                    # same name in two segments: not one field
        base = [p for p in pieces if p[4] == 0]
        if len(base) != 1:
            continue
        seg, reg, rlo, _, _ = base[0]
        out[name] = (seg, reg, rlo, max(p[3] for p in pieces) + 1)
    return out


if __name__ == "__main__":
    import sys
    f = fields(sys.argv[1] if len(sys.argv) > 1 else "regdef.txt")
    print(f"{len(f)} fields placed from bit diagrams")
    for probe in ("VDS_HB_ST", "VDS_HB_SP", "VDS_HSCALE", "PLLAD_MD", "SP_H_CST_SP"):
        print(f"  {probe:14} {f.get(probe)}")
