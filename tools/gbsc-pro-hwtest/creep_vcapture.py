#!/usr/bin/env python3
"""Jog the vertical CAPTURE window by hand and watch what the picture does.

For settling what IF_VB counts. RD-5725-1.1 documents IF_VB_ST and IF_VB_SP as
"vertical blanking start/stop position" and gives no unit, so the wrap point is
the only thing that can say: the counter rolls at 2 x (VTOTAL + 1) if it runs at
the doubled line rate the line doubler needs, and at VTOTAL + 1 if it runs at the
source's. Creeping IF_VB_ST DOWN separates them without needing a clean picture
to start from -- above the wrap the value is never reached, so the picture does
not move at all until the creep crosses it.

    python3 tools/gbsc-pro-hwtest/creep_vcapture.py --host 192.168.88.108

Automation is frozen while it runs, or the engine re-solves the window
underneath you. Everything written is restored on the way out.

It does not jump and it does not bisect, for the reasons creep_window.py gives.
"""
import argparse
import datetime
import json
import os
import sys
import time

from gbs_unit import field_from, get, get_json, read_segment

IF_VB_ST = (1, 0x1C, 0, 11)
IF_VB_SP = (1, 0x1E, 0, 11)

# docs/scaler-geometry-model.md: the vertical write start is
# VDS_VB_SP + 0.2 + 0.8 x magnification.
START_CONST = 0.2
START_PER_MAG = 0.8

SESSION_FIELDS = {
    "IF_VB_ST": IF_VB_ST,
    "IF_VB_SP": IF_VB_SP,
    "IF_HSYNC_RST": (1, 0x0E, 0, 11),
    "IF_PRGRSV_CNTRL": (1, 0x00, 6, 1),
    "IF_LD_RAM_BYPS": (1, 0x0C, 0, 1),
    "IF_LD_SEL_PROV": (1, 0x0B, 7, 1),
    "PLLAD_MD": (5, 0x12, 0, 12),
    "VDS_VSCALE": (3, 0x17, 4, 10),
    "VDS_VSCALE_BYPS": (3, 0x00, 5, 1),
    "VDS_VSYNC_RST": (3, 0x02, 4, 11),
    "VDS_VB_ST": (3, 0x07, 0, 11),
    "VDS_VB_SP": (3, 0x08, 4, 11),
    "VDS_DIS_VB_ST": (3, 0x13, 0, 11),
    "VDS_DIS_VB_SP": (3, 0x14, 4, 11),
    "PB_FETCH_NUM": (4, 0x39, 0, 10),
    "PB_CAP_OFFSET": (4, 0x37, 0, 10),
    "STATUS_SYNC_PROC_VTOTAL": (0, 0x1B, 0, 11),
    "STATUS_SYNC_PROC_HTOTAL": (0, 0x17, 0, 12),
}

HELP = """
  Enter / +   +1        -        -1          fine
  ]           +10       [        -10         coarse
  }           +50       {        -50         very coarse
  m           mark this value, with a note
  s           show the arithmetic again
  w           which register moves: IF_VB_ST / IF_VB_SP
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


def read_state(host):
    """Every field a mark needs, one SEGMENT per request rather than one per
    field: a capture read from one solve and a window read from another describe
    no state the unit was ever in."""
    banks = {}
    for segment in sorted({seg for seg, _, _, _ in SESSION_FIELDS.values()}):
        banks[segment] = read_segment(host, segment, 0x00, 0x5F)

    state = {}
    for name, (segment, register, offset, width) in SESSION_FIELDS.items():
        bank = banks.get(segment)
        state[name] = (None if bank is None
                       else field_from(bank, register, offset, width))
    return state


def derive(state):
    scale = state.get("VDS_VSCALE")
    start, stop = state.get("IF_VB_SP"), state.get("IF_VB_ST")
    lines = state.get("STATUS_SYNC_PROC_VTOTAL")
    capture = None if None in (start, stop) else stop - start
    mag = None if not scale else 1024.0 / scale
    vb_sp = state.get("VDS_VB_SP")
    origin = (None if None in (vb_sp, mag)
              else vb_sp + START_CONST + START_PER_MAG * mag)
    produced = None if None in (capture, mag) else capture * mag
    return {"capture": capture, "magnification": mag, "produced": produced,
            "origin": origin,
            "picture_ends": None if None in (origin, produced) else origin + produced,
            "wrap_if_doubled": None if lines is None else 2 * (lines + 1),
            "wrap_if_line_rate": None if lines is None else lines + 1}


class Session:
    def __init__(self, host, path=None):
        self.host = host
        self.path = path
        self.started = datetime.datetime.now().isoformat(timespec="seconds")
        self.marks = []

    def mark(self, field, value, note, state):
        self.marks.append({"field": field, "value": value, "note": note,
                           "registers": state, "derived": derive(state)})
        self.write()

    def write(self):
        if not self.path:
            return
        directory = os.path.dirname(os.path.abspath(self.path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(self.path, "w") as f:
            json.dump({"tool": "creep_vcapture", "host": self.host,
                       "started": self.started, "marks": self.marks}, f, indent=2)


def show(state):
    d = derive(state)
    if d["capture"] is None:
        print("  (could not read the registers this pass)")
        return
    print(f"  source VTOTAL {state['STATUS_SYNC_PROC_VTOTAL']}"
          f"   wraps at {d['wrap_if_doubled']} if doubled,"
          f" {d['wrap_if_line_rate']} if at line rate")
    print(f"  capture {state['IF_VB_SP']}..{state['IF_VB_ST']}"
          f" = {d['capture']} units   VSCALE {state['VDS_VSCALE']}"
          f" (x{d['magnification']:.4f})   produces {d['produced']:.1f} lines")
    print(f"  output frame {state['VDS_VSYNC_RST'] + 1}"
          f"   memory {state['VDS_VB_SP']}..{state['VDS_VB_ST']}"
          f"   display {state['VDS_DIS_VB_SP']}..{state['VDS_DIS_VB_ST']}")
    print(f"  scan mode: PRGRSV {state['IF_PRGRSV_CNTRL']}"
          f"  LD_RAM_BYPS {state['IF_LD_RAM_BYPS']}"
          f"  LD_SEL_PROV {state['IF_LD_SEL_PROV']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    out = args.out
    if out is None:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "sessions", f"creep-vcapture-{stamp}.json")
    session = Session(args.host, out)

    if get_json(args.host, "/freeze")[1] is None:
        sys.exit("this firmware has no /freeze, so the engine would overwrite "
                 "everything written here")

    before = {"IF_VB_ST": read_field(args.host, IF_VB_ST),
              "IF_VB_SP": read_field(args.host, IF_VB_SP)}
    if None in before.values():
        sys.exit(f"could not read the capture window to restore: {before}")

    fields = {"IF_VB_ST": IF_VB_ST, "IF_VB_SP": IF_VB_SP}
    driving = "IF_VB_ST"
    value = dict(before)
    marks = []

    show(read_state(args.host))
    print(HELP)

    try:
        get(args.host, "/freeze?on=1")
        time.sleep(0.5)
        if get_json(args.host, "/freeze")[1].get("frozen") is not True:
            sys.exit("could not arm the freeze")

        while True:
            reply = input(f"[{driving} {value[driving]}] ").strip()

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
            elif reply == "m" or reply.startswith("m "):
                note = reply[2:].strip() if reply.startswith("m ") else ""
                if not note:
                    note = input("  note: ").strip()
                state = read_state(args.host)
                session.mark(driving, value[driving], note, state)
                marks.append((driving, value[driving], note))
                print(f"  marked {driving} {value[driving]}")
                show(state)
                print(f"  -> {out}")
                continue
            elif reply == "s":
                show(read_state(args.host))
                continue
            elif reply == "w":
                driving = "IF_VB_SP" if driving == "IF_VB_ST" else "IF_VB_ST"
                continue
            elif reply == "?":
                print(HELP)
                continue
            elif reply in ("q", "quit"):
                break
            else:
                print("  ? for the key list")
                continue

            value[driving] = max(0, min(2047, value[driving] + delta))
            write_field(args.host, fields[driving], value[driving])
    except (KeyboardInterrupt, EOFError):
        print()
    finally:
        write_field(args.host, IF_VB_ST, before["IF_VB_ST"])
        write_field(args.host, IF_VB_SP, before["IF_VB_SP"])
        get(args.host, "/freeze?on=0")
        time.sleep(0.5)
        print(f"restored {before}, unfrozen")
        session.write()
        if marks:
            print("\nmarks:")
            for field, at, note in marks:
                print(f"  {field} {at:5d}  {note}")
            print(f"\nsession: {out}")


if __name__ == "__main__":
    main()
