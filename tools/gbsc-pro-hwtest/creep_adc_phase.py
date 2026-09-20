#!/usr/bin/env python3
"""Jog the ADC sampling phase by hand and watch, for shimmer on fine detail.

The sample clock is `PLLAD_MD x line rate`, and on a 15 kHz source the divider
is pinned at the capture write limit rather than chosen for the source -- so it
is not harmonically related to the source's pixel clock and the sampling instant
within each source pixel is arbitrary. Phase is the only control over where in
the pixel the ADC lands.

    python3 tools/gbsc-pro-hwtest/creep_adc_phase.py --host 192.168.88.108

`/sc?b` steps `rto->phaseADC` and latches, so the FIRMWARE's held phase moves
with the jog and anything that re-applies it re-applies this value. Nothing here
needs freezing, and there is nothing to restore beyond walking the 32 steps back
round to where it started, which `q` offers.

Forward only, one step at a time: a transition that happens between two frames
nobody saw is not a measurement, and the wrap is what returns the unit.
"""
import argparse
import datetime
import json
import os
import sys

from gbs_unit import get, read_fields

STEPS = 32

# Read at every mark, in one request. A phase that looked clean against a
# divider that has since moved is not a reading anyone can use again.
MARK_FIELDS = ["PA_ADC_S", "PA_SP_S", "PLLAD_MD", "PLLAD_KS", "PLLAD_CKOS",
               "STATUS_SYNC_PROC_HTOTAL", "STATUS_SYNC_PROC_VTOTAL",
               "HPERIOD_IF", "STATUS_MISC_PLLAD_LOCK", "VDS_HSCALE",
               "VDS_VSCALE"]

HELP = """
  Enter       step one phase (of 32)
  ]           step four
  m           mark this phase, with a note
  s           show the sampling state again
  q           quit, and offer to walk back to the starting phase
  ?           this list
"""


def phase_of(host):
    return read_fields(host, ["PA_ADC_S"])["PA_ADC_S"]


def step(host):
    status, body = get(host, "/sc?b")
    if status != 200:
        print(f"  /sc?b answered {status}: {body}")
    return phase_of(host)


def show(state):
    print(f"  phase {state['PA_ADC_S']:2}   divider {state['PLLAD_MD']}"
          f" (KS {state['PLLAD_KS']} CKOS {state['PLLAD_CKOS']},"
          f" latched as {state['STATUS_SYNC_PROC_HTOTAL']})"
          f"   lock {state['STATUS_MISC_PLLAD_LOCK']}")
    print(f"  source {state['STATUS_SYNC_PROC_VTOTAL']} lines,"
          f" HPERIOD_IF {state['HPERIOD_IF']}"
          f"   scale h {state['VDS_HSCALE']} v {state['VDS_VSCALE']}")


def walk_to(host, wanted, current):
    while current != wanted:
        current = step(host)
        print(f"  phase {current}")
    return current


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--out", default=None, help="write the marks here as JSON")
    args = ap.parse_args()

    started = phase_of(args.host)
    current = started
    marks = []
    print(f"starting phase {started}; {STEPS} steps wraps back to it")
    show(read_fields(args.host, MARK_FIELDS))
    print(HELP)

    while True:
        try:
            reply = input(f"phase {current} > ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if reply == "q":
            break
        if reply == "?":
            print(HELP)
            continue
        if reply == "s":
            show(read_fields(args.host, MARK_FIELDS))
            continue
        if reply == "m" or reply.startswith("m "):
            note = reply[2:].strip() if reply.startswith("m ") else input("  note: ").strip()
            state = read_fields(args.host, MARK_FIELDS)
            marks.append({"phase": state["PA_ADC_S"], "note": note,
                          "registers": state})
            print(f"  marked phase {state['PA_ADC_S']}: {note}")
            continue
        if reply == "]":
            for _ in range(4):
                current = step(args.host)
            print(f"  phase {current}")
            continue
        if reply == "":
            current = step(args.host)
            print(f"  phase {current}")
            continue
        print(f"  unknown: {reply!r}")
        print(HELP)

    if marks:
        print("\n  phase  note")
        for mark in marks:
            print(f"  {mark['phase']:5}  {mark['note']}")

    if current != started:
        back = input(f"\nwalk back to phase {started}? [Y/n] ").strip().lower()
        if back in ("", "y", "yes"):
            walk_to(args.host, started, current)

    if args.out and marks:
        directory = os.path.dirname(os.path.abspath(args.out))
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(args.out, "w") as f:
            json.dump({"tool": "creep_phase", "host": args.host,
                       "started": datetime.datetime.now().isoformat(timespec="seconds"),
                       "startingPhase": started, "marks": marks}, f, indent=2)
        print(f"wrote {args.out}")


if __name__ == "__main__":
    sys.exit(main())
