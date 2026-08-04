#!/usr/bin/env python3
"""Measure where a captured line lands in the output raster.

    python3 tools/gbsc-pro-hwtest/write_origin.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/write_origin.py --host <ip> --plan   # no writes

`geometry_math.solve_horizontal` returns widths and refuses to return positions,
because where the scaler starts writing a captured line has never been measured
-- the geometry handover records it as *assumed* to be VDS_HB_SP. Everything
about pan rests on that assumption, so it needs to stop being one.

Two questions, and they have different consequences:

  PAN     Shift the input capture window along the line, width unchanged. Does
          the picture slide across the screen, or does the frame stay put while
          different source content appears inside it?

          Sliding means the write origin tracks position within the line, so
          panning has to move the display window to compensate -- pan and scale
          stop being independent. Staying put means input blanking alone
          expresses pan, which is the design.

  ORIGIN  Move VDS_HB_SP with everything else fixed. Does the picture's left
          edge follow it?

          This is the assumption stated in geometry_math, tested directly.

Neither is readable from a register: no status field reports where active video
sits inside the output raster. So this drives the unit and asks what you see.

Safety: freezes the firmware first, so a preset load cannot move PLLAD_MD
underneath the experiment, and restores every register it touched on the way
out, including on Ctrl-C. The freeze is never persisted -- a reboot clears it.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit
import geometry_math
import setfield

# Read-modify-write of a spanning field is fiddly enough that it lives in
# setfield; this only needs the names.
CAPTURE = ("IF_HB_ST2", "IF_HB_SP2")
MEMORY = ("VDS_HB_ST", "VDS_HB_SP")
DISPLAY = ("VDS_DIS_HB_ST", "VDS_DIS_HB_SP")
CONTEXT = ("IF_HSYNC_RST", "VDS_HSYNC_RST", "VDS_HSCALE") + CAPTURE + MEMORY + DISPLAY


def read_all(host, spec_map):
    return {name: setfield.read_field(host, spec_map[name]) for name in CONTEXT}


def show(state, hscale_note=True):
    capture = state["IF_HB_ST2"] - state["IF_HB_SP2"]
    print(f"    capture {capture:>5} units  [{state['IF_HB_SP2']}..{state['IF_HB_ST2']}]"
          f"  of a {state['IF_HSYNC_RST'] + 1} unit line")
    print(f"    memory  {state['VDS_HB_ST'] - state['VDS_HB_SP']:>5} px     "
          f"[{state['VDS_HB_SP']}..{state['VDS_HB_ST']}]")
    print(f"    display {state['VDS_DIS_HB_ST'] - state['VDS_DIS_HB_SP']:>5} px     "
          f"[{state['VDS_DIS_HB_SP']}..{state['VDS_DIS_HB_ST']}]")
    if not hscale_note or not state["VDS_HSCALE"] or capture <= 0:
        return          # a negative capture makes the headroom arithmetic report
                        # a huge comfortable margin, which is worse than silence
    line = state["VDS_HSYNC_RST"] + 1
    print(f"    HSCALE  {state['VDS_HSCALE']}  "
          f"(x{1024 / state['VDS_HSCALE']:.3f})  output line {line} px")

    # What the headroom rule makes of the state you are actually in. Worth
    # printing every run: the rule was bracketed at ONE output preset (1445 px
    # line) and geometry_math says so, and a state that reads unsafe here while
    # looking clean on the TV is evidence the threshold is not a constant pixel
    # count. That is a bigger finding than anything this tool sets out to
    # measure, so it should not go unnoticed.
    produced = (state["IF_HB_ST2"] - state["IF_HB_SP2"]) * 1024 / state["VDS_HSCALE"]
    headroom = (state["VDS_HB_ST"] - state["VDS_HB_SP"]) - produced
    verdict = ("OK" if headroom >= geometry_math.HEADROOM_SAFE_PX else
               "thin" if headroom >= geometry_math.HEADROOM_MIN_PX else
               "BELOW the rule -- should be tearing")
    print(f"    produces {produced:.1f} px into a {state['VDS_HB_ST'] - state['VDS_HB_SP']} px "
          f"memory window: {headroom:.1f} px headroom, {verdict}")
    if headroom < geometry_math.HEADROOM_MIN_PX:
        print(f"      (the rule was measured at a {1445} px output line, this is "
              f"{line} px -- if the picture is clean, the rule does not "
              f"generalise)")


def ask(question, options):
    print(f"\n  {question}")
    for key, text in options.items():
        print(f"    [{key}] {text}")
    while True:
        answer = input("  > ").strip().lower()
        if answer in options:
            return answer
        print("    not one of the options")


def apply_fields(host, spec_map, changes, dry):
    for name, value in changes.items():
        spec = spec_map[name]
        if dry:
            print(f"    would set {name} = {value}")
            continue
        if not setfield.apply(host, name, spec, value, dry_run=False):
            raise RuntimeError(f"write to {name} failed")
    if not dry:
        time.sleep(0.4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--shift", type=int, default=0,
                    help="capture-window shift in IF units (default: 1/8 of the "
                         "capture width, rounded to something visible)")
    ap.add_argument("--plan", action="store_true",
                    help="print the trials and the writes, touch nothing")
    ap.add_argument("--out", default=None, help="write the answers to this JSON file")
    args = ap.parse_args()

    spec_map = setfield.load_map()
    missing = [n for n in CONTEXT if n not in spec_map]
    if missing:
        print(f"register map is missing {missing}")
        return 1

    # **FREEZE BEFORE READING.** Two --plan runs two minutes apart read entirely
    # different geometry -- capture [112..1088] with HSCALE 607, then [104..8]
    # with HSCALE 512 -- because the firmware reloads a preset in between. A
    # baseline captured unfrozen describes a state that has already gone, and it
    # is what gets restored at the end.
    frozen = gbs_unit.get(args.host, "/freeze?on=1")
    print(f"froze the firmware: {frozen}\n")

    base = None
    answers = {"trials": []}
    try:
        base = read_all(args.host, spec_map)
        if None in base.values():
            print("could not read the geometry; is the unit up?")
            return 1

        capture_width = base["IF_HB_ST2"] - base["IF_HB_SP2"]
        print("baseline:")
        show(base)
        if capture_width <= 0:
            print(f"\ncapture window reads {capture_width} units -- there is no "
                  f"window to pan.\nThe unit is not on a tuned preset; get a "
                  f"picture you want to keep first.")
            return 1
        shift = args.shift or max(8, capture_width // 8)
        print(f"\nshift for the pan trials: {shift} IF units "
              f"({100 * shift / capture_width:.0f}% of the capture width)")

        if args.plan:
            print("\ntrials:")
            print(f"  1 PAN    IF_HB_SP2/ST2 both +{shift}, output side untouched")
            print(f"  2 PAN    IF_HB_SP2/ST2 both -{shift}, output side untouched")
            print(f"  3 ORIGIN VDS_HB_SP +{shift}, everything else untouched")
            print("\nnothing was written")
            return 0

        answers = {"baseline": base, "shift": shift, "trials": []}
        if ask("Is there a picture on the TV right now?",
               {"y": "yes", "n": "no -- stop, this needs a working picture"}) == "n":
            return 1

        for label, changes, question, options in [
            ("pan +", {"IF_HB_SP2": base["IF_HB_SP2"] + shift,
                       "IF_HB_ST2": base["IF_HB_ST2"] + shift},
             "The capture window moved LATER in the source line, same width. "
             "What happened on screen?",
             {"s": "the whole picture SLID sideways",
              "c": "the frame stayed put, different CONTENT inside it",
              "n": "no visible change",
              "b": "it BROKE -- tearing, rolling, black"}),
            ("pan -", {"IF_HB_SP2": base["IF_HB_SP2"] - shift,
                       "IF_HB_ST2": base["IF_HB_ST2"] - shift},
             "Now EARLIER in the source line by the same amount. What happened?",
             {"s": "the whole picture SLID sideways",
              "c": "the frame stayed put, different CONTENT inside it",
              "n": "no visible change",
              "b": "it BROKE"}),
            ("origin", {"VDS_HB_SP": base["VDS_HB_SP"] + shift},
             "VDS_HB_SP moved later by the same amount, input untouched. "
             "Did the picture's LEFT EDGE follow it?",
             {"y": "yes, the left edge moved right",
              "w": "the picture got NARROWER but the left edge stayed",
              "n": "nothing moved",
              "b": "it BROKE"}),
        ]:
            print(f"\n--- {label}: {', '.join(f'{k}={v}' for k, v in changes.items())}")
            apply_fields(args.host, spec_map, changes, dry=False)
            answer = ask(question, options)
            answers["trials"].append(
                {"trial": label, "changes": changes, "answer": answer,
                 "meaning": options[answer]})
            apply_fields(args.host, spec_map,
                         {k: base[k] for k in changes}, dry=False)

    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        # Only restore if we got far enough to have a baseline AND actually
        # wrote something. --plan returns through here having promised to touch
        # nothing, and a failed read leaves `base` unset.
        wrote = base is not None and not args.plan
        try:
            if wrote:
                print("\nrestoring the baseline ...")
                apply_fields(args.host, spec_map,
                             {k: base[k] for k in CAPTURE + MEMORY + DISPLAY},
                             dry=False)
        finally:
            gbs_unit.get(args.host, "/freeze?on=0")
            print("unfroze the firmware")
        if wrote:
            after = read_all(args.host, spec_map)
            drift = {k: (base[k], after[k]) for k in CONTEXT if base[k] != after[k]}
            print("restored cleanly" if not drift
                  else f"REGISTERS LEFT CHANGED: {drift}")

    print("\n--- what this says ---")
    for trial in answers["trials"]:
        print(f"  {trial['trial']:<8} {trial['meaning']}")

    pans = [t["answer"] for t in answers["trials"] if t["trial"].startswith("pan")]
    if pans and all(a == "c" for a in pans):
        print("\n  Pan is expressible as input blanking ALONE -- the frame stays put\n"
              "  and the content moves. solve_horizontal can keep returning widths,\n"
              "  and the user's pan/scale maps straight onto IF_HB_ST2/SP2.")
    elif pans and all(a == "s" for a in pans):
        print("\n  The write origin TRACKS position in the line: panning the input\n"
              "  slides the picture, so the display window has to move with it and\n"
              "  pan and scale are not independent. solve_horizontal needs to\n"
              "  return positions, not just widths.")

    if args.out:
        with open(args.out, "w") as handle:
            json.dump(answers, handle, indent=1)
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
