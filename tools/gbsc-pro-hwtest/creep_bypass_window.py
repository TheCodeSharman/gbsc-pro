#!/usr/bin/env python3
"""Jog the pass-through blanking window by hand, and watch both its edges.

For finding where the source's porches end and its picture begins on the bypass
channel. HdBypass places both edges from the published raster the source
matched, and the channel delays the sample behind the sync it emits beside it,
so the window lands displaced from the video by that delay.

CHECK BOTH EDGES ARE ON THE PANEL BEFORE CREEPING EITHER. The sink places its
own window from HD_HS_ST and follows it one for one, so an edge can sit outside
what the display paints -- creeping it then moves nothing visible, and the
placement that results is a reading of the other edge alone. Open both edges
wide, confirm the source's border is visible at each, and move HD_HS_ST until it
is if not.

    python3 tools/gbsc-pro-hwtest/creep_bypass_window.py --host 192.168.88.108

SHIFT IS THE DEFAULT AND THE WIDTH IS THE ENGINE'S. Both edges move together, so
the picture stays the size the solve chose and only its alignment with the
source moves; `w` releases one edge at a time when the width itself is in
question. Nothing here is a mode's constant -- the width comes from whatever the
engine solved for the source in front of it, so the same jog works at any mode.

Put content against the edge before measuring one. Set the source's border on
(`printf 'BORDER ON\\n' | nc <riscpc> 6502`) so both ends of the window have
something visible either side of them; at a default framing the last thing on
screen is captured blanking and the boundary cannot be seen at all.

Every mark records both edges, the width, the divider and the sync processor's
own totals, read in ONE pass, and the session is written to sessions/ as it
goes.

Automation is frozen while it runs, or the engine re-solves the window
underneath you. Both edges are restored on the way out.

It does not jump, and it does not bisect. A transition between two frames nobody
saw is not a measurement, and an edge found by halving is only the real one if
nothing clean lies beyond it -- keep creeping past the first one.
"""
import argparse
import datetime
import json
import os
import sys
import time

import setfield

from gbs_unit import get, get_json, read_fields, write_reg

FIELDS = setfield.load_map()

EDGES = ["HD_HB_SP", "HD_HB_ST"]
STATE = EDGES + ["HD_HSYNC_RST", "HD_HS_ST", "HD_HS_SP", "HD_VB_ST", "HD_VB_SP",
                 "PLLAD_MD", "SP_RT_HS_SP", "STATUS_SYNC_PROC_HTOTAL",
                 "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HLOW_LEN",
                 "DAC_RGBS_BYPS2DAC"]


def write_named(host, name, value):
    """One field, read-modify-write, quietly: a jog writes on every keypress."""
    spec = FIELDS[name]
    writes = setfield.byte_writes(host, spec, value)
    if writes is None:
        return False
    for register, _old, new in writes:
        if write_reg(host, spec["seg"], register, new) is None:
            return False
    return True


HELP = """
  Enter / +   +1        -        -1          fine
  ]           +10       [        -10         coarse
  }           +50       {        -50         very coarse
  =N          put the near edge at N (say so in your notes -- not a creep)
  m           mark this placement, with a note
  s           show both edges again
  w           what moves: shift / near edge / far edge
  ?           this list
  q           quit, restore, and print the marks
"""


class Session:
    """The marks, each with the placement it was taken at, written as they happen."""

    def __init__(self, host, path=None):
        self.host = host
        self.path = path
        self.started = datetime.datetime.now().isoformat(timespec="seconds")
        self.marks = []

    def mark(self, near, far, note, state):
        self.marks.append({"near": near, "far": far, "width": far - near,
                           "note": note, "registers": state})
        self.write()

    def record(self):
        return {"tool": "creep_bypass_window", "host": self.host,
                "started": self.started, "marks": self.marks}

    def write(self):
        if not self.path:
            return
        directory = os.path.dirname(os.path.abspath(self.path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(self.path, "w") as f:
            json.dump(self.record(), f, indent=2)


def show(state):
    if state is None:
        print("  (could not read the registers this pass)")
        return
    near, far = state["HD_HB_SP"], state["HD_HB_ST"]
    divider = state["PLLAD_MD"]
    print(f"  blanked {far}..{near} of {divider}, showing {near}..{far} "
          f"({far - near} samples, {100.0 * (far - near) / divider:.1f}% of the line)")
    print(f"  sync processor: htotal {state['STATUS_SYNC_PROC_HTOTAL']} "
          f"vtotal {state['STATUS_SYNC_PROC_VTOTAL']} "
          f"hlow {state['STATUS_SYNC_PROC_HLOW_LEN']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default=None,
                        help="where to write the session; defaults to "
                             "sessions/creep-bypass-window-<timestamp>.json "
                             "beside this tool")
    args = parser.parse_args()

    out = args.out
    if out is None:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "sessions", f"creep-bypass-window-{stamp}.json")
    session = Session(args.host, out)

    if get_json(args.host, "/freeze")[1] is None:
        sys.exit("this firmware has no /freeze, so the engine would overwrite "
                 "everything written here")

    state = read_fields(args.host, STATE)
    if not state.get("DAC_RGBS_BYPS2DAC"):
        sys.exit("this is not the pass-through channel -- /uc?x switches route, "
                 "and the scaling path's window is creep_window.py's")

    before = {name: state[name] for name in EDGES}
    near, far = before["HD_HB_SP"], before["HD_HB_ST"]
    width = far - near
    driving = "shift"
    marks = []

    print(f"divider {state['PLLAD_MD']}, near edge {near}, far edge {far}, "
          f"width {width}")
    show(state)
    print(HELP)

    try:
        get(args.host, "/freeze?on=1")
        time.sleep(0.5)
        if get_json(args.host, "/freeze")[1].get("frozen") is not True:
            sys.exit("could not arm the freeze")

        while True:
            reply = input(f"[{near}..{far}  width {far - near}  {driving}] ").strip()

            delta = None
            if reply in ("", "+"):
                delta = 1
            elif reply == "-":
                delta = -1
            elif reply == "]":
                delta = 10
            elif reply == "[":
                delta = -10
            elif reply == "}":
                delta = 50
            elif reply == "{":
                delta = -50
            elif reply.startswith("="):
                try:
                    wanted = int(reply[1:])
                except ValueError:
                    print("  =N wants a number")
                    continue
                delta = wanted - near
            elif reply in ("m", "mark") or reply.startswith("m "):
                note = reply[2:].strip() if reply.startswith("m ") else ""
                if not note:
                    note = input("  note: ").strip()
                now = read_fields(args.host, STATE)
                session.mark(near, far, note, now)
                marks.append((near, far, far - near, note))
                print(f"  marked {near}..{far}, width {far - near}  "
                      f"divider {now['PLLAD_MD']}  "
                      f"htotal {now['STATUS_SYNC_PROC_HTOTAL']}")
                print(f"  -> {out}")
                continue
            elif reply == "s":
                show(read_fields(args.host, STATE))
                continue
            elif reply == "w":
                driving = {"shift": "near", "near": "far", "far": "shift"}[driving]
                print(f"  now moving: {driving}")
                continue
            elif reply == "?":
                print(HELP)
                continue
            elif reply in ("q", "quit"):
                break
            else:
                print("  ? for the key list")
                continue

            ceiling = state["PLLAD_MD"] - 1
            if driving in ("shift", "near"):
                near = max(0, min(ceiling, near + delta))
            if driving in ("shift", "far"):
                far = max(0, min(ceiling, far + delta))

            write_named(args.host, "HD_HB_SP", near)
            write_named(args.host, "HD_HB_ST", far)
    finally:
        write_named(args.host, "HD_HB_SP", before["HD_HB_SP"])
        write_named(args.host, "HD_HB_ST", before["HD_HB_ST"])
        get(args.host, "/freeze?on=0")
        print(f"\nrestored {before}")
        if marks:
            print("\nmarks:")
            print(f"  {'near':>6} {'far':>6} {'width':>6}  note")
            for near, far, wide, note in marks:
                print(f"  {near:>6} {far:>6} {wide:>6}  {note}")
            print(f"\n{out}")


if __name__ == "__main__":
    main()
