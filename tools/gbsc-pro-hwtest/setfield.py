#!/usr/bin/env python3
"""Write scaler fields by name, read-modify-write, and verify the read-back.

    python3 tools/gbsc-pro-hwtest/setfield.py --host 192.168.88.108 \
        --set IF_HB_SP2=252 --set IF_HB_ST2=984 --set VDS_HSCALE=312

    ... --dry-run          show the byte writes without issuing them

Every horizontal geometry field on this chip shares its bytes with a neighbour --
VDS_HB_ST's top nibble lives in the same byte as VDS_HB_SP's bottom one, and
IF_HB_SP2's three high bits sit in a byte with something else -- so poking a byte
with the value you wanted the field to have silently destroys the neighbour.
This reads each affected byte, replaces only the field's bits, and writes it back.

Writes happen in the order given, which is the caller's to choose: a sequence
that leaves the scaler producing more pixels than the memory window holds, even
for one intermediate step, will visibly corrupt the picture on the way through.

Field names and their bit positions come from tv5725_registers.json, the same map
snapdiff.py decodes with.
"""

import argparse
import json
import os
import sys

import gbs_unit

HERE = os.path.dirname(os.path.abspath(__file__))


def load_map():
    with open(os.path.join(HERE, "tv5725_registers.json")) as handle:
        return json.load(handle)


def span_of(spec):
    return (spec["off"] + spec["width"] + 7) // 8


def read_field(host, spec):
    return gbs_unit.read_field(host, spec["seg"], spec["reg"], spec["off"], spec["width"])


def byte_writes(host, spec, value):
    """The (register, old, new) byte writes this field change implies, with the
    field's bits replaced and every other bit in those bytes preserved."""
    raw = 0
    for index in range(span_of(spec)):
        byte = gbs_unit.read_reg(host, spec["seg"], spec["reg"] + index)
        if byte is None:
            return None
        raw |= byte << (8 * index)

    mask = ((1 << spec["width"]) - 1) << spec["off"]
    updated = (raw & ~mask) | ((value << spec["off"]) & mask)

    writes = []
    for index in range(span_of(spec)):
        old = (raw >> (8 * index)) & 0xFF
        new = (updated >> (8 * index)) & 0xFF
        if old != new:
            writes.append((spec["reg"] + index, old, new))
    return writes


def apply(host, name, spec, value, dry_run):
    before = read_field(host, spec)
    if before is None:
        print(f"  {name}: could not read, skipped")
        return False

    writes = byte_writes(host, spec, value)
    if writes is None:
        print(f"  {name}: could not read its bytes, skipped")
        return False

    detail = ", ".join(
        f"s{spec['seg']} 0x{reg:02x} 0x{old:02x}->0x{new:02x}" for reg, old, new in writes
    )
    print(f"  {name:16} {before} -> {value}   [{detail or 'no byte change'}]")
    if dry_run:
        return True

    for register, _old, new in writes:
        if gbs_unit.write_reg(host, spec["seg"], register, new) is None:
            print(f"    write to s{spec['seg']} 0x{register:02x} failed")
            return False

    after = read_field(host, spec)
    if after != value:
        print(f"    VERIFY FAILED: reads back {after}, not {value}")
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                        help="repeatable; applied in the order given")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    register_map = load_map()
    changes = []
    for assignment in args.set:
        if "=" not in assignment:
            parser.error(f"--set wants NAME=VALUE, got {assignment!r}")
        name, _, text = assignment.partition("=")
        name = name.strip()
        if name not in register_map:
            parser.error(f"no field named {name!r} in tv5725_registers.json")
        spec = register_map[name]
        value = int(text, 0)
        if not 0 <= value < (1 << spec["width"]):
            parser.error(f"{name} is {spec['width']} bits, {value} does not fit")
        changes.append((name, spec, value))

    ok = True
    for name, spec, value in changes:
        ok = apply(args.host, name, spec, value, args.dry_run) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
