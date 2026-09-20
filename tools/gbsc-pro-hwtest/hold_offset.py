#!/usr/bin/env python3
"""Hold a field at the engine's own value plus an offset, across re-solves.

For judging a one-unit change by eye on a source that keeps changing mode. A
single write is undone by the next solve, and freezing instead strands the
picture on the previous mode's geometry -- so neither survives a bench where the
source is being swept.

    python3 tools/gbsc-pro-hwtest/hold_offset.py --host 192.168.88.108 \
        --field VDS_DIS_HB_SP --offset 1

Whatever the engine writes becomes the new base and the offset is re-applied on
top, so the experiment follows every mode change instead of being invalidated by
one. A value this tool did not write is the engine's; a value it did is left
alone. Ctrl-C restores the engine's last base and exits.

The offset is applied to ONE field, so a caller offsetting something the solver
derives another register from will make the two disagree. This is an instrument
for judging an edge, not a way to configure the unit.
"""
import argparse
import sys
import time

import setfield
from gbs_unit import read_named


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--field", required=True)
    ap.add_argument("--offset", type=int, required=True)
    ap.add_argument("--interval", type=float, default=0.4)
    args = ap.parse_args()

    spec = setfield.load_map().get(args.field)
    if spec is None:
        sys.exit(f"not in tv5725_registers.json: {args.field}")

    base = None      # the engine's value, as last seen
    written = None   # what this tool put there on top of it
    print(f"holding {args.field} at engine value {args.offset:+d}; Ctrl-C restores")

    try:
        while True:
            now = read_named(args.host, args.field)
            if now is not None and now != written:
                base = now
                written = base + args.offset
                print(f"  engine wrote {base}; holding {written}")
                setfield.apply(args.host, args.field, spec, written, False)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print()
        if base is not None:
            print(f"restoring {args.field} to the engine's {base}")
            setfield.apply(args.host, args.field, spec, base, False)


if __name__ == "__main__":
    sys.exit(main())
