#!/usr/bin/env python3
"""Extract each field's Function text from RD-5725-1.1, bounded correctly.

The existing extraction bleeds across field boundaries -- TEST_BUS_SEL ends up
carrying TEST_BUS_EN's description -- because the PDF's Function column wraps
both above and below the row that names the field. Annotating a header with
those would be worse than leaving it undocumented.

This uses the layout instead. In `pdftotext -layout` output each field table is:

      Bit       Name                  Function
                                      <function text, may start ABOVE the row>
       0        DEC1_BYPS
                                      <and continue BELOW it>
       1        DEC2_BYPS             <or sit on the row itself>

So: find the Name column and the Function column by where the header words sit,
then walk the table assigning every Function-column fragment to the nearest bit
row -- preferring the row it is level with, then the row below (the PDF wraps
description-first for multi-line entries), then the row above.
"""
import re, sys, json
from collections import OrderedDict

REG_HDR = re.compile(r"REG\s+S(\d)_([0-9A-Fa-f]{2})\s*,\s*(R/W|RO|WO)", re.I)
# `Function` is spelled FUNCTION in seven of the 558 tables, and a case-sensitive
# match silently skipped every field in them -- MEM_FST_REG among them.
TBL_HDR = re.compile(r"^(\s+)Bit(\s+)Name(\s+)Function\s*$", re.I)

# The name may begin with `_`: the PDF wraps long field names across two lines
# and the continuation can start anywhere, including mid-underscore
# (`PB_CAP_BUF_STA_ADDR` + `_A [20:16]`). Requiring an initial [A-Z] dropped
# those rows entirely; allowing it is what lets NAME_WRAP rejoin them.
#
# **AND IT MAY BEGIN WITH `[`.** The wrap can push the ENTIRE name onto the line
# above, leaving the bit row holding nothing but its slice:
#
#           WFF_SAFE_GUARD_B       Write FIFO Buffer B Safe Guard Address:
#     7-0   [7:0]                  Safe guard address buffer B [7:0] ...
#
# The same defect as the underscore one, one character class short, and nastier:
# an unmatched row reads as description text, so the field does not arrive wrong,
# it does not arrive at all. CAP_SAFE_GAURD_A, WFF_SAFE_GUARD_A,
# WFF_SAFE_GUARD_B and the `[0]` slice of VDS_NS_SQUARE_RAD all go that way, the
# last taking its whole 15-bit field with it.
# docs/preset-gap-datasheet-map.md
BITROW = re.compile(r"^\s{2,}(\d{1,2})(?:\s*[-~]\s*(\d{1,2}))?\s{2,}([A-Z_\[][A-Z0-9_\[\]:]{0,})\s*(.*)$")

# The same row, but keeping the `[9:8]` slice suffix so merge_slices can
# reconcile a field the datasheet spreads over several registers.
#
# **PARSE WITH keep_slices=True TO BUILD merged.json.** Without it the `[9:8]`
# suffixes are stripped and every slice of a wide field collides on one key.
BITROW_SLICED = re.compile(
    r"^\s{2,}(\d{1,2})(?:\s*[-~]\s*(\d{1,2}))?\s{2,}"
    r"([A-Z_][A-Z0-9_]{0,}(?:\s*\[\d+(?::\d+)?\])?|\[\d+(?::\d+)?\])\s*(.*)$")

# A bare identifier fragment sitting in the Name column of a line that carries no
# bit number -- the first half of a wrapped name.
NAME_WRAP = re.compile(r"^[A-Z][A-Z0-9_]*$")
NOISE = re.compile(r"^(RD-5725|TRUEVIEW5725|Registers Definition|\d+[-—]\d+)")


def clean(s):
    s = " ".join(s.split())
    s = re.sub(r"\s*RD-5725-\S+\s*", " ", s)
    s = re.sub(r"\s*TRUEVIEW5725\s*", " ", s)
    s = re.sub(r"\s*Registers Definition\s*", " ", s)
    return " ".join(s.split()).strip(" .,")


def parse(path, keep_slices=False):
    bitrow = BITROW_SLICED if keep_slices else BITROW
    lines = open(path, errors="replace").read().split("\n")   # NOT splitlines(): the PDF text has form feeds, which splitlines() treats as line breaks and which cut register tables in half
    out = {}                       # NAME -> (seg, reg, hi, lo, access, desc)
    seg = reg = acc = None
    i = 0
    while i < len(lines):
        m = REG_HDR.search(lines[i])
        if m:
            seg, reg, acc = int(m.group(1)), int(m.group(2), 16), m.group(3).upper()
            i += 1
            continue
        h = TBL_HDR.match(lines[i])
        if not h or seg is None:
            i += 1
            continue

        name_col = len(h.group(1)) + 3 + len(h.group(2))
        func_col = name_col + 4 + len(h.group(3))
        rows, pending = [], []       # rows: [hi,lo,name,[frags]]
        # Per row, (lines taken ABOVE its own Name row, 1 if it also has text ON
        # the row). That shape is what places the boundary in the next run --
        # see the split below.
        shapes = []
        # The first half of a wrapped field name, carried from the line above.
        # The PDF breaks a long name across two lines and puts the bit number
        # only on the second, so the halves have to be rejoined or the field is
        # recorded under a fragment of its own name -- which is worse than
        # missing it. `PB_CAP_BUF_STA_ADDR_B` was being stored as `R_B`, and
        # `IF_HS_RATE_SEG0` as `EG0`.
        wrapped = None
        j = i + 1
        while j < len(lines):
            ln = lines[j]
            if REG_HDR.search(ln) or TBL_HDR.match(ln):
                break
            if not ln.strip():
                j += 1
                continue
            b = bitrow.match(ln)
            # NOISE must be tested AFTER BITROW: its page-number pattern
            # \d+[-—]\d+ also matches a bit range like "7-0", which silently
            # discarded every multi-bit field in the datasheet.
            if not b and NOISE.match(ln.strip()):
                j += 1
                continue
            if b:
                a1 = int(b.group(1))
                a2 = int(b.group(2)) if b.group(2) else a1

                # The name comes from the NAME COLUMN, never from "whatever
                # follows the bit number". Those differ whenever the name is
                # wrapped, because the Function text then starts on the bit
                # row itself and the row pattern happily captures its first
                # letter: `MADPT_GM_NOISE_VALU` + the `I` of "In global motion"
                # was being recorded as MADPT_GM_NOISE_VALUI.
                name = ln[name_col:func_col].strip() if len(ln) > name_col else ""

                # A name can be split with its first half ABOVE the bit row and
                # its second half BELOW it, leaving the row's own name column
                # empty:
                #       MADPT_GM_NOISE_VALU
                #   3-0                       In global motion noise manual ...
                #       E [3:0]
                if not name:
                    for k in range(j + 1, min(j + 3, len(lines))):
                        if not lines[k].strip():
                            continue
                        if bitrow.match(lines[k]):
                            break
                        below = (lines[k][name_col:func_col].strip()
                                 if len(lines[k]) > name_col else "")
                        if below:
                            name = below
                        break

                # ...but the columns are taken from each table's own header, and
                # adjacent tables do not agree on them. At s3_08 the header puts
                # Name at column 26 while its rows put it at 18, so the slice
                # lands mid-name and yields `P [3:0]`.
                #
                # When the column slice is a strict SUFFIX of what the row
                # carries, the columns are misaligned and the row is right. The
                # MADPT_GM_NOISE_VALU case is the opposite shape -- the column
                # is a PREFIX of the row, which has swallowed a letter of the
                # Function text -- so the two rules do not collide.
                #
                # A fragment here is not a mis-named field but a missing one:
                # `[3:0]` is the piece holding bit 0, so merge_slices cannot
                # anchor VDS_VB_SP and drops all 11 bits of it.
                row_name = (b.group(3) or "").strip()
                if name and row_name != name and row_name.endswith(name):
                    name = row_name

                if wrapped:
                    name = wrapped + name
                    wrapped = None
                if not name:
                    j += 1
                    continue
                if not keep_slices:
                    name = name.split("[")[0].strip()
                inline = ln[func_col:].strip() if len(ln) > func_col else ""
                # The Function column wraps above AND below its bit row, and the
                # cell is vertically CENTRED on that row -- so a run of text
                # between two rows is the tail of the field above and the head
                # of the field below, and the boundary is not at a fixed offset.
                #
                # Centred means a field takes as many lines below its row as it
                # took above, less one if it also has text ON the row. Assuming
                # a single trailing line instead gave every field the NEXT one's
                # title: IF_UV_REVERT ended "...Flip the Y and UV order Select
                # CCIR656 data", which is IF_SEL_656's heading, and TEST_BUS_SEL
                # carried TEST_BUS_EN's description. Both read as real datasheet
                # prose about a real register, so nothing downstream could catch
                # it, and both reached Tv5725.h.
                title = []
                if pending:
                    if rows:
                        # A field with no text on its own Name row has its whole
                        # cell in this run, so it must keep at least its title.
                        # Without that floor a run of one-line cells collapses
                        # alternately -- GPIO_VAL_0 claims the line below it to
                        # balance the one above, and that line is the whole of
                        # GPIO_VAL_1's cell.
                        above, on_row = shapes[-1]
                        room = len(pending) - (0 if inline else 1)
                        mine = max(0, min(room, above - on_row))
                        rows[-1][3].extend(pending[:mine])
                        title = pending[mine:]
                    else:
                        title = pending
                    pending = []
                rows.append([max(a1, a2), min(a1, a2), name, list(title)])
                shapes.append((len(title), 1 if inline else 0))
                if inline:
                    rows[-1][3].append(inline)
            else:
                # A line with no bit number still has a Name column, and when it
                # holds a bare identifier it is the first half of the next row's
                # name rather than description text.
                col = ln[name_col:func_col].strip() if len(ln) > name_col else ""
                wrapped = col if NAME_WRAP.match(col) else None

                frag = ln[func_col:].strip() if len(ln) > func_col else ln.strip()
                if frag:
                    pending.append(frag)
            j += 1
        if pending and rows:
            rows[-1][3].extend(pending)

        siblings = {r[2] for r in rows}
        for hi, lo, name, frags in rows:
            if name in ("RESERVED",):
                continue
            # Every real TV5725 field name carries a module prefix and so
            # contains an underscore. Across the whole document the only names
            # that do not are wrap fragments the joiner did not catch -- AIN
            # from VDS_BLEV_GAIN, LIP from a clip control, ZE1 from a vertical
            # size -- plus RSERVED, which is the PDF's own typo for RESERVED.
            # Six names, all wrong, and none of them a field. Dropping them is
            # what keeps a fragment out of the header, where it would read as a
            # register nobody has heard of.
            base = name.split('[')[0].strip()
            if len(base) < 3 or '_' not in base:
                continue
            d = clean(" ".join(frags))
            # Bleed check: a field's own description must not name a DIFFERENT
            # field of the same register. When it does, the column parse ran
            # into the neighbour and the text cannot be trusted -- drop it
            # rather than write someone else's documentation into the header.
            for other in siblings:
                if other != name and other in d:
                    d = ""
                    break
            # First good parse wins. Registers appear more than once in the PDF
            # (contents page plus body) and "keep the longest" reliably picked
            # the bleeding one.
            if name not in out or (not out[name][5] and d):
                out[name] = (seg, reg, hi, lo, acc, d)
        i = j
    return out


if __name__ == "__main__":
    docs = parse(sys.argv[1])
    json.dump({k: {"seg": v[0], "reg": v[1], "hi": v[2], "lo": v[3],
                   "access": v[4], "desc": v[5]} for k, v in docs.items()},
              open(sys.argv[2], "w"), indent=1, sort_keys=True)
    print(f"{len(docs)} fields with bounded descriptions -> {sys.argv[2]}")
    for probe in ("DEC1_BYPS", "SP_PRE_COAST", "DAC_RGBS_B0ENZ", "TEST_BUS_SEL",
                  "TEST_BUS_EN", "IF_HB_ST", "PLL_2XV", "VDS_HSCALE"):
        if probe in docs:
            s, r, hi, lo, a, d = docs[probe]
            print(f"\n{probe}  s{s} 0x{r:02X} bits {hi}-{lo} {a}\n    {d[:180]}")
