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
    """For each name, the readable text that follows its last mention.

    The Function column is what we want and it trails the name, so this takes the
    following few lines and drops table furniture. Crude, but it degrades to
    "no description" rather than to nonsense, which the table parser did not.
    """
    lines = text.splitlines()
    where = {}
    for i, line in enumerate(lines):
        for name in re.findall(r"\b([A-Z][A-Z0-9_]{2,})\b", line):
            if name in names:
                where.setdefault(name, []).append(i)

    out = {}
    for name, hits in where.items():
        best = ""
        for i in hits:
            chunk = []
            for line in lines[i:i + 5]:
                stripped = line.strip()
                # keep prose, drop the bit-grid rows and page furniture
                if not stripped or NOISE.search(stripped):
                    continue
                stripped = re.sub(r"\s{2,}", "  ", stripped)
                if len(stripped) > 12 and re.search(r"[a-z]{3}", stripped):
                    chunk.append(stripped)
                if len(" ".join(chunk)) > 220:
                    break
            joined = " ".join(chunk).strip()
            if len(joined) > len(best):
                best = joined
        out[name] = best[:400]
    return out


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
