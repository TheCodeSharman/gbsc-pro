#!/usr/bin/env python3
"""Jog VDS_HB_SP by hand at one solve, and mark each value clean or corrupt.

The memory window's near edge is a ROUNDED quantity. Axis::placePicture takes
`lrintf(corner - (startConst + startPerMag x magnification))`, so the register
carries a residual that sweeps the full +-0.5 as the zoom steps, and moving the
register by one moves the residual off the rounding entirely. On the bench a
corrupt picture has cleared by one step in both directions -- 33 -> 32 at
magnification 2.06, and 8 -> 9 where the solve hit windowStopMin and clamped.

What this maps is the BAD SET at a fixed solve: which values of the register
corrupt and which do not, with nothing else moving. Moving this register alone
changes the memory window's WIDTH, which is the quantity the shear follows, so
the set alternates by one. `creep_memory_width.py` moves the width without
moving this edge.

    python3 tools/gbsc-pro-hwtest/creep_hb_sp.py --host 192.168.88.108

Automation is frozen while it runs, because the engine rewrites this register on
every solve. The register and the freeze are both restored on the way out.

Two things it deliberately does NOT do. It does not jump: a transition that
happens between two frames the eye never saw is not a measurement. And it does
not bisect: corruption on this board comes in bands, so a boundary found by
halving is only the real one if nothing clean lies beyond it.
"""
import argparse
import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import read_fields                       # noqa: E402
from shear import (STATE, Session, clamped, describe, freeze,  # noqa: E402
                   predict, write_field)

HELP = """
  Enter / +   +1          -       -1
  ]           +5          [       -5
  c           mark this value CLEAN
  x           mark this value CORRUPT
  s           show the arithmetic again
  ?           this list
  q           quit, restore, and print the marks
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "sessions", "creep_hb_sp-"
                               + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                               + ".json")
    session = Session(args.host, args.out or default_out, tool="creep_hb_sp")

    state = read_fields(args.host, STATE)
    if state is None:
        print("could not read the unit")
        return 1
    restore = state["VDS_HB_SP"]

    if not freeze(args.host, True):
        print("could not freeze automation; the engine would rewrite the register")
        return 1
    print(f"frozen. VDS_HB_SP restores to {restore} on the way out.")
    print(describe(restore, state))
    print(HELP)

    value = restore
    try:
        while True:
            try:
                reply = input(f"[{value}] ").strip()
            except EOFError:
                break

            if reply in ("q", "quit"):
                break
            if reply == "?":
                print(HELP)
                continue

            if reply in ("c", "x"):
                fresh = read_fields(args.host, STATE)
                if fresh is None:
                    print("  read failed; not marked")
                    continue
                verdict = "clean" if reply == "c" else "corrupt"
                expected = predict(fresh)
                session.mark(value, verdict, fresh)
                called = ("the clamp governs here, not the width"
                          if clamped(fresh)
                          else "AGREES" if verdict == expected else "*** REFUTES ***")
                print(f"  marked {verdict}, width rule predicted {expected}  {called}")
                print(f"    {describe(value, fresh)}")
                continue

            if reply == "s":
                fresh = read_fields(args.host, STATE)
                print("  " + (f"{describe(value, fresh)}   predicts {predict(fresh)}"
                              if fresh else "read failed"))
                continue

            step = {"": 1, "+": 1, "-": -1, "]": 5, "[": -5}.get(reply)
            if step is None:
                print("  ? for the keys")
                continue

            wanted = value + step
            if wanted < 0:
                print("  0 is the floor")
                continue
            if write_field(args.host, "VDS_HB_SP", wanted):
                value = wanted
    finally:
        write_field(args.host, "VDS_HB_SP", restore)
        freeze(args.host, False)
        print(f"\nrestored VDS_HB_SP {restore}, automation running again\n")
        print(session.table())
        if session.marks:
            print(f"\n{session.score()}")
            if session.path:
                print(session.path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
