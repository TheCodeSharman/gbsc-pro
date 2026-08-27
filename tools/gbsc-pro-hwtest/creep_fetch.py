#!/usr/bin/env python3
"""Jog PB_FETCH_NUM, and slide the capture window, against the write limit.

Past IF 1125 -- 2250 ADC samples from the line start -- the capture path stops
writing video and writes Y=U=V=0, which decodes to green and destroys any active
picture that reaches it. Call that position X. This is the tool that measured it
and the tool for re-confirming it at a divider it has not been measured at: the
prediction is a band exactly `ifLine - 1125` units wide, so PLLAD_MD 2400 should
give a thin 75-unit one.

    python3 tools/gbsc-pro-hwtest/creep_fetch.py --host 192.168.88.108

`x` switches what the keys move. The capture window slides at FIXED WIDTH --
start and stop together, in granules of 2, the low bit of IF_HB_SP2 being dead --
so the picture size and the scaler's output stay put and only the capture
position moves. That is the vernier: creep the window until the green just
appears, and the stop at that threshold IS X, read off the register rather than
estimated off the screen.

X does not move with the fetch, and the fetch keys are here because that is what
established it. What the fetch does move is whether playback reads past what was
written; PB_CAP_OFFSET is the stride it must not fall below.

Every mark records the fetch, offset, capture window and HSCALE read in the SAME
pass. A value paired with a fetch read at another moment is not a measurement.

Automation is frozen while it runs, or the engine re-solves from the capture and
overwrites both. Everything written is restored on the way out.

Two things it deliberately does NOT do. It does not jump: a transition between
two frames nobody saw is not a measurement. And it does not bisect: corruption
on this board comes in bands, so a boundary found by halving is only the real
one if nothing clean lies beyond it -- creep past the first edge.

docs/capture-limits.md, docs/investigations/tail-green.md
"""
import argparse
import sys
import time

from gbs_unit import field_spec, get, get_json

PB_FETCH_NUM = field_spec("PB_FETCH_NUM")
PB_CAP_OFFSET = field_spec("PB_CAP_OFFSET")
VDS_HSCALE = field_spec("VDS_HSCALE")
IF_HSYNC_RST = field_spec("IF_HSYNC_RST")
IF_HB_SP2 = field_spec("IF_HB_SP2")
IF_HB_ST2 = field_spec("IF_HB_ST2")

FETCH_MAX = 1023
CAPTURE_GRANULE = 2

HELP = """
  Enter / +   +1        -        -1          fine
  ]           +10       [        -10         coarse
  }           +50       {        -50         very coarse
  x           switch what the keys move: fetch <-> capture window
  m           mark this state, with a note -- say what the green did
  s           show the arithmetic again
  f           freeze on/off
  ?           this list
  q           quit, restore, and print the marks

  The capture window slides at fixed width, in granules of 2.
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


def state(host):
    fetch = read_field(host, PB_FETCH_NUM)
    offset = read_field(host, PB_CAP_OFFSET)
    scale = read_field(host, VDS_HSCALE)
    total = read_field(host, IF_HSYNC_RST)
    start = read_field(host, IF_HB_SP2)
    stop = read_field(host, IF_HB_ST2)
    if None in (fetch, offset, scale, total, start, stop):
        return None
    return {"fetch": fetch, "offset": offset, "scale": scale, "line": total + 1,
            "start": start, "stop": stop, "capture": stop - start}


def show(now):
    if now is None:
        print("  (could not read the registers this pass)")
        return
    fetch = now["fetch"]
    print(f"  capture {now['start']}..{now['stop']} = {now['capture']} units "
          f"of a {now['line']} line   HSCALE {now['scale']}")
    print(f"  PB_FETCH_NUM {fetch}  PB_CAP_OFFSET {now['offset']}  "
          f"delta {now['offset'] - fetch:+d}  "
          f"capture/fetch {now['capture'] / fetch if fetch else 0.0:.3f}")


def frozen(host):
    payload = get_json(host, "/freeze")[1]
    return isinstance(payload, dict) and payload.get("frozen") is True


def slide(host, now, delta):
    """Move start and stop together, so the width and the picture size hold."""
    delta *= CAPTURE_GRANULE
    start, stop = now["start"] + delta, now["stop"] + delta
    if start < 0 or stop > now["line"] - 1:
        print("  window is against the end of the line")
        return
    write_field(host, IF_HB_ST2, stop)
    write_field(host, IF_HB_SP2, start)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    args = parser.parse_args()

    if get_json(args.host, "/freeze")[1] is None:
        sys.exit("this firmware has no /freeze, so the engine would overwrite "
                 "everything written here")

    before = state(args.host)
    if before is None:
        sys.exit("could not read the framing to restore it")

    target = "fetch"
    marks = []

    show(before)
    print(HELP)

    try:
        get(args.host, "/freeze?on=1")
        time.sleep(0.5)
        if not frozen(args.host):
            sys.exit("could not arm the freeze")

        while True:
            now = state(args.host)
            if now is None:
                print("  (read failed -- try again)")
                continue
            reply = input(f"[{target}  fetch {now['fetch']}  "
                          f"capture {now['start']}..{now['stop']}] ").strip()

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
            elif reply == "x":
                target = "capture" if target == "fetch" else "fetch"
                print(f"  keys now move the {target}")
                continue
            elif reply == "m":
                note = input("  note: ").strip()
                marks.append({"note": note, **now})
                print(f"  marked fetch {now['fetch']} at capture "
                      f"{now['start']}..{now['stop']}")
                continue
            elif reply == "s":
                show(now)
                continue
            elif reply == "f":
                if frozen(args.host):
                    get(args.host, "/freeze?on=0")
                    print("  released -- move the zoom, then f again to freeze")
                else:
                    get(args.host, "/freeze?on=1")
                    time.sleep(0.5)
                    print("  frozen again")
                    show(state(args.host))
                continue
            elif reply == "?":
                print(HELP)
                continue
            elif reply in ("q", "quit"):
                break
            else:
                print("  ? for the key list")
                continue

            if target == "fetch":
                write_field(args.host, PB_FETCH_NUM,
                            max(1, min(FETCH_MAX, now["fetch"] + delta)))
            else:
                slide(args.host, now, delta)
    finally:
        write_field(args.host, IF_HB_ST2, before["stop"])
        write_field(args.host, IF_HB_SP2, before["start"])
        write_field(args.host, PB_FETCH_NUM, before["fetch"])
        get(args.host, "/freeze?on=0")
        print(f"\nrestored fetch {before['fetch']} and capture "
              f"{before['start']}..{before['stop']}, automation released")

    if not marks:
        return
    print("\n  fetch  offset  capture      width  cap/fetch  HSCALE  note")
    for m in marks:
        fetch = m["fetch"]
        print(f"  {fetch:5d}  {m['offset']:6d}  "
              f"{m['start']:4d}..{m['stop']:<5d}  {m['capture']:5d}  "
              f"{m['capture'] / fetch if fetch else 0.0:9.3f}  "
              f"{m['scale']:6d}  {m['note']}")
    print("\n  X tracks the fetch if the threshold stop moves with it; X is "
          "absolute if the same stop is the threshold at every fetch. Three "
          "fetches minimum -- two points cannot disconfirm a line.")


if __name__ == "__main__":
    main()
