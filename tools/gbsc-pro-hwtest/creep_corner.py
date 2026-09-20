#!/usr/bin/env python3
"""Jog VDS_DIS_HB_SP by hand at one solve, and mark each value clean or corrupt.

The corner is where the display window opens, and it is the one register the
write floor's rounding lands in directly -- `lrintf(windowStopMin +
originOffset)`. Moving it alone is the only motion that can part it from the
write origin, since a solve makes them equal by construction.

**Run at both ends of the question and the corner is dead.** At scale 320 and
capture 534, where the origin is exactly 143, the picture is clean at every
value from 139 to 146; at scale 316, where the origin is 144.0127, it is
corrupt at every value from 144 to 150. An exact framing survives the corner
moved four units off the origin and a fractional one is rescued by no corner at
all, so the corner does not reach the picture and the rule that rested on it is
gone. What survives is the scale, which is what `shear.floor_scale` predicts.

It stays because the motion is reusable and nothing else has taken a register to
the write floor -- `creep_hb_sp.py`, `creep_memory_phase.py` and
`creep_memory_width.py` have 96 marks between them and not one with `VDS_HB_SP`
on the floor. `VDS_DIS_VB_SP` has never been crept at all, so the vertical axis
is unmeasured rather than unaffected.

    python3 tools/gbsc-pro-hwtest/creep_corner.py --host 192.168.88.108

Automation is frozen while it runs, because the engine rewrites this register on
every solve. The register and the freeze are both restored on the way out.

It does not jump and it does not bisect, for the reasons in CLAUDE.md.
"""
import argparse
import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import read_fields                       # noqa: E402
from shear import (FloorScaleRule, STATE, Session, derive, freeze,  # noqa: E402
                   unsolved, write_field)

HELP = """
  Enter / +   +1          -       -1
  ]           +5          [       -5
  c           mark this value CLEAN
  x           mark this value CORRUPT
  s           show the arithmetic again
  ?           this list
  q           quit, restore, and print the marks
"""


def describe(value, state):
    """The corner against the origin it is supposed to sit on."""
    d = derive(state)
    parts = [f"VDS_DIS_HB_SP {value}"]
    if d["magnification"]:
        parts.append(f"HSCALE {state['VDS_HSCALE']} (x{d['magnification']:.4f})")
    if d["capture"] is not None:
        parts.append(f"capture {d['capture']}")
    if d["origin"] is not None:
        parts.append(f"origin {d['origin']:.4f}")
    if d["phase"] is not None:
        parts.append(f"phase {d['phase']:+.4f}")
    return "  ".join(parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "sessions", "creep_corner-"
                               + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                               + ".json")
    session = Session(args.host, args.out or default_out, tool="creep_corner",
                      rule=FloorScaleRule)

    state = read_fields(args.host, STATE)
    if state is None:
        print("could not read the unit")
        return 1

    # A jog from a state no solve produced measures the restore, not the rule.
    why = unsolved(state)
    if why:
        print(f"this is not a solved state: {why}")
        return 1

    restore = state["VDS_DIS_HB_SP"]

    if not freeze(args.host, True):
        print("could not freeze automation; the engine would rewrite the register")
        return 1
    print(f"frozen. VDS_DIS_HB_SP restores to {restore} on the way out.")
    print(describe(restore, state))
    print(f"the floor scale rule predicts this state {FloorScaleRule(state)}")
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
                expected = FloorScaleRule(fresh)
                session.mark(value, verdict, fresh)
                called = ("the floor scale rule does not govern here"
                          if not FloorScaleRule.governs(fresh)
                          else "AGREES" if verdict == expected else "*** REFUTES ***")
                print(f"  marked {verdict}, the floor scale rule predicted {expected}"
                      f"  {called}")
                print(f"    {describe(value, fresh)}")
                continue

            if reply == "s":
                fresh = read_fields(args.host, STATE)
                print("  " + (f"{describe(value, fresh)}   predicts "
                              f"{FloorScaleRule(fresh)}" if fresh else "read failed"))
                continue

            step = {"": 1, "+": 1, "-": -1, "]": 5, "[": -5}.get(reply)
            if step is None:
                print("  ? for the keys")
                continue

            wanted = value + step
            if wanted < 0:
                print("  0 is the floor")
                continue
            if write_field(args.host, "VDS_DIS_HB_SP", wanted):
                value = wanted
    finally:
        write_field(args.host, "VDS_DIS_HB_SP", restore)
        freeze(args.host, False)
        print(f"\nrestored VDS_DIS_HB_SP {restore}, automation running again\n")
        print(session.table())
        if session.marks:
            print(f"\n{session.score()}")
            if session.path:
                print(session.path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
