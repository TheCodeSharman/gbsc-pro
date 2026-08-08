#!/usr/bin/env python3
"""Build a register map for the TV5725: addressing from the firmware header,
meaning from the datasheet.

    python3 tools/gbsc-pro-hwtest/extract_registers.py

Writes tv5725_registers.json next to this script.

**The datasheet is the authority.** `tv5725.h` is used only for addressing —
segment, register, bit offset, width — because it is what the firmware actually
writes through and because it can be checked: of the 102 fields whose segment,
register and width could be compared mechanically against the PDF, 101 agreed
(the exception, DEC_TEST_SEL, differs by one bit of width).

Every field is marked with whether the datasheet knows it:

    documented      the name appears in the PDF, and its description was captured
    named-only      the name appears but no usable description was extracted
    NOT DOCUMENTED  the name appears nowhere in the PDF

That last group is roughly 200 fields, and they are gbs-control's own inventions
for registers it pokes without knowing what they do — `ADC_5_00`,
`ADC_TEST_0C_BIT1`, `ADC_UNUSED_60`. The header even labels eight of them
"fake name". Treat anything so marked with suspicion: the address may be right,
but nothing else about it is attested.

Descriptions are pulled by locating the field name in the extracted text rather
than by parsing the Bit/Name tables. Those tables are multi-column and defeat
line-based parsing — an earlier attempt produced garbage like
"ADC_AUTO_OADC_AUTO_O Bit FST_TEST" and silently lost 337 real fields.

**Descriptions are bounded by the table's Name rows**, rewritten 2026-08-09 when
Michael reported the register panel's comments "seem to be either missing or
aggregated". They were: `describe()` took the five lines following a name and
stopped at neither the end of that field's Function cell nor the start of the
next, so `ADC_SOGCTRL` (S5_02, bits 5-1) carried "ADC input selection When = 00,
R0/G0/B0/SOG0 as input" — which is `ADC_INPUT_SEL`'s, from bits 7-6 of the same
byte — while `ADC_INPUT_SEL` lost its own `00` case, making the channel
numbering look as though it starts at 01.

**The Function cell is vertically CENTRED on its Name row**, which is what made
it hard: a field's title sits ABOVE its own name and its detail below, so a
chunk that only runs forward gets the wrong half of two different fields. Taking
one line above, and stopping one line short of the next Name row so that field
keeps its own title, separates them.

Two things that had to be got right, both of which cost a wrong result first:

- **Only Name rows count.** The bit grid above each table names every field in
  the register at once; scanning forward from one of those reads whichever cell
  happens to follow, and being longer, it won the "longest chunk" contest. That
  is how `ADC_SOGCTRL` held `ADC_SOGEN`'s description.
- **The fixture has to be the real layout.** An invented sample that put each
  field's text after its name made the unit tests pass while the map stayed
  wrong — worse than having no test.

**Known limitation**, with an xfail against it in test_extract_registers.py: a
field whose cell is a SINGLE line still absorbs the next field's title, because
there is no row between them to stop at. Separating those needs the PDF's cell
geometry (`pdftotext -bbox`, or pdfplumber) rather than flattened text.

`documented` fell from 584 to 566 across this change, and that is a gain, not a
loss: the fields that dropped out were claiming descriptions like "Bit  Name
Function" (`PLL_4XV`), "0  1  glb_still" (`VDS_NR_GLB_STILL_MENU`), or another
field's text entirely (`PLL_LEN` held `PLL_VCORST`'s).

So the map is authoritative for addressing and much better than it was for
meaning — but still check the PDF before relying on a value.

Needs poppler (`pdftotext`).
"""

import json
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
HEADER = REPO / "GBSC-Pro-Source code" / "gbs-control" / "tv5725.h"
PDF = REPO / "docs" / "Tvia TrueView 5725 Registers Definition (RD-5725-1.1).pdf"
OUT = HERE / "tv5725_registers.json"

HEADER_FIELD = re.compile(
    r"typedef\s+UReg<\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,"
    r"\s*(\d+)\s*,\s*(\d+)\s*>\s*([A-Za-z_][A-Za-z0-9_]*)\s*;?\s*(//.*)?")

NOISE = re.compile(r"RD-5725|TRUEVIEW5725|Registers Definition|^\s*Bit\s*$|^\s*Name\s*$"
                   r"|^\s*Function\s*$|^\s*RESERVED\s*$|^[\s\d\-]*$")

# "INPUT_FORMATTER 14 ... REG S1_14, R/W" -- the start of the next register's
# page, which ends whatever field was being described.
REGISTER_HEADING = re.compile(r"REG\s+S\d_[0-9A-Fa-f]{2}")


def from_header():
    if not HEADER.exists():
        sys.exit(f"missing {HEADER}")
    fields = {}
    for seg, reg, off, width, name, comment in HEADER_FIELD.findall(
            HEADER.read_text(encoding="utf-8", errors="replace")):
        fields[name] = {
            "seg": int(seg, 16), "reg": int(reg, 16),
            "off": int(off), "width": int(width),
            "fake": bool(comment and "fake" in comment.lower()),
        }
    return fields


def pdf_text():
    if not PDF.exists():
        sys.exit(f"missing {PDF}")
    if not shutil.which("pdftotext"):
        sys.exit("pdftotext not on PATH -- install poppler-utils")
    cmd = ["pdftotext", "-layout", str(PDF), "-"]
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode != 0 or not done.stdout:
        sys.exit("pdftotext failed — install poppler-utils")
    return done.stdout


def describe(text, names):
    """For each name, the Function-column text belonging to that name alone.

    The Function column trails its Name in the RD's layout, so the text after a
    name is the right place to look. What makes it hard is that the chunk has to
    STOP: where the PDF packs several narrow fields into one byte, the next
    field's row follows immediately, and a chunk that runs on gives the field its
    neighbour's meaning and truncates the neighbour's own.

    That was the bug. `ADC_SOGCTRL` (S5_02 bits 5-1) carried "ADC input selection
    When = 00, R0/G0/B0/SOG0 as input", which is `ADC_INPUT_SEL`'s enum from bits
    7-6 of the same byte -- and the bleed cut `ADC_INPUT_SEL`'s own text so its
    00 case appeared nowhere in the map, making the channel numbering look as
    though it starts at 01. Michael, 2026-08-09: the register panel's comments
    "seem to be either missing or aggregated".

    So each chunk is bounded at the next known field name, and separately at the
    next register heading, and the Bit/Name/Function headings are dropped rather
    than absorbed. Still line-based and still degrades to "no description"
    rather than to nonsense.
    """
    lines = text.splitlines()
    where = {}
    for i, line in enumerate(lines):
        for name in re.findall(r"\b([A-Z][A-Z0-9_]{2,})\b", line):
            if name in names:
                where.setdefault(name, []).append(i)

    rows = name_rows(lines, names)

    out = {}
    for name, hits in where.items():
        # A Name row is the field's own table entry and always wins. The other
        # mentions are the bit grid above the table, where every field in the
        # register appears at once -- scanning forward from one of those reads
        # whichever cell happens to follow, and being longer, it used to win.
        # That is how ADC_SOGCTRL ended up holding ADC_SOGEN's description.
        own = [h for h in hits if rows.get(h) == name]
        spans = ([cell_span(h, rows) for h in own] if own
                 else [(h, h + 6) for h in hits])
        best = ""
        for span in spans:
            joined = gather(lines, span, name, names)
            if len(joined) > len(best):
                best = joined
        out[name] = best[:400]
    return out


NAME_ROW = re.compile(r"^\s*\d+(\s*-\s*\d+)?\s+([A-Z][A-Z0-9_]{2,})\b")


def name_rows(lines, names):
    """Line index -> field name, for the table's Name rows only.

    A Name row carries a bit range and exactly one known name. The bit GRID
    above each table carries several names and no bit range, so requiring the
    range is what keeps the grid out -- and the grid is where every field in the
    register appears at once, which would bound every cell to nothing.
    """
    rows = {}
    for i, line in enumerate(lines):
        match = NAME_ROW.match(line)
        if match and match.group(2) in names:
            rows[i] = match.group(2)
    return rows


def cell_span(hit, rows):
    """The lines belonging to the field whose Name row is at `hit`.

    The Function cell is CENTRED on its Name row, so the field's title sits on
    the line ABOVE the name and its detail below. Taking one line above, and
    stopping one line short of the next Name row so that field keeps its own
    title, is what a flattened-text parse can manage. It leaves one stray line
    on a field whose cell is a single line -- see the xfail in
    test_extract_registers.py.
    """
    above = [i for i in rows if i < hit]
    below = [i for i in rows if i > hit]
    start = max(hit - 1, max(above) + 1 if above else 0)
    stop = min(below) - 1 if below else hit + 6
    return start, max(stop, hit + 1)


def gather(lines, span, name, names):
    start, stop = span
    chunk = []
    for i in range(start, min(stop, len(lines))):
        # A new register heading ends this field, whatever else follows.
        if i > start and REGISTER_HEADING.search(lines[i]):
            break
        stripped = lines[i].strip()
        if not stripped or NOISE.search(stripped):
            continue
        stripped = strip_table_furniture(stripped, name)
        if len(stripped) > 12 and re.search(r"[a-z]{3}", stripped):
            chunk.append(stripped)
        if len(" ".join(chunk)) > 220:
            break
    return " ".join(chunk).strip()


def strip_table_furniture(line, name):
    """The field's own name, its bit range, and the column headings.

    They sit on the same line as the description often enough that dropping the
    whole line would lose real text, so they are cut out of it instead.
    """
    line = re.sub(r"\b" + re.escape(name) + r"\b(\s*\[[\d:]+\])?", " ", line)
    line = re.sub(r"^\s*\d+(\s*-\s*\d+)?\s+", " ", line)
    line = re.sub(r"\b(Bit|Name|Function)\b", " ", line)
    return re.sub(r"\s{2,}", "  ", line).strip()


def main():
    fields = from_header()
    text = pdf_text()
    names = set(fields)
    descriptions = describe(text, names)

    counts = {"documented": 0, "named-only": 0, "NOT DOCUMENTED": 0, "fake": 0}
    for name, entry in fields.items():
        mentioned = name in text
        desc = descriptions.get(name, "")
        if not mentioned:
            entry["status"] = "NOT DOCUMENTED"
        elif desc:
            entry["status"] = "documented"
        else:
            entry["status"] = "named-only"
        entry["desc"] = desc
        counts[entry["status"]] += 1
        if entry["fake"]:
            counts["fake"] += 1

    OUT.write_text(json.dumps(fields, indent=1, sort_keys=True), encoding="utf-8")
    print(f"wrote {OUT.name}: {len(fields)} fields")
    for key in ("documented", "named-only", "NOT DOCUMENTED"):
        print(f"  {key:16s} {counts[key]}")
    print(f'  (of which the header itself labels "fake name": {counts["fake"]})')


if __name__ == "__main__":
    main()
