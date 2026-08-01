#!/usr/bin/env python3
"""Snapshot the scaler's register configuration, and diff two snapshots.

    python3 dump_registers.py --host 192.168.88.108 --out working.json
    python3 dump_registers.py --diff working.json broken.json

Status registers (segment 0, 0x00-0x2F) are live measurements rather than
configuration and change on every read, so they are captured under "status" and
left out of the diff unless --all is passed.
"""

import argparse
import json
import sys
import time

from gbs_unit import get, read_reg

# Configuration registers worth capturing, as (segment, first, last).
# The preset ranges from gbs-control.ino, widened on segment 1 to take in the
# HD_* bypass registers at 0x37-0x49 and on segment 5 for the sync processor.
CONFIG_RANGES = [
    (0, 0x40, 0x5F),
    (0, 0x90, 0x9F),
    (1, 0x00, 0x5F),
    (3, 0x00, 0x7F),
    (4, 0x00, 0x5F),
    (5, 0x00, 0x7F),
]

# Live measurements — captured for context, excluded from diffs by default.
STATUS_RANGES = [(0, 0x00, 0x2F)]

# Registers worth naming in the output, from tv5725.h.
NAMES = {
    (0, 0x16): "STATUS_16 (HS/VS pol+active)",
    (0, 0x17): "SYNC_PROC_HTOTAL lo",
    (0, 0x18): "SYNC_PROC_HTOTAL hi",
    (0, 0x19): "SYNC_PROC_HLOW_LEN lo",
    (0, 0x1B): "SYNC_PROC_VTOTAL lo",
    (0, 0x1C): "SYNC_PROC_VTOTAL hi",
    (1, 0x37): "HD_HSYNC_RST lo",
    (1, 0x39): "HD_INI_ST lo",
    (1, 0x3B): "HD_HB_ST lo",
    (1, 0x47): "HD_VS_ST lo",
    (3, 0x01): "VDS_HSYNC_RST lo",
    (3, 0x02): "VDS_VSYNC_RST lo",
    (3, 0x16): "VDS_HSCALE lo",
    (5, 0x20): "SP_SOG_SRC_SEL / EXT_SYNC_SEL(bit3)",
    (5, 0x56): "SP_SOG_MODE(bit0)",
}


def read_ranges(host, ranges, label):
    out = {}
    total = sum(last - first + 1 for _, first, last in ranges)
    done = 0
    for segment, first, last in ranges:
        for reg in range(first, last + 1):
            value = read_reg(host, segment, reg)
            done += 1
            if done % 40 == 0:
                print(f"  {label}: {done}/{total}", file=sys.stderr)
            if value is not None:
                out[f"{segment}:{reg:02x}"] = value
    return out


def dump(host, path, note):
    status, _ = get(host, "/wifi/status")
    if status != 200:
        print(f"error: {host} did not answer (status {status})", file=sys.stderr)
        return 2

    print(f"reading configuration registers from {host} ...", file=sys.stderr)
    config = read_ranges(host, CONFIG_RANGES, "config")
    live = read_ranges(host, STATUS_RANGES, "status")

    snapshot = {
        "host": host,
        "note": note,
        "captured": time.strftime("%Y-%m-%d %H:%M:%S"),
        "config": config,
        "status": live,
    }
    with open(path, "w") as f:
        json.dump(snapshot, f, indent=1, sort_keys=True)
    print(f"\nwrote {path}: {len(config)} config + {len(live)} status registers")
    return 0


def diff(a_path, b_path, include_status):
    a = json.load(open(a_path))
    b = json.load(open(b_path))
    sections = ["config"] + (["status"] if include_status else [])

    any_diff = False
    for section in sections:
        left, right = a.get(section, {}), b.get(section, {})
        keys = sorted(set(left) | set(right))
        rows = [(k, left.get(k), right.get(k)) for k in keys if left.get(k) != right.get(k)]
        print(f"\n=== {section}: {len(rows)} differing of {len(keys)} ===")
        if not rows:
            continue
        any_diff = True
        print(f"  {'register':<10} {'A':>6} {'B':>6}   name")
        for key, x, y in rows:
            segment, reg = key.split(":")
            name = NAMES.get((int(segment), int(reg, 16)), "")
            xs = "--" if x is None else f"0x{x:02x}"
            ys = "--" if y is None else f"0x{y:02x}"
            print(f"  s{segment} 0x{reg:<5} {xs:>6} {ys:>6}   {name}")

    print(f"\nA: {a.get('note','')}  ({a.get('captured','')})")
    print(f"B: {b.get('note','')}  ({b.get('captured','')})")
    return 0 if not any_diff else 1


def restore(host, path, segments, repeat):
    """Write a snapshot's registers back, for the segments named. Separates cause
    from effect: if a known-good configuration makes the unit lock, the signal was
    fine. Nothing is persisted, and the firmware overwrites as it hunts."""
    from gbs_unit import write_reg

    snapshot = json.load(open(path))
    wanted = {k: v for k, v in snapshot["config"].items()
              if int(k.split(":")[0]) in segments}
    print(f"restoring {len(wanted)} registers in segments {sorted(segments)} "
          f"from {path}\n  ({snapshot.get('note','')})\n")

    for round_ in range(repeat):
        changed = 0
        for key, value in sorted(wanted.items()):
            segment, reg = key.split(":")
            current = read_reg(host, int(segment), int(reg, 16))
            if current != value:
                write_reg(host, int(segment), int(reg, 16), value)
                changed += 1
        state = read_reg(host, 0, 0x16) or 0
        print(f"  pass {round_+1}: rewrote {changed:3d}   STATUS_16=0x{state:02x}"
              f"  H={'yes' if state & 2 else 'no ':>3} V={'yes' if state & 8 else 'no'}"
              + ("   <<< LOCKED" if (state & 0x0A) == 0x0A else ""))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host")
    ap.add_argument("--out")
    ap.add_argument("--note", default="", help="what state this was captured in")
    ap.add_argument("--diff", nargs=2, metavar=("A", "B"))
    ap.add_argument("--all", action="store_true", help="include status registers in the diff")
    ap.add_argument("--restore", metavar="SNAPSHOT",
                    help="write a snapshot's registers back (needs --host)")
    ap.add_argument("--segments", default="5",
                    help="comma-separated segments to restore, default 5 (sync processor)")
    ap.add_argument("--repeat", type=int, default=6,
                    help="restore passes, to fight the firmware rewriting as it hunts")
    args = ap.parse_args()

    if args.diff:
        return diff(args.diff[0], args.diff[1], args.all)
    if args.restore:
        if not args.host:
            ap.error("--restore needs --host")
        return restore(args.host, args.restore,
                       {int(s) for s in args.segments.split(",")}, args.repeat)
    if not (args.host and args.out):
        ap.error("need --host and --out to capture, --diff A B, or --restore SNAPSHOT")
    return dump(args.host, args.out, args.note)


if __name__ == "__main__":
    sys.exit(main())
