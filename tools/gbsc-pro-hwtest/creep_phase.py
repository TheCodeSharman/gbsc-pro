#!/usr/bin/env python3
"""Jog FrameSync's phase target by hand and watch where the tear sits.

Frame time lock steers the display clock until the output frame rate equals the
input's. The rates then match but the two vsyncs still sit at a fixed offset,
and where the read pointer crosses the write pointer shows as a STATIONARY tear.
A tear that drifts means the rates are not locked and this tool is the wrong
one; a tear that stays put is what this moves.

    python3 tools/gbsc-pro-hwtest/creep_phase.py --host 192.168.88.108

The target is in degrees of one input frame, so 360 spans the frame top to
bottom and the crossover lands near target/360 down the screen -- near, because
the loop settles a little short of its target while the output-rate calibration
is stale. The distance between where it lands and target/360 is the calibration
error, and this tool is how it gets measured: mark the value that puts the tear
in vertical blanking and the difference from the blanking's own position is it.

Two things it deliberately does NOT do. It does not jump, because a transition
between two frames nobody saw is not a measurement; =N exists for getting back
somewhere, not for finding anything. And it does not bisect: park the tear in
blanking and keep going to find where it comes out the other side, or the range
recorded is one edge and a guess.
"""
import argparse
import datetime
import json
import os
import sys

from gbs_unit import get, get_json

HELP = """
  Enter / +   +1 degree     -        -1        fine
  ]           +10           [        -10       coarse
  }           +30           {        -30       very coarse
  =N          jump to N (a jump is not a creep -- say so in the note)
  m           mark this value, with a note
  s           show the state again
  ?           this list
  q           quit, restore the original target, print the marks
"""


def state(host):
    """Everything in one pass: a target from one read paired with a clock from
    another invents a discrepancy."""
    status, payload = get_json(host, "/framesync")
    if status != 200 or not isinstance(payload, dict):
        return None
    return payload


def show(s):
    if s is None:
        print("  (could not read /framesync this pass)")
        return
    phase = s.get("targetPhase", 0)
    print(f"  target {phase:>4}deg  = {phase / 360.0 * 100:5.1f}% down the frame"
          f"   ready {str(s.get('ready')).lower():5}"
          f"  clock {s.get('nowHz', 0) / 1e6:.4f} MHz"
          f"  source {s.get('fieldRateHz', 0):.3f} Hz")


def set_phase(host, degrees):
    get(host, f"/framesync?phase={degrees}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True)
    ap.add_argument("--out", default=None,
                    help="session file; defaults to "
                         "sessions/creep-phase-<timestamp>.json beside this tool")
    args = ap.parse_args()

    first = state(args.host)
    if first is None:
        sys.exit("no /framesync on that host -- is it running a build with the route?")
    if not first.get("ready"):
        print("WARNING: FrameSync reports ready=false, so nothing is steering and")
        print("         the tear you see is not the one this moves.")
    if not first.get("driving"):
        print("WARNING: driving=false -- no clock generator, so there is no lock.")

    original = int(first.get("targetPhase", 90))
    phase = original
    marks = []
    stamp = datetime.datetime.now().strftime("%Y-%m-%dT%H%M%S")
    path = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "sessions", f"creep-phase-{stamp}.json")

    def save():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump({"host": args.host, "started": stamp,
                       "original": original, "marks": marks}, f, indent=2)

    print(__doc__.split("\n")[0])
    print(HELP)
    show(first)

    try:
        while True:
            try:
                line = input(f"[{phase:>4}deg] ").strip()
            except EOFError:
                break

            if line in ("q", "quit"):
                break
            if line in ("?", "h", "help"):
                print(HELP); continue
            if line == "s":
                show(state(args.host)); continue
            if line == "m":
                note = input("  note: ").strip()
                now = state(args.host) or {}
                marks.append({"phase": phase, "note": note,
                              "nowHz": now.get("nowHz"),
                              "fieldRateHz": now.get("fieldRateHz"),
                              "percentDown": round(phase / 360.0 * 100, 1)})
                save()
                print(f"  marked {phase}deg ({phase / 360.0 * 100:.1f}% down)")
                continue

            step = {"": 1, "+": 1, "-": -1, "]": 10, "[": -10, "}": 30, "{": -30}
            if line in step:
                phase = (phase + step[line]) % 360
            elif line.startswith("="):
                try:
                    phase = int(line[1:]) % 360
                except ValueError:
                    print("  =N wants a number"); continue
            else:
                print("  ? for the keys"); continue

            set_phase(args.host, phase)
            show(state(args.host))
    finally:
        set_phase(args.host, original)
        save()
        print(f"\nrestored target to {original}deg")
        if marks:
            print(f"\n{'deg':>5} {'% down':>7}  note")
            for m in marks:
                print(f"{m['phase']:>5} {m['percentDown']:>7.1f}  {m['note']}")
        print(f"\nsession: {path}")


if __name__ == "__main__":
    main()
