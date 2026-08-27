#!/usr/bin/env python3
"""Jog the output window's right-hand edge by hand, coarse and fine, and watch.

For finding where the picture's right edge meets the screen, and whether the far
end of the line needs any blanking reserve at all. The near end has a measured
floor -- VDS_HB_SP below 8 corrupts -- and Axis::blankingEachEnd mirrors it onto
the far end as windowStopMin 8 + startConst 55 = 63, which nothing has measured:
maxDisplayWindow takes the 63 before the scale is chosen, so the picture
saturates ~100 px short of the line end and the question never comes up.

Whatever floor this finds is a property of the MS9288A, not of the TV. The
encoder consumes the analog blanking and generates HDMI blanking of its own, so
the minimum porch the scaler must emit is a board characteristic. Where a given
set stops painting is a different quantity and is not portable -- do not size the
reserve from it.

    python3 tools/gbsc-pro-hwtest/creep_window.py --host 192.168.88.108

Every mark records the capture window, both scales, both rasters, the divider
and the playback pair, read in ONE pass at the moment it is taken, and the
session is written to sessions/ as it goes. A mark that is only a value and a
note cannot be read again: the framing has to come from the tool, because a dump
taken afterwards is the framing at dump time.

Automation is frozen while it runs, or the engine re-solves the windows
underneath you. Everything written is restored on the way out.

Two things it deliberately does NOT do. It does not jump: a transition that
happens between two frames the eye never saw is not a measurement. And it does
not bisect: corruption on this board comes in bands, so a boundary found by
halving is only the real one if nothing clean lies beyond it.
"""
import argparse
import datetime
import json
import os
import sys
import time

from gbs_unit import field_from, field_spec, get, get_json, read_segment

VDS_HSCALE = field_spec("VDS_HSCALE")
VDS_HSYNC_RST = field_spec("VDS_HSYNC_RST")
VDS_HB_ST = field_spec("VDS_HB_ST")
VDS_HB_SP = field_spec("VDS_HB_SP")
VDS_DIS_HB_ST = field_spec("VDS_DIS_HB_ST")
IF_HB_SP2 = field_spec("IF_HB_SP2")
IF_HB_ST2 = field_spec("IF_HB_ST2")

START_CONST = 55.0
START_PER_MAG = 25.0

# Everything the arithmetic behind a mark uses. A mark is a value and a note;
# without these it cannot be read again, and the framing it was taken at cannot
# be recovered afterwards -- a dump taken later is the framing at dump time.
SESSION_FIELDS = {name: field_spec(name) for name in (
    "PLLAD_MD", "IF_HSYNC_RST", "IF_HB_SP2", "IF_HB_ST2", "IF_VB_SP",
    "IF_VB_ST", "VDS_HSCALE", "VDS_VSCALE", "VDS_HSCALE_BYPS",
    "VDS_VSCALE_BYPS", "VDS_HSYNC_RST", "VDS_VSYNC_RST", "VDS_HB_SP",
    "VDS_HB_ST", "VDS_VB_SP", "VDS_VB_ST", "VDS_DIS_HB_SP",
    "VDS_DIS_HB_ST", "VDS_DIS_VB_SP", "VDS_DIS_VB_ST", "PB_FETCH_NUM",
    "PB_CAP_OFFSET", "STATUS_SYNC_PROC_HTOTAL",
    "STATUS_SYNC_PROC_HLOW_LEN", "STATUS_SYNC_PROC_VTOTAL"
)}


def mark_note(reply):
    """The note out of a mark keypress: the text after `m`, None for a bare `m`
    so the caller prompts, False when the reply is not a mark at all."""
    if reply == "m":
        return None
    if reply.startswith("m "):
        return reply[2:].strip()
    return False


def read_state(read_bank):
    """Every field above, one SEGMENT per request rather than one per field.

    Simultaneity is the point: a capture read from one solve and a window read
    from another describe no state the unit was ever in.
    """
    banks = {}
    for segment in sorted({seg for seg, _, _, _ in SESSION_FIELDS.values()}):
        banks[segment] = read_bank(segment)

    state = {}
    for name, (segment, register, offset, width) in SESSION_FIELDS.items():
        bank = banks.get(segment)
        state[name] = (None if bank is None
                       else field_from(bank, register, offset, width))
    return state


def derive(state):
    """What the registers mean, from one pass and no other source."""
    scale = state.get("VDS_HSCALE")
    start, stop = state.get("IF_HB_SP2"), state.get("IF_HB_ST2")
    capture = None if None in (start, stop) else stop - start
    mag = None if not scale else 1024.0 / scale
    hb_sp = state.get("VDS_HB_SP")

    origin = (None if None in (hb_sp, mag)
              else hb_sp + START_CONST + START_PER_MAG * mag)
    produced = None if None in (capture, mag) else capture * mag
    return {"capture": capture, "magnification": mag, "produced": produced,
            "origin": origin,
            "picture_ends": None if None in (origin, produced) else origin + produced}


class Session:
    """The marks, each with the framing it was taken at, written as they happen.

    Written after every mark rather than at the end: a session lost to a crash
    or a pulled cable is a session repeated on the bench.
    """

    def __init__(self, host, path=None):
        self.host = host
        self.path = path
        self.started = datetime.datetime.now().isoformat(timespec="seconds")
        self.marks = []

    def mark(self, value, reserve, note, state):
        self.marks.append({"value": value, "reserve": reserve, "note": note,
                           "registers": state, "derived": derive(state)})
        self.write()

    def record(self):
        return {"tool": "creep_window", "host": self.host,
                "started": self.started, "marks": self.marks}

    def write(self):
        if not self.path:
            return
        directory = os.path.dirname(os.path.abspath(self.path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(self.path, "w") as f:
            json.dump(self.record(), f, indent=2)

HELP = """
  Enter / +   +1        -        -1          fine
  ]           +10       [        -10         coarse
  }           +50       {        -50         very coarse
  =N          jump to N (say so in your notes -- a jump is not a creep)
  m           mark this value, with a note
  s           show the arithmetic again
  w           which register moves: both / display only / memory only
  ?           this list
  q           quit, restore, and print the marks
"""


def raw(host, seg, reg):
    status, payload = get_json(host, f"/getreg?s={seg}&r=0x{reg:02X}")
    if status != 200 or not isinstance(payload, dict):
        return None
    return int(str(payload["value"]), 16)


def read_field(host, spec):
    seg, reg, off, width = spec
    value = 0
    for i in range((off + width + 7) // 8):
        byte = raw(host, seg, reg + i)
        if byte is None:
            return None
        value |= byte << (8 * i)
    return (value >> off) & ((1 << width) - 1)


def write_field(host, spec, wanted):
    seg, reg, off, width = spec
    span = (off + width + 7) // 8
    value = 0
    for i in range(span):
        byte = raw(host, seg, reg + i)
        if byte is None:
            return False
        value |= byte << (8 * i)
    mask = ((1 << width) - 1) << off
    value = (value & ~mask) | ((wanted << off) & mask)
    for i in range(span):
        get(host, f"/setreg?s={seg}&r=0x{reg + i:02X}"
                  f"&v=0x{(value >> (8 * i)) & 0xFF:02X}")
    return True


def arithmetic(host, total):
    """Where the picture ends, from the registers as they are right now.

    Read in one pass. Pairing a capture from one solve with a window from another
    is how a 46 px discrepancy got reported that was not there.
    """
    scale = read_field(host, VDS_HSCALE)
    hb_sp = read_field(host, VDS_HB_SP)
    sp2 = read_field(host, IF_HB_SP2)
    st2 = read_field(host, IF_HB_ST2)
    if None in (scale, hb_sp, sp2, st2) or not scale:
        return None
    mag = 1024.0 / scale
    capture = st2 - sp2
    start = hb_sp + START_CONST + START_PER_MAG * mag
    return {"capture": capture, "mag": mag, "scale": scale,
            "start": start, "end": start + capture * mag, "total": total}


def show(state):
    if state is None:
        print("  (could not read the registers this pass)")
        return
    print(f"  capture {state['capture']}  HSCALE {state['scale']} "
          f"(x{state['mag']:.4f})  picture {state['start']:.0f}"
          f"..{state['end']:.0f}  of {state['total']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default=None,
                        help="where to write the session; defaults to "
                             "sessions/creep-window-<timestamp>.json beside this tool")
    args = parser.parse_args()

    out = args.out
    if out is None:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "sessions", f"creep-window-{stamp}.json")
    session = Session(args.host, out)

    if get_json(args.host, "/freeze")[1] is None:
        sys.exit("this firmware has no /freeze, so the engine would overwrite "
                 "everything written here")

    total = read_field(args.host, VDS_HSYNC_RST)
    if total is None:
        sys.exit("could not read VDS_HSYNC_RST")
    total += 1

    before = {"VDS_HB_ST": read_field(args.host, VDS_HB_ST),
              "VDS_DIS_HB_ST": read_field(args.host, VDS_DIS_HB_ST)}
    if None in before.values():
        sys.exit(f"could not read the windows to restore: {before}")

    value = before["VDS_DIS_HB_ST"]
    driving = "both"
    marks = []

    print(f"line total {total}, far edge {total - 2}")
    show(arithmetic(args.host, total))
    print(f"starting at VDS_DIS_HB_ST {value}, VDS_HB_ST {before['VDS_HB_ST']}")
    print(HELP)

    try:
        get(args.host, "/freeze?on=1")
        time.sleep(0.5)
        if get_json(args.host, "/freeze")[1].get("frozen") is not True:
            sys.exit("could not arm the freeze")

        while True:
            reserve = total - value
            reply = input(f"[{value}  reserve {reserve}  {driving}] ").strip()

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
                    value = max(0, min(total - 2, int(reply[1:])))
                except ValueError:
                    print("  =N wants a number")
                    continue
            elif mark_note(reply) is not False:
                note = mark_note(reply)
                if note is None:
                    note = input("  note: ").strip()
                state = read_state(lambda seg: read_segment(args.host, seg, 0x00, 0x5F))
                session.mark(value, total - value, note, state)
                marks.append((value, total - value, note))
                framing = derive(state)
                print(f"  marked {value}, reserve {total - value}  "
                      f"capture {state['IF_HB_SP2']}..{state['IF_HB_ST2']}  "
                      f"HSCALE {state['VDS_HSCALE']} VSCALE {state['VDS_VSCALE']}  "
                      f"PLLAD_MD {state['PLLAD_MD']}")
                print(f"  -> {out}")
                continue
            elif reply == "s":
                show(arithmetic(args.host, total))
                continue
            elif reply == "w":
                driving = {"both": "display", "display": "memory",
                           "memory": "both"}[driving]
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

            if delta is not None:
                value = max(0, min(total - 2, value + delta))

            if driving in ("both", "memory"):
                write_field(args.host, VDS_HB_ST, value)
            if driving in ("both", "display"):
                write_field(args.host, VDS_DIS_HB_ST, value)
    except (KeyboardInterrupt, EOFError):
        print()
    finally:
        write_field(args.host, VDS_HB_ST, before["VDS_HB_ST"])
        write_field(args.host, VDS_DIS_HB_ST, before["VDS_DIS_HB_ST"])
        get(args.host, "/freeze?on=0")
        time.sleep(0.5)
        print(f"restored {before}, unfrozen")
        session.write()
        if marks:
            print("\nmarks:")
            for at, reserve, note in marks:
                print(f"  {at:5d}  reserve {reserve:4d}  {note}")
            print(f"\nsession, with the framing behind every mark: {out}")


if __name__ == "__main__":
    main()
