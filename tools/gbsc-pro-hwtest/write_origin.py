#!/usr/bin/env python3
"""Measure where a captured line lands in the output raster.

    python3 tools/gbsc-pro-hwtest/write_origin.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/write_origin.py --host <ip> --plan   # no writes

Where the scaler starts writing a captured line: VDS_HB_SP + 78 bypassed, + 80
with the scaler engaged, constant over VDS_HB_SP 49..170, and VDS_VB_SP + 26
vertically. `geometry_math.solve_axis` computes positions from those constants,
so what this is for is re-measuring them if the clock or output preset changes.

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
# The vertical pair is read too, so headroom_of() can order on the tighter axis
# rather than assuming the horizontal one is the only hazard.
VERTICAL = ("IF_VB_ST", "IF_VB_SP", "VDS_VB_ST", "VDS_VB_SP", "VDS_VSCALE")
CONTEXT = (("IF_HSYNC_RST", "VDS_HSYNC_RST", "VDS_HSCALE")
           + CAPTURE + MEMORY + DISPLAY + VERTICAL)


AXIS_FIELDS = (
    ("VDS_HB_ST", "VDS_HB_SP", "IF_HB_ST2", "IF_HB_SP2", "VDS_HSCALE"),
    ("VDS_VB_ST", "VDS_VB_SP", "IF_VB_ST", "IF_VB_SP", "VDS_VSCALE"),
)


def axis_headroom(state, fields):
    """One axis's window minus what it has to hold, or None if the state does
    not carry that axis at all."""
    window_st, window_sp, capture_st, capture_sp, scale = fields
    try:
        window = state[window_st] - state[window_sp]
        capture = state[capture_st] - state[capture_sp]
        scale_value = state[scale]
    except KeyError:
        return None
    produced = geometry_math.produced_px(capture, scale_value)
    if produced is None:
        return None
    return window - produced


def headroom_of(state):
    """The tighter of the two axes, or None if the state carries neither.

    This is the quantity setfield's docstring is about: let it go negative for
    even one intermediate step and the picture visibly corrupts. Both axes have
    the hazard -- vertically, every VSCALE below the one in place at VDS_VB
    37..845 goes negative -- so ordering on the horizontal alone would send a
    vertical move in the ruinous order while reporting itself safe.
    """
    measured = [axis_headroom(state, fields) for fields in AXIS_FIELDS]
    present = [value for value in measured if value is not None]
    return min(present) if present else None


def ordered_writes(current, changes):
    """`changes` as (name, value) pairs, ordered so the headroom never dips
    below where the move leaves it.

    Both windows that bound the headroom are two independent registers, so
    moving either takes two writes with a state in between, and one of the two
    orders is wrong. They fail in opposite directions, which is why a rule about
    one pair is not enough:

        capture (IF_HB_SP2/ST2)   widening it raises the produced pixel count
        memory  (VDS_HB_SP/ST)    narrowing it lowers the space available

    So this does not reason about either pair directly. At each step it sends
    whichever remaining write leaves the *most* headroom, which orders both
    pairs correctly and also handles a change that moves both at once -- where
    per-pair rules would still let the two interact into an overrun. Ties keep
    the caller's order, so unrelated fields pass through untouched.

    The bench baseline on 2026-08-05 ran at 14 px of headroom while a mis-ordered
    memory-window slide dipped it to -86. That is the whole margin, several times
    over.
    """
    remaining = list(changes.items())
    if headroom_of(current) is None:
        return remaining      # nothing to reason with; do as we were told
    ordered = []
    state = dict(current)
    while remaining:
        best = max(remaining,
                   key=lambda pair: headroom_of(dict(state, **{pair[0]: pair[1]})))
        remaining.remove(best)
        ordered.append(best)
        state[best[0]] = best[1]
    return ordered


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
    verdict = ("OK" if headroom >= geometry_math.HEADROOM_WARN_PX else
               "thin" if headroom >= geometry_math.HEADROOM_WARN_PX / 2 else
               "BELOW the rule -- should be tearing")
    print(f"    produces {produced:.1f} px into a {state['VDS_HB_ST'] - state['VDS_HB_SP']} px "
          f"memory window: {headroom:.1f} px headroom, {verdict}")
    if headroom < geometry_math.HEADROOM_WARN_PX / 2:
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


def apply_fields(host, spec_map, current, changes, dry):
    for name, value in ordered_writes(current, changes):
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
        # Both pan trials must stay inside the line. The capture window is
        # panned in both directions, so the usable shift is bounded by whichever
        # edge runs out first: ST2 must not reach IF_HSYNC_RST, SP2 must not go
        # negative. An edge past either wraps, and a wrapped window answers "it
        # BROKE" for a reason that has nothing to do with the question.
        # ST2 must stay strictly under IF_HSYNC_RST -- the firmware's own guard
        # is `if (IF_HB_ST2 < IF_HSYNC_RST)` and wraps otherwise, so landing
        # exactly on it is already out of bounds.
        room = min(base["IF_HSYNC_RST"] - base["IF_HB_ST2"] - 1, base["IF_HB_SP2"])
        wanted = args.shift or max(8, capture_width // 8)
        shift = min(wanted, room)
        print(f"\nshift for the pan trials: {shift} IF units "
              f"({100 * shift / capture_width:.0f}% of the capture width)")
        if shift < wanted:
            print(f"  clamped from {wanted}: the window is [{base['IF_HB_SP2']}.."
                  f"{base['IF_HB_ST2']}] in a {base['IF_HSYNC_RST'] + 1} unit line, "
                  f"so {room} is all the room there is on the tighter side")
        if shift < 8:
            print(f"\ncapture window has only {room} units of room to pan. Widen it, "
                  f"or pass --shift explicitly and accept a wrapped edge.")
            return 1

        # The ORIGIN trial moves VDS_HB_SP, which resizes the memory window and
        # therefore moves the headroom. Moving it later shrinks the window, and
        # at a thin baseline that tears -- which would be read as "it BROKE"
        # when the question is only "did the left edge follow". Move it earlier
        # instead: same test of the assumption, and headroom goes up, not down.
        produced = capture_width * 1024 / base["VDS_HSCALE"]
        headroom = (base["VDS_HB_ST"] - base["VDS_HB_SP"]) - produced
        origin_delta = -shift if headroom - shift < geometry_math.HEADROOM_WARN_PX else shift
        origin_word = "later" if origin_delta > 0 else "earlier"
        if origin_delta < 0:
            print(f"  origin trial moves VDS_HB_SP {origin_word} ({origin_delta}): "
                  f"headroom is {headroom:.1f} px and moving it later would take it "
                  f"to {headroom - shift:.1f}, which tears and answers nothing")

        if args.plan:
            print("\ntrials:")
            print(f"  1 PAN    IF_HB_SP2/ST2 both +{shift}, output side untouched")
            print(f"  2 PAN    IF_HB_SP2/ST2 both -{shift}, output side untouched")
            print(f"  3 ORIGIN VDS_HB_SP {origin_delta:+d}, everything else untouched")
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
            ("origin", {"VDS_HB_SP": base["VDS_HB_SP"] + origin_delta},
             f"VDS_HB_SP moved {origin_word} by the same amount, input "
             f"untouched. Did the picture's LEFT EDGE follow it?",
             {"y": f"yes, the left edge moved {'right' if origin_delta > 0 else 'left'}",
              "w": "the picture got WIDER/NARROWER but the left edge stayed",
              "n": "nothing moved",
              "b": "it BROKE"}),
        ]:
            print(f"\n--- {label}: {', '.join(f'{k}={v}' for k, v in changes.items())}")
            apply_fields(args.host, spec_map, base, changes, dry=False)
            answer = ask(question, options)
            answers["trials"].append(
                {"trial": label, "changes": changes, "answer": answer,
                 "meaning": options[answer]})
            # Undoing the trial moves the same window back, so it needs the same
            # ordering care -- and the state it starts from is the trial's, not
            # the baseline's.
            apply_fields(args.host, spec_map, dict(base, **changes),
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
                # Read what is actually there rather than assuming the trials
                # unwound themselves: a Ctrl-C between a trial's two writes
                # leaves the window half-moved, and that is precisely the state
                # whose restore order matters most.
                apply_fields(args.host, spec_map, read_all(args.host, spec_map),
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
              "  and the content moves, which is what geometry_math.pan_capture\n"
              "  assumes and what the user's pan control maps onto.")
    elif pans and all(a == "s" for a in pans):
        print("\n  The write origin TRACKS position in the line: panning the input\n"
              "  slides the picture, so the display window has to move with it and\n"
              "  pan and scale are not independent. geometry_math.pan_capture would\n"
              "  then have to re-solve the output windows on every pan.")

    if args.out:
        with open(args.out, "w") as handle:
            json.dump(answers, handle, indent=1)
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
