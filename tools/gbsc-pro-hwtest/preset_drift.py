#!/usr/bin/env python3
"""What moved that the geometry engine does not own.

    python3 tools/gbsc-pro-hwtest/preset_drift.py \
        --reference snapshots/pixel-perfect-from-pan-and-zoom-2026-08-06.json \
        --against   snapshots/clean-x1344-last-line-only-2026-08-08.json

A plain register diff between a Python-driven state and a firmware-driven one is
unreadable, because the geometry engine is *supposed* to have written different
values -- a different zoom means a different capture window, scale and blanking.
Those differences are the tool working, not the fault.

So this splits the diff in two. Fields listed in OWNED are the ones
geometry_regs.h writes; everything else is a register that changed with nobody
in the geometry path claiming responsibility, which is the set worth reading.

Field names and bit positions come from tv5725_registers.json, the same map
snapdiff.py and setfield.py decode with, so a field that straddles two bytes is
reported once by name rather than twice by byte.
"""

import argparse
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# Exactly the writes in geometry_regs.h applyGeometry(). Keep in step with it:
# a field that moves into the engine and not into this list shows up here as
# unexplained drift, which is a false alarm that costs a bench session.
OWNED = {
    "VDS_HB_ST", "VDS_HB_SP", "VDS_VB_ST", "VDS_VB_SP",
    "IF_HB_ST2", "IF_HB_SP2", "IF_VB_ST", "IF_VB_SP",
    "VDS_HSCALE", "VDS_VSCALE", "VDS_HSCALE_BYPS", "VDS_VSCALE_BYPS",
    "VDS_DIS_HB_ST", "VDS_DIS_HB_SP", "VDS_DIS_VB_ST", "VDS_DIS_VB_SP",
}


def load_map():
    with open(os.path.join(HERE, "tv5725_registers.json")) as handle:
        return json.load(handle)


def load_bytes(path):
    """{(segment, register): value} from a dump_registers.py snapshot."""
    with open(path) as handle:
        config = json.load(handle)["config"]
    out = {}
    for key, value in config.items():
        segment, register = key.split(":")
        out[(int(segment), int(register, 16))] = (
            int(value, 16) if isinstance(value, str) else value
        )
    return out


def field(values, spec):
    span = (spec["off"] + spec["width"] + 7) // 8
    raw = 0
    for index in range(span):
        byte = values.get((spec["seg"], spec["reg"] + index))
        if byte is None:
            return None
        raw |= byte << (8 * index)
    return (raw >> spec["off"]) & ((1 << spec["width"]) - 1)


def drift(reference, against, registers):
    owned, unexplained = [], []
    for name, spec in sorted(registers.items()):
        if spec.get("fake"):
            continue
        before, after = field(reference, spec), field(against, spec)
        if before is None or after is None or before == after:
            continue
        row = (name, spec["seg"], spec["reg"], before, after)
        (owned if name in OWNED else unexplained).append(row)
    return owned, unexplained


def report(title, rows):
    print(f"\n{title}  ({len(rows)})")
    if not rows:
        print("    none")
        return
    for name, segment, register, before, after in rows:
        print(f"    {name:26} s{segment} 0x{register:02x}   {before:>6} -> {after:>6}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True, help="the state that worked")
    parser.add_argument("--against", required=True, help="the state to explain")
    args = parser.parse_args()

    registers = load_map()
    owned, unexplained = drift(
        load_bytes(args.reference), load_bytes(args.against), registers
    )

    print(f"reference  {args.reference}")
    print(f"against    {args.against}")
    report("OWNED by the geometry engine -- differences expected", owned)
    report("UNEXPLAINED -- nothing in the geometry path writes these", unexplained)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
