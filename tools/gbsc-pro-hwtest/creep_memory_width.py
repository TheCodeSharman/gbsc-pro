#!/usr/bin/env python3
"""Change the memory window's WIDTH at one solve, and mark each step.

The shear follows `VDS_HB_ST - VDS_HB_SP`: an EVEN width shears and an odd one is
clean. That rule was found by searching predicates against marks already taken,
and it has never called a mark before it was taken. This is where it can fail.

**Two motions, and they separate the width from the register.** The refuted rule
-- the parity of `VDS_HB_SP` alone -- predicts the OPPOSITE of the width rule on
both of them, so one run decides between them.

    key      near edge   far edge   width    VDS_HB_SP   width rule   HB_SP rule
    w / n    -+1         +-1        +-2      moves       no change    flips
    > / <    fixed       +-1        +-1      fixed       flips        no change

`w` and `n` move both edges about the centre, so the width changes by two and its
parity does not: every step must carry the first verdict, unbroken. `>` and `<`
move the far edge alone, leaving `VDS_HB_SP` exactly where the solve put it: every
step must alternate. A run that shows the first and not the second says the width
is standing in for something else.

    python3 tools/gbsc-pro-hwtest/creep_memory_width.py --host 192.168.88.108

Start from a clean solve and narrow rather than widen. **Widening opens the window
past the end of the picture**, and what the playback stage fetches beyond it is
absence of data rather than corruption -- a band at the right of the line, not the
whole-picture diagonal this is marking. Narrowing keeps the window inside the
picture and only costs a column off the right.

Automation is frozen while it runs, because the engine rewrites all three
registers on every solve. Everything written is restored on the way out.

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
from shear import (STATE, WINDOW_STOP_MIN, Session, describe,  # noqa: E402
                   freeze, predict, unsolved, write_field)

NEAR = "VDS_HB_SP"
# The memory window's far edge and the display window's are one quantity in two
# registers. Letting them come apart puts the blanking and the fetch on different
# edges, which is a third variable.
FAR = ("VDS_HB_ST", "VDS_DIS_HB_ST")

HELP = """
  n / Enter   narrow by 2, parity HELD    w       widen by 2, parity HELD
  <           far edge -1, parity FLIPS   >       far edge +1, parity FLIPS
  c           mark this width CLEAN
  x           mark this width CORRUPT
  s           show the arithmetic again
  ?           this list
  q           quit, restore, and print the marks
"""

# near, far
MOTION = {"n": (+1, -1), "": (+1, -1), "w": (-1, +1),
          "<": (0, -1), ">": (0, +1)}


def move(host, live, near_step, far_step):
    """Move the edges from where they are NOW, grown before shrunk.

    Relative rather than from a baseline read at startup: the framing can move
    under the tool, because freezing stops the engine re-solving by itself and not
    a pad press.
    """
    near = live[NEAR] + near_step
    far = live["VDS_HB_ST"] + far_step
    if near < WINDOW_STOP_MIN:
        print(f"  {WINDOW_STOP_MIN} is the near edge's floor, and the clamp "
              f"governs the picture below it")
        return False
    if far >= live["VDS_HSYNC_RST"]:
        print(f"  the far edge cannot reach the raster at {live['VDS_HSYNC_RST']}")
        return False
    if far <= near:
        print("  the window would close")
        return False
    order = (("VDS_HB_ST", "VDS_DIS_HB_ST", NEAR) if far_step > 0
             else (NEAR, "VDS_HB_ST", "VDS_DIS_HB_ST"))
    for name in order:
        if not write_field(host, name, near if name == NEAR else far):
            return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "sessions", "creep_memory_width-"
                               + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                               + ".json")
    session = Session(args.host, args.out or default_out, tool="creep_memory_width")

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
    print(f"  {describe(base[NEAR], base)}   predicts {predict(base)}")
    print(HELP)

    near_offset = far_offset = 0
    try:
        while True:
            live = read_fields(args.host, STATE)
            if live is None:
                print("  read failed")
                break
            if live["VDS_HSCALE"] != base["VDS_HSCALE"]:
                print(f"  the solve moved -- VDS_HSCALE {base['VDS_HSCALE']} -> "
                      f"{live['VDS_HSCALE']}. Re-baselining here, offsets back to 0.")
                base, near_offset, far_offset = live, 0, 0

            width = live["VDS_HB_ST"] - live[NEAR]
            try:
                reply = input(f"[width {width} "
                              f"{'even' if width % 2 == 0 else 'odd'}  "
                              f"HB_SP {live[NEAR]}] ").strip()
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
                session.mark(width, verdict, live)
                agree = "AGREES" if verdict == expected else "*** REFUTES ***"
                print(f"  width {width} marked {verdict}, rule predicted "
                      f"{expected}  {agree}")
                print(f"    {describe(live[NEAR], live)}")
                continue

            if reply == "s":
                print(f"  {describe(live[NEAR], live)}   predicts {predict(live)}")
                continue

            motion = MOTION.get(reply)
            if motion is None:
                print("  ? for the keys")
                continue
            if move(args.host, live, *motion):
                near_offset += motion[0]
                far_offset += motion[1]
    finally:
        back = read_fields(args.host, STATE)
        if back is not None:
            move(args.host, back, -near_offset, -far_offset)
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
