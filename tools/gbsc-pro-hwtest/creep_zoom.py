#!/usr/bin/env python3
"""Step the horizontal zoom by hand and mark each solve clean or corrupt.

The zoom moves the memory window and the picture's produced width together, and
the shear follows the window's WIDTH: `VDS_HB_ST - VDS_HB_SP` EVEN shears and odd
is clean. Two earlier rules are refuted -- the parity of `VDS_HB_SP` alone, and
`VDS_HB_SP + floor(produced)` -- and this sweep is where a candidate meets marks
it was not fitted to.

**It PREDICTS before you look.** Each step prints what the rule expects, and the
table on exit scores the predictions against the marks. A rule that survives a
sweep it could have failed is worth something; a table of agreements collected
without a prediction is not -- and the width rule was found by searching
candidate predicates against marks already taken, so until it calls fresh ones it
is a fit rather than a finding. docs/known-issues.md

Marks below the near edge's clamp are scored separately and not against the
width rule: there the picture is broken on 49 of 51 marks whatever the width.

    python3 tools/gbsc-pro-hwtest/creep_zoom.py --host 192.168.88.108

It presses the ZOOM PAD rather than writing VDS_HSCALE, so the whole solve moves
together -- the capture window, both scales, the playback fetch and the memory
window are one interdependent answer, and setting a subset of it by hand
measures the mismatch rather than the thing. Automation stays running for the
same reason: the engine has to solve.

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
from gbs_unit import get                                # noqa: E402
from shear import (Session, clamped, describe, predict,  # noqa: E402
                   settled)

HELP = """
  Enter / +   zoom IN  1     -       zoom OUT 1
  ]           zoom IN  8     [       zoom OUT 8     (the pad's own step)
  c           mark this solve CLEAN
  x           mark this solve CORRUPT
  s           show the arithmetic again
  ?           this list
  q           quit and print the marks
"""


def press(host, key, pixels):
    get(host, f"/sc?{key}={pixels}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default="")
    parser.add_argument("--judging", default="unstated",
                        help="which artefact the verdicts are about, e.g. "
                             "'the wedge' or 'image stability'")
    args = parser.parse_args()

    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "sessions", "creep_zoom-"
                               + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                               + ".json")
    session = Session(args.host, args.out or default_out, tool="creep_zoom",
                      judging=args.judging)

    state = settled(args.host)
    if state is None:
        print("could not read the unit")
        return 1

    print("the zoom pad moves the whole solve; automation stays running.")
    print("/sc?B returns to the default framing when you are done.")
    print(HELP)
    print(f"  {describe(state['VDS_HB_SP'], state)}   predicts {predict(state)}")

    try:
        while True:
            try:
                reply = input(f"[HSCALE {state.get('VDS_HSCALE')} "
                              f"HB_SP {state.get('VDS_HB_SP')}] ").strip()
            except EOFError:
                break

            if reply in ("q", "quit"):
                break
            if reply == "?":
                print(HELP)
                continue

            if reply in ("c", "x"):
                verdict = "clean" if reply == "c" else "corrupt"
                expected = predict(state)
                session.mark(state.get("VDS_HSCALE"), verdict, state)
                called = ("the clamp governs here, not the width"
                          if clamped(state)
                          else "AGREES" if verdict == expected else "*** REFUTES ***")
                print(f"  marked {verdict}, width rule predicted {expected}  {called}")
                continue

            if reply == "s":
                print(f"  {describe(state['VDS_HB_SP'], state)}"
                      f"   predicts {predict(state)}")
                continue

            step = {"": ("I", 1), "+": ("I", 1), "-": ("O", 1),
                    "]": ("I", 8), "[": ("O", 8)}.get(reply)
            if step is None:
                print("  ? for the keys")
                continue

            press(args.host, step[0], step[1])
            fresh = settled(args.host)
            if fresh is None:
                print("  read failed after the press")
                continue
            if fresh == state:
                print("  the solve did not move -- the scale may be at its clamp")
            state = fresh
            print(f"  {describe(state['VDS_HB_SP'], state)}"
                  f"   predicts {predict(state)}")
    finally:
        print()
        print(session.table())
        if session.marks:
            print(f"\n{session.score()}")
            if session.path:
                print(session.path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
