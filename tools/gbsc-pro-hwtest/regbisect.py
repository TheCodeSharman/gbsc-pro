#!/usr/bin/env python3
"""Bisect a register difference between a known-good and a known-bad dump.

    python3 tools/gbsc-pro-hwtest/regbisect.py --host <ip> \\
        --good snapshots/zigzag-bisect-good-2026-08-09.json \\
        --bad  snapshots/zigzag-bisect-bad-2026-08-09.json  list

    ... --host <ip> --good G --bad B good           # restore the good state
    ... --host <ip> --good G --bad B apply 3,4,5    # bad bytes 3,4,5 on top

Every run starts by restoring the GOOD state for all differing bytes, then
writes the chosen subset of BAD values on top. So each observation is the good
state plus exactly the subset named, and no run depends on the one before it.

**Freeze the unit first** (`/freeze?on=1`), or the sync watcher writes over the
experiment between the write and the look.

**WHOLE BYTES, NEVER FIELDS.** Differing fields share bytes -- VDS_HSCALE and
VDS_VSCALE both live in s3 0x17 -- and some differing bytes have no field in the
map at all, so a field-level bisect can neither separate the two scales nor
address the unmapped bytes.

The method needs no theory about what the registers mean, only a good state, a
bad state, and an eye that can say which one is on screen. Two rules that make it
work:

- **Split by kind before halving.** Geometry against everything else keeps the
  picture the same size and makes the comparison honest. Blind halving produces
  states like a bad capture window with good scales, where the picture overflows
  the raster and is its own artefact.
- **Re-verify the fault reproduces.** When neither half reproduces it, apply
  every differing byte to confirm the state is still faulty rather than that the
  bisect has gone wrong -- the cause may need one byte from each half.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import write_reg


def differences(good_path, bad_path):
    """Every config byte that differs, ordered by segment then register."""
    good = json.load(open(good_path))["config"]
    bad = json.load(open(bad_path))["config"]

    def order(key):
        seg, reg = key.split(":")
        return int(seg), int(reg, 16)

    out = []
    for key in sorted(good, key=order):
        if key in bad and good[key] != bad[key]:
            seg, reg = key.split(":")
            out.append((int(seg), int(reg, 16), good[key], bad[key]))
    return out


def as_int(value):
    return int(value, 16) if isinstance(value, str) else value


def apply(host, subset, which):
    for seg, reg, good, bad in subset:
        write_reg(host, seg, reg, as_int(good if which == "good" else bad))
        time.sleep(0.05)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True)
    parser.add_argument("--good", required=True, help="known-good dump")
    parser.add_argument("--bad", required=True, help="known-bad dump")
    parser.add_argument("action", choices=["list", "good", "apply"])
    parser.add_argument("indices", nargs="?", default="",
                        help="for `apply`: comma-separated indices from `list`")
    args = parser.parse_args(argv)

    diffs = differences(args.good, args.bad)

    if args.action == "list":
        for i, (seg, reg, good, bad) in enumerate(diffs):
            print(f"  [{i:2d}] s{seg} 0x{reg:02x}   good {good}  ->  bad {bad}")
        print(f"{len(diffs)} bytes differ")
        return 0

    apply(args.host, diffs, "good")
    if args.action == "good":
        print(f"restored the good state for all {len(diffs)} bytes")
        return 0

    time.sleep(0.3)
    chosen = [diffs[int(i)] for i in args.indices.split(",") if i.strip()]
    apply(args.host, chosen, "bad")
    print(f"applied {len(chosen)} BAD bytes on top of the good state:")
    for seg, reg, good, bad in chosen:
        print(f"    s{seg} 0x{reg:02x}  {good} -> {bad}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
