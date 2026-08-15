#!/usr/bin/env python3
"""Reconcile fields that the datasheet splits across consecutive registers.

The PDF names a wide field once per register it occupies:

    s3 0x16  bits 7-0   VDS_HSCALE [7:0]
    s3 0x17  bits 1-0   VDS_HSCALE [9:8]

Treating those as two fields loses the field entirely -- and they are exactly
the ones that matter, since every geometry and timing value (PLLAD_MD,
VDS_HSCALE, VDS_HSYNC_RST, IF_HB_ST ...) is wider than a byte.

Merging rule: group by base name, find the piece carrying field bit 0 -- that
piece gives the segment, register and bit offset the firmware addresses from --
and take the width as the highest field bit plus one. The description is
whichever piece carries the longest trusted text, since the PDF usually
documents the field once, against its low half.
"""
import json, re, sys
from collections import defaultdict

SLICE = re.compile(r"^([A-Z][A-Z0-9_]*?)\s*\[(\d+)\s*:\s*(\d+)\]$")
SINGLE = re.compile(r"^([A-Z][A-Z0-9_]*?)\s*\[(\d+)\]$")


def merge(raw):
    """raw: list of (name, seg, reg, hi, lo, access, desc) as parsed per register."""
    groups = defaultdict(list)
    plain = {}
    for name, seg, reg, hi, lo, acc, desc in raw:
        m = SLICE.match(name) or SINGLE.match(name)
        if m:
            base = m.group(1)
            fhi = int(m.group(2))
            flo = int(m.group(3)) if m.lastindex >= 3 else fhi
            groups[base].append((seg, reg, hi, lo, fhi, flo, acc, desc))
        else:
            plain[name] = (seg, reg, hi, lo, acc, desc)

    out = dict()
    for name, (seg, reg, hi, lo, acc, desc) in plain.items():
        out[name] = {"seg": seg, "reg": reg, "lo": lo, "width": hi - lo + 1,
                     "access": acc, "desc": desc, "pieces": 1}

    for base, pieces in groups.items():
        # the piece holding field bit 0 defines where the firmware addresses from
        anchor = min(pieces, key=lambda p: p[5])
        if anchor[5] != 0:
            continue                       # incomplete capture, don't guess
        width = max(p[4] for p in pieces) + 1
        best = max(pieces, key=lambda p: len(p[7]))
        out[base] = {"seg": anchor[0], "reg": anchor[1], "lo": anchor[3],
                     "width": width, "access": anchor[6], "desc": best[7],
                     "pieces": len(pieces)}
    return out


if __name__ == "__main__":
    sys.path.insert(0, ".")
    import fielddocs
    docs = fielddocs.parse(sys.argv[1], keep_slices=True)
    raw = [(k, v[0], v[1], v[2], v[3], v[4], v[5]) for k, v in docs.items()]
    merged = merge(raw)
    json.dump(merged, open(sys.argv[2], "w"), indent=1, sort_keys=True)
    multi = {k: v for k, v in merged.items() if v["pieces"] > 1}
    print(f"{len(merged)} fields ({len(multi)} reconciled from multiple registers)"
          f" -> {sys.argv[2]}")
    for probe in ("PLLAD_MD", "VDS_HSCALE", "VDS_HSYNC_RST", "IF_HB_ST",
                  "SP_PRE_COAST", "HPERIOD_IF", "VPERIOD_IF"):
        v = merged.get(probe)
        if v:
            print(f"\n{probe}: s{v['seg']} 0x{v['reg']:02X} off {v['lo']} "
                  f"w {v['width']} ({v['pieces']} piece(s))\n    {v['desc'][:150]}")
        else:
            print(f"\n{probe}: absent")
