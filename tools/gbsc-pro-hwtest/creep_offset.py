#!/usr/bin/env python3
"""Jog PB_CAP_OFFSET by hand and watch, with the fetch read in the same pass.

For finding what the playback stride has to be against PB_FETCH_NUM. The offset
is the per-line allocation in 64-bit memory words and the fetch is what playback
reads into it; below the fetch, successive lines overlap and each overwrites its
predecessor's tail, which shows as combing or as skewed content where lines stop
lining up. Whether one constant serves every framing or the stride has to follow
the fetch is the open measurement.

    python3 tools/gbsc-pro-hwtest/creep_offset.py --host 192.168.88.108

It does NOT move the green band down the right of the picture. That is a fixed
position in the line, IF 1125, and no offset reaches it. docs/capture-limits.md

Every mark records the offset AND the fetch, capture and HSCALE behind it. That
is the point of the tool: a value paired with a fetch read at some other moment
is not a measurement, and a cached register panel invalidated a day of them.

Automation is frozen while jogging, or the engine recomputes the offset from the
fetch and overwrites whatever is typed here. The offset is restored on the way
out; `f` releases the freeze so the zoom can be moved between runs, because the
whole question is whether the requirement tracks the fetch.

Two things it deliberately does NOT do. It does not jump: a transition between
two frames the eye never saw is not a measurement. And it does not bisect:
corruption on this board comes in bands, so a boundary found by halving is only
the real one if nothing clean lies beyond it.
"""
import argparse
import sys
import time

from gbs_unit import get, get_json

PB_CAP_OFFSET = (4, 0x37, 0, 10)
PB_FETCH_NUM = (4, 0x39, 0, 10)
VDS_HSCALE = (3, 0x16, 0, 10)
IF_HB_SP2 = (1, 0x1A, 0, 11)
IF_HB_ST2 = (1, 0x18, 0, 11)

OFFSET_MAX = 1023

HELP = """
  Enter / +   +1        -        -1          fine
  ]           +10       [        -10         coarse
  }           +50       {        -50         very coarse
  m           mark this value, with a note
  s           show the arithmetic again
  f           freeze on/off -- release it to move the zoom, then freeze again
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


def state(host):
    """The fetch and the framing behind it, read in ONE pass.

    Never pair an offset with a fetch read at another moment: the engine
    recomputes the fetch from the capture on every solve.
    """
    fetch = read_field(host, PB_FETCH_NUM)
    scale = read_field(host, VDS_HSCALE)
    sp2 = read_field(host, IF_HB_SP2)
    st2 = read_field(host, IF_HB_ST2)
    if None in (fetch, scale, sp2, st2):
        return None
    return {"fetch": fetch, "scale": scale, "capture": st2 - sp2}


def show(now, offset):
    if now is None:
        print("  (could not read the registers this pass)")
        return
    fetch = now["fetch"]
    ratio = offset / fetch if fetch else 0.0
    print(f"  capture {now['capture']}  HSCALE {now['scale']}  "
          f"PB_FETCH_NUM {fetch}  offset {offset}  "
          f"delta {offset - fetch:+d}  ratio {ratio:.3f}")


def frozen(host):
    payload = get_json(host, "/freeze")[1]
    return isinstance(payload, dict) and payload.get("frozen") is True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    args = parser.parse_args()

    if get_json(args.host, "/freeze")[1] is None:
        sys.exit("this firmware has no /freeze, so the engine would overwrite "
                 "everything written here")

    before = read_field(args.host, PB_CAP_OFFSET)
    if before is None:
        sys.exit("could not read PB_CAP_OFFSET to restore it")

    offset = before
    marks = []

    show(state(args.host), offset)
    print(f"starting at PB_CAP_OFFSET {before}")
    print(HELP)

    try:
        get(args.host, "/freeze?on=1")
        time.sleep(0.5)
        if not frozen(args.host):
            sys.exit("could not arm the freeze")

        while True:
            now = state(args.host)
            fetch = now["fetch"] if now else 0
            reply = input(f"[{offset}  fetch {fetch}  "
                          f"delta {offset - fetch:+d}] ").strip()

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
            elif reply == "m":
                note = input("  note: ").strip()
                marks.append({"offset": offset, "note": note, **(now or {})})
                print(f"  marked offset {offset} against fetch {fetch}")
                continue
            elif reply == "s":
                show(now, offset)
                continue
            elif reply == "f":
                if frozen(args.host):
                    get(args.host, "/freeze?on=0")
                    print("  released -- move the zoom, then f again to freeze")
                else:
                    get(args.host, "/freeze?on=1")
                    time.sleep(0.5)
                    offset = read_field(args.host, PB_CAP_OFFSET) or offset
                    print("  frozen again")
                    show(state(args.host), offset)
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
                offset = max(0, min(OFFSET_MAX, offset + delta))
                write_field(args.host, PB_CAP_OFFSET, offset)
    finally:
        write_field(args.host, PB_CAP_OFFSET, before)
        get(args.host, "/freeze?on=0")
        print(f"\nrestored PB_CAP_OFFSET {before}, automation released")

    if not marks:
        return
    print("\n  offset  fetch  delta   ratio  capture  HSCALE  note")
    for m in marks:
        fetch = m.get("fetch", 0)
        ratio = m["offset"] / fetch if fetch else 0.0
        print(f"  {m['offset']:6d}  {fetch:5d}  {m['offset'] - fetch:+5d}  "
              f"{ratio:6.3f}  {m.get('capture', 0):7d}  {m.get('scale', 0):6d}"
              f"  {m['note']}")
    print("\n  A fixed margin holds if delta is constant across framings; a "
          "proportional one if ratio is. Two framings cannot tell them apart "
          "unless the fetch differs a lot -- take the extremes.")


if __name__ == "__main__":
    main()
