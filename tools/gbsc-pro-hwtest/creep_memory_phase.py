#!/usr/bin/env python3
"""Shift the memory window's PHASE at one solve, and mark each offset.

One quantity lands in three registers. `Axis::solve()` takes

    displayStart = floorf(windowStop + originOffset + produced)
    memory  = (windowStop,      displayStart)      -> VDS_HB_SP, VDS_HB_ST
    display = (corner,          displayStart)      -> VDS_DIS_HB_SP, VDS_DIS_HB_ST

so moving `windowStop` moves VDS_HB_SP, VDS_HB_ST and VDS_DIS_HB_ST together and
leaves VDS_DIS_HB_SP, the picture's corner, where it is. Confirmed against 185
recorded marks: the far pair are equal in all of them and the arithmetic
predicts them in 180.

**Moving VDS_HB_SP alone is a different experiment.** It changes the window's
WIDTH rather than its phase, and it produces a state no solve would emit -- which
is why a jog of that register found a clean parity that whole solves do not obey.
This shifts all three, so every offset is a state the engine would have reached
had its rounding gone the other way.

    python3 tools/gbsc-pro-hwtest/creep_memory_phase.py --host 192.168.88.108

What it answers: at a fixed zoom, does the shear follow the phase? Measured, it
does not -- 35 offsets, all sheared, at widths 1822 and 1814, both even. The
width rule predicts exactly that, because a phase shift holds the width fixed.
`creep_memory_width.py` moves the width and holds the phase instead.

Automation is frozen while it runs, because the engine rewrites all three on
every solve. Everything written is restored on the way out.

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
from shear import (STATE, Session, describe, freeze,   # noqa: E402
                   predict, unsolved, write_field)

# The three the phase lands in. VDS_DIS_HB_SP is the corner and stays put.
NEAR = "VDS_HB_SP"
FAR = ("VDS_HB_ST", "VDS_DIS_HB_ST")

HELP = """
  Enter / +   phase +1      -       phase -1
  ]           phase +4      [       phase -4
  c           mark this phase CLEAN
  x           mark this phase CORRUPT
  s           show the arithmetic again
  ?           this list
  q           quit, restore, and print the marks
"""


def shift(host, live, delta):
    """Shift all three by `delta` from where they are NOW.

    Relative rather than from a baseline read at startup: the framing can move
    under the tool -- the pads still re-solve while automation is frozen -- and a
    baseline captured before that puts the near and far edges different distances
    from the solve, which is a change of WIDTH wearing a phase's name.

    Grown before shrunk, the same ordering VideoPath::write() uses.
    """
    near = live[NEAR] + delta
    far = live["VDS_HB_ST"] + delta
    if near < 0 or far < 0:
        return False
    order = FAR + (NEAR,) if delta > 0 else (NEAR,) + FAR
    for name in order:
        if not write_field(host, name, far if name in FAR else near):
            return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "sessions", "creep_memory_phase-"
                               + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                               + ".json")
    session = Session(args.host, args.out or default_out, tool="creep_memory_phase")

    base = read_fields(args.host, STATE)
    if base is None:
        print("could not read the unit")
        return 1
    problem = unsolved(base)
    if problem:
        print(f"not a state the engine solved: {problem}")
        print("/sc?U re-solves from the source as it reads now.")
        return 1

    if not freeze(args.host, True):
        print("could not freeze automation; the engine would rewrite all three")
        return 1
    print(f"frozen. VDS_HB_SP {base[NEAR]}, VDS_HB_ST and VDS_DIS_HB_ST "
          f"{base['VDS_HB_ST']} restore on the way out.")
    print(f"  {describe(base[NEAR], base)}")
    print(HELP)

    offset = 0
    try:
        while True:
            live = read_fields(args.host, STATE)
            if live is None:
                print("  read failed")
                break
            # The solve can move under the tool: freezing stops the engine
            # re-solving by itself, not a pad press. A shift measured against a
            # baseline from before that is a width change, not a phase.
            if live["VDS_HSCALE"] != base["VDS_HSCALE"]:
                print(f"  the solve moved -- VDS_HSCALE {base['VDS_HSCALE']} "
                      f"-> {live['VDS_HSCALE']}. Re-baselining here, phase back to 0.")
                base, offset = live, 0
            problem = unsolved(live)
            if problem:
                print(f"  {problem}")
                print("  q, then /sc?U to re-solve, and start again.")

            try:
                reply = input(f"[phase {offset:+d}  HB_SP {live[NEAR]}] ").strip()
            except EOFError:
                break

            if reply in ("q", "quit"):
                break
            if reply == "?":
                print(HELP)
                continue

            if reply in ("c", "x"):
                verdict = "clean" if reply == "c" else "corrupt"
                expected = predict(live)
                session.mark(offset, verdict, live)
                print(f"  phase {offset:+d} marked {verdict}, width rule "
                      f"predicted {expected}")
                print(f"    {describe(live[NEAR], live)}")
                continue

            if reply == "s":
                print(f"  {describe(live[NEAR], live)}   predicts {predict(live)}")
                continue

            step = {"": 1, "+": 1, "-": -1, "]": 4, "[": -4}.get(reply)
            if step is None:
                print("  ? for the keys")
                continue
            if shift(args.host, live, step):
                offset += step
    finally:
        back = read_fields(args.host, STATE)
        if back is not None:
            shift(args.host, back, -offset)
        freeze(args.host, False)
        print("\nrestored, automation running again\n")
        print(session.table())
        if session.marks:
            print(f"\n{session.score()}")
            if session.path:
                print(session.path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
