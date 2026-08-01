#!/usr/bin/env python3
"""Live view of the scaler's sync processor, for correlating with a scope.

    python3 tools/gbsc-pro-hwtest/sync_monitor.py --host 192.168.88.108

Polls STATUS_16 (segment 0, register 0x16) over /getreg and prints what the chip
thinks is arriving on its H and V sync inputs. Put a probe on the VGA input at
the same time and the two answer different halves of the same question: whether
sync is present on the wire, and whether the scaler sees it.

The bits, from the sync watcher's own comment block:

    bit 0  HS polarity        bit 1  HS active
    bit 2  VS polarity        bit 3  VS active

`stable` in the firmware is (STATUS_16 & 0x0a) == 0x0a — H active *and* V active
at the same instant. Ctrl-C for a summary.
"""

import argparse
import collections
import sys
import time

from gbs_unit import get, read_reg, read_word


def describe(value):
    return (
        f"H={'ACTIVE' if value & 0x02 else '  --  '}({'+' if value & 0x01 else '-'}) "
        f"V={'ACTIVE' if value & 0x08 else '  --  '}({'+' if value & 0x04 else '-'})"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--totals", action="store_true",
                    help="also read HTOTAL/VTOTAL — three times the requests per sample")
    args = ap.parse_args()

    status, _ = get(args.host, "/wifi/status")
    if status != 200:
        print(f"error: {args.host} did not answer (status {status})", file=sys.stderr)
        return 2

    seen = collections.Counter()
    locks = []
    started = time.time()
    interactive = sys.stdout.isatty()
    previous = None
    print("polling STATUS_16 — Ctrl-C to stop")
    print("(only changes are printed; a steady reading means a steady input)\n")

    try:
        while True:
            value = read_reg(args.host, 0, 0x16)
            if value is None:
                continue
            seen[value] += 1
            elapsed = time.time() - started
            both = (value & 0x0A) == 0x0A
            if both:
                locks.append(elapsed)

            line = f"{elapsed:7.1f}s  0x{value:02x}  {describe(value)}"
            if args.totals:
                htotal = read_word(args.host, 0, 0x17, 0x0FFF)
                vtotal = read_word(args.host, 0, 0x1B, 0x07FF)
                line += f"  ht={htotal:<5} vt={vtotal:<5}"
            if both:
                line += "   <<< H+V TOGETHER"

            if value != previous or both:
                print(line, flush=True)
                previous = value
            elif interactive:
                print(f"\r{line}   ", end="", flush=True)
    except KeyboardInterrupt:
        pass

    total = sum(seen.values())
    if not total:
        print("\nno samples")
        return 1

    h = sum(n for value, n in seen.items() if value & 0x02)
    v = sum(n for value, n in seen.items() if value & 0x08)
    print(f"\n\n{total} samples over {time.time() - started:.0f}s")
    print(f"  H active : {h:5d}  ({100 * h / total:5.1f}%)")
    print(f"  V active : {v:5d}  ({100 * v / total:5.1f}%)")
    print(f"  both     : {len(locks):5d}  ({100 * len(locks) / total:5.1f}%)  <- what 'stable' needs")
    print("\n  STATUS_16 values seen:")
    for value, count in seen.most_common():
        print(f"    0x{value:02x}  x{count:<6} {describe(value)}")
    if locks:
        preview = ", ".join(f"{t:.1f}s" for t in locks[:10])
        print(f"\n  H+V coincided at: {preview}{' ...' if len(locks) > 10 else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
