#!/usr/bin/env python3
"""Where RD-5725-1.1 puts each register, so the header can be put there too.

The datasheet's own grouping is functional, not by segment, and that is the
point of using it. Segment 0 is split across STATUS, MISCELLANEOUS and OSD;
segment 1 across INPUT FORMATTER, HD_BYPS and MODE_DETECT; segment 5 across ADC
and SYNC_PROC. Sorting the header by address alone would interleave three
unrelated blocks and read no better than the arbitrary grouping it replaces.

    import datasheet
    chapters = datasheet.chapters("regdef.txt")     # ordered [(n, title, seg)]
    owner    = datasheet.owners("regdef.txt")       # (seg, reg) -> chapter number
"""
import re

CHAPTER = re.compile(r"^Chapter\s+(\d+)．\s*(.+?)\s*$")
REG = re.compile(r"REG\s+S(\d)_([0-9A-Fa-f]{2})\s*,")


def _scan(path):
    """(chapter number, title) -> the registers the datasheet lists under it."""
    found = {}
    order = []
    current = None
    for line in open(path, errors="replace").read().split("\n"):
        m = CHAPTER.match(line.strip())
        if m:
            current = (int(m.group(1)), m.group(2))
            if current not in found:
                found[current] = set()
                order.append(current)
            continue
        r = REG.search(line)
        if r and current is not None:
            found[current].add((int(r.group(1)), int(r.group(2), 16)))
    return order, found


def chapters(path):
    order, found = _scan(path)
    out = []
    for number, title in order:
        segs = sorted({seg for seg, _ in found[(number, title)]})
        out.append((number, title, segs[0] if len(segs) == 1 else None))
    return out


def owners(path):
    _, found = _scan(path)
    return {reg: number for (number, _), regs in found.items() for reg in regs}


def place(reg, owner):
    """Which chapter a register belongs to, including ones the datasheet omits.

    Twelve of the header's registers are gbs-control's own -- s1 0x2B-0x2D and a
    run in s5 0x60-0x69 -- and they are not arbitrary additions but scratch and
    alias registers sitting inside a documented block. The nearest documented
    register in the same segment is the block they belong with; falling back to
    a bucket at the end of the file would separate them from the fields they
    exist to serve.
    """
    if reg in owner:
        return owner[reg]
    seg, addr = reg
    same = [(abs(a - addr), a) for s, a in owner if s == seg]
    if not same:
        return None
    return owner[(seg, min(same)[1])]
