#!/usr/bin/env python3
"""Give the memory window a margin off the write floor, and mark each step.

On the write floor `VDS_HB_SP` is pinned at 8, and the picture is broken there
at every `VDS_HSCALE` except the multiples of 64. Above the clamp every scale is
clean, so neither the floor nor the scale explains the fault alone -- it takes
both, and what the floor contributes is unmeasured.

This is the motion that asks. It moves the window start and the corner together
in steps of 2: together because moving `VDS_HB_SP` alone opens the display
window before the write starts and shows unwritten memory down the left, and by
2 because an even memory window shears and the parity would otherwise alternate
under the step.

    python3 tools/gbsc-pro-hwtest/creep_margin.py --host 192.168.88.108

Park on a CORRUPT write-floor framing first -- any scale on the floor that is
not a multiple of 64, which is all but one of them. The floor scale rule
predicts corrupt at every margin, since the scale does not move. A clean mark
refutes it and makes the fix a margin on `windowStopMin`, costing a few units of
picture and no zoom range.

The far edge does not move, so the picture loses `delta` units off its right as
the margin grows. Judge the corruption, not the framing.

Automation is frozen while it runs. Both registers and the freeze are restored
on the way out.

It does not jump and it does not bisect, for the reasons in CLAUDE.md.
"""
import argparse
import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import read_fields                       # noqa: E402
from shear import (FloorScaleRule, STATE, Session, derive,  # noqa: E402
                   margin_targets, freeze, unsolved, write_field)

HELP = """
  Enter / +   +2          -       -2
  ]           +6          [       -6
  c           mark this margin CLEAN
  x           mark this margin CORRUPT
  s           show the arithmetic again
  ?           this list
  q           quit, restore, and print the marks
"""


def describe(state):
    d = derive(state)
    parts = [f"VDS_HB_SP {state['VDS_HB_SP']}",
             f"corner {state['VDS_DIS_HB_SP']}"]
    if d["magnification"]:
        parts.append(f"HSCALE {state['VDS_HSCALE']} (x{d['magnification']:.4f})")
    if d["width"] is not None:
        parts.append(f"width {d['width']} {'even' if d['width'] % 2 == 0 else 'odd'}")
    if d["origin"] is not None:
        parts.append(f"origin {d['origin']:.4f}")
    return "  ".join(parts)


def apply_margin(host, state, targets):
    """Both near edges, or neither: a half-applied pair is not a framing."""
    for name, value in targets.items():
        if not write_field(host, name, value):
            for undo, held in state.items():
                if undo in targets:
                    write_field(host, undo, held)
            return None
    fresh = dict(state)
    fresh.update(targets)
    return fresh


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "sessions", "creep_margin-"
                               + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                               + ".json")
    session = Session(args.host, args.out or default_out, tool="creep_margin",
                      rule=FloorScaleRule)

    solved = read_fields(args.host, STATE)
    if solved is None:
        print("could not read the unit")
        return 1

    why = unsolved(solved)
    if why:
        print(f"this is not a solved state: {why}")
        return 1
    if not FloorScaleRule.governs(solved):
        print(f"VDS_HB_SP is {solved['VDS_HB_SP']}, not on the write floor; "
              "zoom in until it pins at 8")
        return 1
    if FloorScaleRule(solved) == "clean":
        print(f"VDS_HSCALE {solved['VDS_HSCALE']} is a multiple of 64 and is "
              "already clean; park on any other scale on the floor")
        return 1

    restore = {"VDS_HB_SP": solved["VDS_HB_SP"],
               "VDS_DIS_HB_SP": solved["VDS_DIS_HB_SP"]}

    if not freeze(args.host, True):
        print("could not freeze automation; the engine would rewrite both")
        return 1
    print(f"frozen. {restore} restores on the way out.")
    print(describe(solved))
    print("the floor scale rule predicts corrupt at every margin here")
    print(HELP)

    state = solved
    try:
        while True:
            try:
                reply = input(f"[margin +{state['VDS_HB_SP'] - restore['VDS_HB_SP']}] ").strip()
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
                session.mark(fresh["VDS_HB_SP"], verdict, fresh)
                called = "AGREES" if verdict == expected else "*** REFUTES ***"
                print(f"  marked {verdict}, the floor scale rule predicted "
                      f"{expected}  {called}")
                print(f"    {describe(fresh)}")
                continue

            if reply == "s":
                fresh = read_fields(args.host, STATE)
                print("  " + (describe(fresh) if fresh else "read failed"))
                continue

            step = {"": 2, "+": 2, "-": -2, "]": 6, "[": -6}.get(reply)
            if step is None:
                print("  ? for the keys")
                continue

            targets = margin_targets(state, step)
            if targets is None:
                print("  8 is the floor")
                continue
            moved = apply_margin(args.host, state, targets)
            if moved is None:
                print("  write failed; both edges put back")
                continue
            state = moved
    finally:
        for name, value in restore.items():
            write_field(args.host, name, value)
        freeze(args.host, False)
        print(f"\nrestored {restore}, automation running again\n")
        print(session.table())
        if session.marks:
            print(f"\n{session.score()}")
            if session.path:
                print(session.path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
