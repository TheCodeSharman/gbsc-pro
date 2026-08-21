#!/usr/bin/env python3
"""Every source mode the bench presents, with its registers and a photograph.

    python3 mode_survey.py --host <ip> --for 600 --out /tmp/survey

**A REGISTER DUMP CANNOT SAY WHETHER THE PICTURE IS RIGHT, AND A PHOTOGRAPH
CANNOT SAY WHY.** Three states measured on this bench read as internally
plausible from the registers alone and only one of them shows a picture: a
consistent divider the ADC PLL cannot lock to, a locked divider with a stale
IF_HSYNC_RST beside it, and the state where all three agree. So this records
both, at the same instant, for each mode the source settles into.

Modes come from whatever is driving the source -- the mode-cycling script -- so
this waits for them rather than asking for them. A mode is a settled pair:
HPERIOD_IF and STATUS_SYNC_PROC_VTOTAL both holding still, because either alone
moves through values no source runs at while a change is in flight.

The line rate is HPERIOD_IF against the chip's own 27 MHz, which does not move
with PLLAD_MD -- so it stays true in exactly the states where the divider is
wrong, which are the ones worth photographing. docs/tv5725-chip.md
"""

import argparse
import json
import os
import subprocess
import time

import gbs_unit

FIELDS = ("PLLAD_MD", "IF_HSYNC_RST", "SP_RT_HS_SP", "STATUS_SYNC_PROC_VTOTAL",
          "STATUS_SYNC_PROC_HTOTAL", "HPERIOD_IF", "STATUS_MISC_PLLAD_LOCK",
          "IF_PRGRSV_CNTRL", "DEC1_BYPS", "DEC2_BYPS", "VDS_HSCALE", "VDS_VSCALE")

CATALOGUE = json.load(open(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "tv5725_registers.json")))

SETTLED_SAMPLES = 5          # consecutive agreeing pairs before it counts
SETTLE_WAIT_S = 3.0          # then let the engine finish reacting before reading


# Looked up once, never hand-written: a wrong offset does not error, it returns
# a plausible number. HPERIOD_IF written out by hand elsewhere read 1455 for a
# register holding 431.
PAIR = tuple(CATALOGUE[name] for name in ("HPERIOD_IF", "STATUS_SYNC_PROC_VTOTAL"))


def read_pair(host):
    """HPERIOD_IF and STATUS_SYNC_PROC_VTOTAL in ONE request. Both live in
    segment 0, and the detection loop has to outrun the mode changes it is
    watching for -- reading each field separately made the loop slower than the
    modes and it saw one in ten minutes."""
    registers = gbs_unit.read_segment(host, 0)
    if not registers:
        return (None, None)
    return tuple(gbs_unit.field_from(registers, f["reg"], f["off"], f["width"])
                 for f in PAIR)


def read_all(host):
    registers = gbs_unit.read_segment(host, 0)
    out = {}
    for name in FIELDS:
        d = CATALOGUE[name]
        if d["seg"] == 0 and registers:
            out[name] = gbs_unit.field_from(registers, d["reg"], d["off"], d["width"])
        else:
            out[name] = gbs_unit.read_field(host, d["seg"], d["reg"], d["off"], d["width"])
    return out


def line_rate(hperiod):
    if not hperiod:
        return 0.0
    return 27000000.0 / ((hperiod + 1) * 4)


def snap(path):
    """A photograph, or None where no camera is set up. Never fatal: the
    register half of the survey is worth having on its own."""
    try:
        subprocess.run(["tv-snap", "-q", "-o", path], check=True,
                       capture_output=True, timeout=60)
        return path
    except Exception:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--for", dest="duration", type=float, default=600.0)
    parser.add_argument("--out", default="/tmp/mode-survey")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    seen = []
    held, run = None, 0
    started = time.time()
    while time.time() - started < args.duration:
        pair = read_pair(args.host)
        if None in pair or not pair[1]:
            held, run = None, 0
            time.sleep(0.1)
            continue

        if held and abs(pair[0] - held[0]) <= 1 and abs(pair[1] - held[1]) <= 1:
            run += 1
        else:
            held, run = pair, 1

        if run == SETTLED_SAMPLES and not any(
                abs(pair[0] - s["pair"][0]) <= 1 and abs(pair[1] - s["pair"][1]) <= 1
                for s in seen):
            time.sleep(SETTLE_WAIT_S)
            state = read_all(args.host)
            name = f"hper{pair[0]}-vt{pair[1]}"
            photo = snap(os.path.join(args.out, name + ".jpg"))
            seen.append({"pair": pair, "state": state, "photo": photo})
            rate = line_rate(state["HPERIOD_IF"])
            vt = state["STATUS_SYNC_PROC_VTOTAL"] or 1
            print(f"  {name:16} {rate/1000:6.2f} kHz {rate/vt:6.2f} Hz  "
                  f"MD={state['PLLAD_MD']:5} IF={state['IF_HSYNC_RST']:5} "
                  f"lock={state['STATUS_MISC_PLLAD_LOCK']} "
                  f"f={state['PLLAD_MD'] * rate / 1e6:5.1f} MHz  {photo or 'no photo'}",
                  flush=True)
        time.sleep(0.1)

    print(f"\n{len(seen)} distinct modes")
    json.dump([{k: v for k, v in s.items()} for s in seen],
              open(os.path.join(args.out, "survey.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
