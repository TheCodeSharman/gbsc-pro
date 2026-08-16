#!/usr/bin/env python3
"""Build the register map the panel reads: addressing from the firmware header,
wording from the datasheet.

    python3 tools/gbsc-pro-hwtest/extract_registers.py

Writes tv5725_registers.json next to this script.

This is a join, not a parse. Reading RD-5725-1.1 happens once, in
tools/tv5725-header, and this consumes its merged.json — a second extraction of
the same PDF is a second owner of the same facts, and the two drifted: the
panel and the header audit disagreed about which fields the datasheet documents
while both claimed to have read it.

**Addressing comes from tv5725.h.** It is what the firmware writes through, and
where it disagrees with the datasheet it is right: RD-5725-1.1 gives a wide
field's slice in both the bit diagram and the Bit/Name rows, and for eleven
fields those contradict each other. VDS_HB_ST is the clearest — the rows make it
10 bits at s3_05, and it routinely holds 1342, which does not fit.

**The join is on bits, not on names.** The datasheet's own spelling is sometimes
wrong (CAP_SAFE_GAURD_A, OSD_YCBCR_RGB_FORMATE) and its long names wrap, leaving
the extraction holding a fragment — VDS_SVM_GAIN is filed under `_GAIN`,
VDS_FLOCK_EN under `K_EN`. Eighteen real registers reach the panel only because
the segment, register, offset and width match.

**Only fields the datasheet attests are emitted.** gbs-control invents names for
registers it pokes without knowing what they do — ADC_5_00, ADC_UNUSED_60,
GBS_PRESET_ID — and the header labels eight "fake name". A panel row for one of
those offers a value to write and nothing to write it against. They stay in
tv5725.h, which is the firmware's business; they do not belong in a tuning UI.
"""

import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code",
                      "gbs-control", "tv5725.h")
MERGED = os.path.join(HERE, "..", "tv5725-header", "merged.json")
OUT = os.path.join(HERE, "tv5725_registers.json")

TYPEDEF = re.compile(
    r"typedef\s+UReg<\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,"
    r"\s*(\d+)\s*,\s*(\d+)\s*>\s*([A-Za-z_]\w*)")


def header_fields(path=HEADER):
    """Every field tv5725.h declares, as {name: (seg, reg, offset, width)}."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        return {name: (int(seg, 16), int(reg, 16), int(off), int(width))
                for seg, reg, off, width, name in TYPEDEF.findall(handle.read())}


def _span(reg, lo, width):
    """A field's bit range within its segment, counting from the segment's byte
    0. The datasheet's wide blocks run across registers, so containment cannot
    be tested a register at a time."""
    first = reg * 8 + lo
    return first, first + width - 1


def datasheet_fields(path=MERGED):
    """The datasheet extraction, by name and by the bits each entry covers."""
    with open(path, encoding="utf-8") as handle:
        merged = json.load(handle)
    by_name = {n: e["desc"] for n, e in merged.items() if e.get("desc")}
    by_addr = {(e["seg"], e["reg"], e["lo"], e["width"]): e["desc"]
               for e in merged.values() if e.get("desc")}
    # WIDEST first. A field with no entry of its own belongs to the block it
    # sits in, and any narrower entry covering it is a SIBLING, about different
    # bits entirely.
    blocks = sorted(((n, e["seg"]) + _span(e["reg"], e["lo"], e["width"])
                     + (e["reg"], e["width"]) for n, e in merged.items()),
                    key=lambda b: b[2] - b[3])
    return by_name, by_addr, blocks


def partition(fields):
    """The header's fields minus the ones that merely wrap other fields.

    tv5725.h declares both `PLL648_CONTROL_01` and the five documented fields
    inside it, so the same bits carry two names. That is two owners: a write
    through the byte silently rewrites everything the finer names describe, and
    a panel listing both offers the reader that mistake. Keep the finest naming
    available and drop whatever contains it.

    A byte with nothing finer inside it is not a container — it is the only name
    those bits have — so it stays whether or not the datasheet knows it.
    """
    spans = {n: (s,) + _span(r, o, w)
             for n, (s, r, o, w) in fields.items()}
    keep = {}
    for name, (seg, lo, hi) in spans.items():
        wraps = any(s == seg and lo <= flo and fhi <= hi and fhi - flo < hi - lo
                    for other, (s, flo, fhi) in spans.items() if other != name)
        if not wraps:
            keep[name] = fields[name]
    return keep


def build(header_path=HEADER, merged_path=MERGED, partitioned=True):
    """The map: what tv5725.h declares, as a partition, described where it can be.

    A field is documented if the datasheet names it, or covers its exact bits
    under another name, or sits inside a wider entry.

    That last group does NOT inherit the block's text. Segment 0's status is
    documented as a few multi-byte blocks — SYNC_PROC_STATUS_ is 56 bits over
    s0_16..s0_1C — and the extraction gives each block the description of one
    arbitrary row inside it, so SYNC_PROC_STATUS_ reads "H low pulse length
    value". Inheriting that captions STATUS_SYNC_PROC_VTOTAL with a different
    field's meaning, which is the failure this whole chain exists to remove.
    They get a pointer to the block instead: true, and it says where to look.
    """
    by_name, by_addr, blocks = datasheet_fields(merged_path)
    out = {}
    fields = header_fields(header_path)
    # tv5725.h keeps its convenience typedefs — the firmware writes through
    # them — so rewording the header asks for all of them. Only the panel wants
    # the partition.
    for name, (seg, reg, off, width) in (partition(fields) if partitioned
                                         else fields).items():
        lo, hi = _span(reg, off, width)
        inside = None
        desc = by_name.get(name) or by_addr.get((seg, reg, off, width))
        if not desc:
            block = next(((n, r, w) for n, s, slo, shi, r, w in blocks
                          if s == seg and slo <= lo and hi <= shi), None)
            if block:
                inside = block[0]
                desc = (f"Part of {inside}, which RD-5725-1.1 documents as one "
                        f"{block[2]}-bit block at s{seg}_{block[1]:02X} rather "
                        f"than field by field.")
        out[name] = {"seg": seg, "reg": reg, "off": off,
                     "width": width, "desc": desc or ""}
        if inside:
            out[name]["block"] = inside
    return out


def main():
    fields = build()
    with open(OUT, "w", encoding="utf-8") as handle:
        json.dump(fields, handle, indent=1, sort_keys=True)
    declared = len(header_fields())
    print(f"{len(fields)} documented fields of {declared} declared -> {OUT}")
    for probe in ("VDS_HB_ST", "PLLAD_MD", "CAP_SAFE_GUARD_A", "HPERIOD_IF"):
        e = fields.get(probe)
        print(f"\n{probe}: " + (f"s{e['seg']} 0x{e['reg']:02X} b{e['off']}:"
                                f"{e['width']}\n    {e['desc'][:140]}"
                                if e else "absent"))


if __name__ == "__main__":
    main()
