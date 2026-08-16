#!/usr/bin/env python3
"""Capture a full register snapshot, or diff two of them by field name.

    python3 tools/gbsc-pro-hwtest/snapdiff.py --host 192.168.88.108 \
        --save snapshots/fills-corrupt.json --note "320x256 full screen, corrupted"

    python3 tools/gbsc-pro-hwtest/snapdiff.py \
        --diff snapshots/clean.json snapshots/fills-corrupt.json

Reads all six segments over /getregs, so a capture is a handful of requests
rather than 1536. The diff resolves each changed byte to the field names that
live in it and decodes their values, because "s3 0x16 went 0x83 -> 0x00" is not
something anyone can reason about, and "VDS_HSCALE 387 -> 512" is.

Segment 0 registers below 0x30 are live measurements that change on every read;
they are captured but excluded from the diff unless --all is passed.
"""

import argparse
import json
import os
import urllib.request

VOLATILE = {(0, r) for r in range(0x00, 0x30)}
HERE = os.path.dirname(os.path.abspath(__file__))


def capture(host):
    config = {}
    for segment in range(6):
        for first, last in ((0x00, 0x7F), (0x80, 0xFF)):
            url = f"http://{host}/getregs?s={segment:x}&from={first:02x}&to={last:02x}"
            with urllib.request.urlopen(url, timeout=10) as response:
                values = json.loads(response.read())["values"]
            for i in range(len(values) // 2):
                config[f"{segment}:{first + i:02x}"] = int(values[2 * i : 2 * i + 2], 16)
    return config


def load_map():
    with open(os.path.join(HERE, "tv5725_registers.json")) as handle:
        return json.load(handle)


def fields_at(register_map, segment, register):
    """Every field whose bytes include this register, so a changed byte reports
    the fields it actually affects rather than just its address."""
    hits = []
    for name, spec in register_map.items():
        if spec["seg"] != segment:
            continue
        span = (spec["off"] + spec["width"] + 7) // 8
        if spec["reg"] <= register < spec["reg"] + span:
            hits.append((name, spec))
    return hits


def value_of(config, spec):
    span = (spec["off"] + spec["width"] + 7) // 8
    raw = 0
    for i in range(span):
        byte = config.get(f"{spec['seg']}:{spec['reg'] + i:02x}")
        if byte is None:
            return None
        raw |= byte << (8 * i)
    return (raw >> spec["off"]) & ((1 << spec["width"]) - 1)


def diff(before, after, register_map, include_volatile=False):
    a = before["config"] if "config" in before else before
    b = after["config"] if "config" in after else after

    changed_fields = {}
    changed_bytes = []
    for key in sorted(set(a) | set(b)):
        segment, register = int(key.split(":")[0]), int(key.split(":")[1], 16)
        if not include_volatile and (segment, register) in VOLATILE:
            continue
        if a.get(key) == b.get(key):
            continue
        changed_bytes.append((key, a.get(key), b.get(key)))
        for name, spec in fields_at(register_map, segment, register):
            old, new = value_of(a, spec), value_of(b, spec)
            if old != new:
                changed_fields[name] = (old, new, spec)

    # Every field in the map is datasheet-attested now, so there is no second
    # group to separate out. A byte that resolves to no field still shows in the
    # byte count above, which is what keeps an undocumented change visible.
    print(f"{len(changed_bytes)} bytes differ, resolving to {len(changed_fields)} fields\n")
    for name in sorted(changed_fields):
        old, new, spec = changed_fields[name]
        print(f"    {name:26} {str(old):>7} -> {str(new):<7}"
              f"  (s{spec['seg']} 0x{spec['reg']:02x} b{spec['off']} w{spec['width']})")
    print()

    orphans = [k for k, x, y in changed_bytes
               if not fields_at(register_map, int(k.split(':')[0]), int(k.split(':')[1], 16))]
    if orphans:
        print(f"  --- bytes with no field in the map ({len(orphans)}) ---")
        for key, old, new in changed_bytes:
            if key in orphans:
                print(f"    s{key}  0x{old:02x} -> 0x{new:02x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host")
    parser.add_argument("--save", help="capture from --host and write here")
    parser.add_argument("--note", default="")
    parser.add_argument("--diff", nargs=2, metavar=("BEFORE", "AFTER"))
    parser.add_argument("--all", action="store_true", help="include segment 0 live registers")
    args = parser.parse_args()

    if args.diff:
        register_map = load_map()
        with open(args.diff[0]) as f:
            before = json.load(f)
        with open(args.diff[1]) as f:
            after = json.load(f)
        for path, snap in zip(args.diff, (before, after)):
            print(f"  {path}: {snap.get('note', '(no note)')}")
        print()
        diff(before, after, register_map, args.all)
        return

    if not (args.host and args.save):
        parser.error("give --diff BEFORE AFTER, or --host and --save")

    config = capture(args.host)
    with open(args.save, "w") as handle:
        json.dump({"note": args.note, "host": args.host, "config": config}, handle, indent=1)
    print(f"saved {len(config)} registers to {args.save}")
    if args.note:
        print(f"  note: {args.note}")


if __name__ == "__main__":
    main()
