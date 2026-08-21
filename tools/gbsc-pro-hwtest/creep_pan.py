#!/usr/bin/env python3
"""Jog the horizontal pan by hand and find where the garbage scrolls on.

Panning right eventually brings a band of stale buffer onto the right of the
picture. The capture writes what it is asked for; it is the READ side that runs
past the end of it, so what is on screen there is whatever the buffer held --
static, and it does not follow a source whose border is cycling colour. The pan
needs a bound that stops before that band arrives, and this finds where it is.

    python3 tools/gbsc-pro-hwtest/creep_pan.py --host 192.168.88.108

Every mark records the pan position together with the capture window, both
scales, both windows and the playback pair, read in one pass -- so the bound can
be written as a function of whichever of them it turns out to track.

Automation is NOT frozen: the pan goes through the engine, which re-solves the
capture window, both scales and both windows from it. Freezing would measure a
framing the firmware would never produce.

One press is one unit of pan, which is finer than the OSD's step -- the pads
move eight output pixels, about four capture units at the bench magnification,
enough to step over the boundary without seeing it.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import field_from, get, get_json, read_segment
import creep_window

# What lastCapture() clamps against, mirrored from InputLine.h so a mark says
# which of the two bounds it was against. docs/capture-limits.md
WRITE_LIMIT_UNITS = 1125


def clamp_state(state):
    """Where the capture window sits, and what the pan has left before the
    stop meets lastCapture()."""
    hsync_rst = state.get("IF_HSYNC_RST")
    stop = state.get("IF_HB_ST2")
    if hsync_rst is None or stop is None:
        return {"line_units": None, "last_capture": None,
                "stop": stop, "clearance": None}

    line_units = hsync_rst + 1
    wrap = line_units - 2 if line_units >= 2 else 0
    last_capture = min(wrap, WRITE_LIMIT_UNITS)
    return {"line_units": line_units, "last_capture": last_capture,
            "stop": stop, "clearance": last_capture - stop}


def settled(read, was=None, tries=20, interval=0.15, pause=time.sleep):
    """The reading once the engine has acted on the press and stopped moving.

    A press against the clamp moves nothing, so an unchanged value is a real
    answer rather than a timeout -- it comes back equal to `was`.
    """
    last = None
    for _ in range(tries):
        current = read()
        if current is not None and current != was and current == last:
            return current
        last = current
        pause(interval)
    return last


class PanSession(creep_window.Session):
    def record(self):
        session = super().record()
        session["tool"] = "creep_pan"
        return session


HELP = """
  Enter / -   pan LEFT 1     +      pan RIGHT 1
  [           pan LEFT 10    ]      pan RIGHT 10
  {           pan LEFT 50    }      pan RIGHT 50
  m [note]    mark this framing
  s           show the framing again
  ?           this list
  q           quit and print the marks
"""


def read_state_of(host):
    return creep_window.read_state(lambda segment: read_segment(host, segment))


def stop_of(host):
    """Just IF_HB_ST2, one segment: settled() reads this in a loop."""
    segment, register, offset, width = creep_window.SESSION_FIELDS["IF_HB_ST2"]
    bank = read_segment(host, segment)
    if bank is None:
        return None
    return field_from(bank, register, offset, width)


def set_pan(host, wanted):
    get(host, f"/geometry?ph={wanted}")


def show(state):
    where = clamp_state(state)
    derived = creep_window.derive(state)
    if where["stop"] is None or derived["capture"] is None:
        print("  (could not read the framing this pass)")
        return
    print(f"  capture {state['IF_HB_SP2']}..{where['stop']} "
          f"= {derived['capture']} units   clamp {where['last_capture']} "
          f"(clearance {where['clearance']})   "
          f"HSCALE {state['VDS_HSCALE']} x{derived['magnification']:.3f}   "
          f"picture ends {derived['picture_ends']:.0f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    out = args.out
    if out is None:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "sessions", f"creep-pan-{stamp}.json")
    session = PanSession(args.host, out)

    state = read_state_of(args.host)
    if state.get("IF_HB_ST2") is None:
        print("could not read the framing -- is the unit answering /getreg?")
        return 1

    status, framing = get_json(args.host, "/geometry")
    if status != 200 or not isinstance(framing, dict):
        print("could not read /geometry")
        return 1
    pan = int(framing["ph"])

    print(__doc__.split("\n\n")[0])
    print(HELP)
    show(state)

    steps = {"": -1, "-": -1, "+": 1, "[": -10, "]": 10, "{": -50, "}": 50}
    while True:
        try:
            reply = input(f"ph {pan} > ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if reply == "q":
            break
        if reply == "?":
            print(HELP)
            continue

        note = creep_window.mark_note(reply)
        if note is not False:
            if note is None:
                note = input("  note: ").strip()
            state = read_state_of(args.host)
            where = clamp_state(state)
            session.mark(where["stop"], where["clearance"], note, state)
            print(f"  marked stop {where['stop']}, "
                  f"clearance {where['clearance']}: {note}")
            continue

        if reply == "s":
            show(read_state_of(args.host))
            continue

        if reply not in steps:
            print("  ? for the key list")
            continue

        was = stop_of(args.host)
        pan += steps[reply]
        set_pan(args.host, pan)
        stop = settled(lambda: stop_of(args.host), was=was)
        if stop == was:
            print(f"  stop unchanged at {stop} -- against the clamp, "
                  f"or inside the pan's dead travel")
        show(read_state_of(args.host))

    print()
    if not session.marks:
        print("no marks taken")
        return 0
    print(f"{'stop':>6} {'clearance':>10}  note")
    for mark in session.marks:
        print(f"{mark['value']:>6} {mark['reserve']:>10}  {mark['note']}")
    print(f"\nsession written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
