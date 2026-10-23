#!/usr/bin/env python3
"""How bad each acquisition is, over a night of transitions, unattended.

    python3 tools/gbsc-pro-hwtest/transition_soak.py --host <ip> --hours 9
    python3 tools/gbsc-pro-hwtest/transition_soak.py --host <ip> --legs 60 \
        --only vga-pass,vga-scaled-720p
    python3 tools/gbsc-pro-hwtest/transition_soak.py --report runs/soak.jsonl

**A SYMPTOM THAT IS NOT THERE ON EVERY ENTRY IS A DISTRIBUTION, NOT A STATE.**
One clean capture after one transition says nothing about how often a solve
lands badly, so this takes the transition many times and keeps every reading.

Each leg picks a different input, route and output at random, applies them in
an order that leaves the leg's own transition last, waits for the source, and
scores the emitted frame with `frame_cells.py` -- which localises, so a
disturbance confined to the test card's grating is not averaged away by the
rest of the picture.

What is kept per leg: the registers a bad solve would show in, both channels'
maps as text, and the leg that preceded it -- a solve is judged by what it
came from. A leg reading far above its own type's level also keeps a PNG, so
the rare bad one is an image rather than a number.

**ONE PROCESS MAY DRIVE THE UNIT AT A TIME.** Two runs calling `/input` time
each other's source changes, which reads as an unreliable unit.

Scoring assumes the test card, so the `vga` legs carry the measurement. The
Wii's menu animates and has no grating, so its legs are a health check and a
transition rather than a shimmer reading.

`docs/investigations/the-sampling-phase-cannot-reach-the-shimmer.md` is the
artefact. `docs/bench-output-capture.md` is the dongle, including why a
capture asked for at the wrong size is resampled by it.
"""

import argparse
import json
import random
import signal
import statistics
import sys
import time
from pathlib import Path

import numpy as np

import gbs_unit as g
from frame_cells import breathing, heat_map, measure, moving, summary
from hdmi_capture import borders, device, frames, luma, write_png

# input, route and output, with what the dongle must be asked for so it does
# not resample. A pass-through size is the SOURCE's raster, so it holds only
# while the source stays in the mode the run started from.
LEGS = {
    "vga-scaled-1080p": dict(input="vga", passthrough=False, output="1080p",
                             size=(1920, 1080)),
    "vga-scaled-720p": dict(input="vga", passthrough=False, output="720p",
                            size=(1280, 720)),
    "vga-pass": dict(input="vga", passthrough=True, output=None,
                     size=(1280, 720)),
    "ypbpr-scaled-1080p": dict(input="ypbpr", passthrough=False, output="1080p",
                               size=(1920, 1080)),
    "ypbpr-pass": dict(input="ypbpr", passthrough=True, output=None,
                       size=(720, 480)),
}

# Readings of a leg's own type before one can be judged against them. Below
# this every leg is kept, so each type has references in the log.
OUTLIER_HISTORY = 6
OUTLIER_MULTIPLE = 3.0

# Contrast modulation a leg has to clear before its type's level matters. A
# quiet type's median is near the capture's own noise, and a multiple of that
# flags every leg above it.
OUTLIER_FLOOR = 0.02

REGISTERS = ["PLLAD_MD", "STATUS_SYNC_PROC_HTOTAL", "STATUS_SYNC_PROC_VTOTAL",
             "PA_SP_S", "PA_ADC_S", "HD_HSYNC_RST", "SP_SOG_MODE",
             "DAC_RGBS_BYPS2DAC", "OUT_SYNC_SEL", "VDS_HSYNC_RST",
             "VDS_VSYNC_RST", "SP_H_PULSE_IGNOR", "SP_PRE_COAST",
             "SP_POST_COAST"]

# The sync processor is not following the source below this, so an acquisition
# reported before it counts is the field left over from the source that went.
LOCKED_VTOTAL_MIN = 200


def plan(now, want):
    """The actions taking `now` to `want`, the leg's own transition last.

    Route first: a source arriving while the other route is preferred acquires
    on it and is re-resolved onto this one, which is a transition the leg did
    not ask for standing between the capture and the one it did.
    """
    actions = []
    if now["passthrough"] != want["passthrough"]:
        actions.append(("route", want["passthrough"]))
    if now["input"] != want["input"]:
        actions.append(("input", want["input"]))
    if want["output"] and now["output"] != want["output"]:
        actions.append(("output", want["output"]))
    return actions


def next_leg(rng, names, current):
    """A different leg, uniformly."""
    return rng.choice([n for n in names if n != current] or names)


def is_outlier(score, history, multiple=OUTLIER_MULTIPLE, floor=OUTLIER_FLOOR,
               least=OUTLIER_HISTORY):
    """Whether this leg reads far above its own type's level so far."""
    if len(history) < least:
        return True
    return score > multiple * max(statistics.median(history), floor)


def _state(host):
    """Which input, route and output are in force."""
    fields = g.read_fields(host, ["DAC_RGBS_BYPS2DAC", "VDS_VSYNC_RST"])
    lines = (fields.get("VDS_VSYNC_RST") or 0) + 1
    output = next((n for n, total in g.OUTPUT_FRAME_LINES.items() if total == lines),
                  None)
    return dict(input=g.setting(host, "input"),
                passthrough=bool(fields.get("DAC_RGBS_BYPS2DAC")),
                output=output)


def _counting(host):
    """Wait for the sync processor to count the source, rather than for a
    `state` the input change has not yet cleared."""
    return g.wait_for(
        lambda: (g.read_named(host, "STATUS_SYNC_PROC_VTOTAL") or 0) > LOCKED_VTOTAL_MIN
        or None, timeout=45.0, interval=0.5) is not None


def _set_route(host, passthrough, tries=3):
    """Toggle the pass-through preference until the route in force matches."""
    for _ in range(tries):
        if bool(g.read_named(host, "DAC_RGBS_BYPS2DAC")) == passthrough:
            return True
        g.get(host, "/uc?x")
        time.sleep(3.0)
        g.wait_for(lambda: bool(g.read_named(host, "DAC_RGBS_BYPS2DAC")) == passthrough
                   or None, timeout=20.0, interval=0.5)
    return bool(g.read_named(host, "DAC_RGBS_BYPS2DAC")) == passthrough


def apply_leg(host, actions):
    """Carry out a plan, saying which steps took and which was last."""
    done = []
    for kind, value in actions:
        if kind == "route":
            ok = _set_route(host, value)
        elif kind == "input":
            g.select_input(host, value)
            ok = _counting(host)
        else:
            ok = g.choose_output(host, value)
        done.append(dict(action=kind, value=value, ok=bool(ok)))
    return done


def score(host, leg, settle, frame_count, cell, dev):
    """One leg's capture and what the registers read beside it."""
    width, height = leg["size"]
    time.sleep(settle)
    registers = g.read_fields(host, REGISTERS)
    geometry = g.get_json(host, "/geometry")[1] or {}
    rgb = frames(frame_count, dev, width, height, warmup=12)
    grey = luma(rgb)
    reading = dict(registers=registers, state=geometry.get("state"),
                   line_rate=geometry.get("lineRateHz"),
                   capture=f"{width}x{height}", mean_luma=float(grey.mean()),
                   margins=borders(grey[0]))

    result = measure(grey, cell)
    if result is None:
        reading["black"] = True
        return reading, rgb
    for key in ("move_sd", "cmod"):
        stats = summary(result, key)
        reading[key] = stats
        reading[key + "_map"] = "\n".join(heat_map(result, key)[0])
    reading["moving"] = [(c["x"], c["y"], round(c["move_sd"], 4))
                         for c in moving(result["cells"])[:6]]
    reading["breathing"] = [(c["x"], c["y"], round(c["cmod"], 4))
                            for c in breathing(result["cells"])[:6]]
    return reading, rgb


def _recover(host):
    """Give the encoder its sync back, then force a detection pass."""
    g.write_named(host, "PAD_SYNC_OUT_ENZ", 1)
    time.sleep(0.5)
    g.write_named(host, "PAD_SYNC_OUT_ENZ", 0)
    time.sleep(3.0)
    if _counting(host):
        return "pad"
    g.get(host, "/sc?~")
    return "detect" if _counting(host) else "failed"


def run(args):
    host, dev = args.host, args.device or device()
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    shots = out.parent / (out.stem + "-frames")
    shots.mkdir(exist_ok=True)

    names = [n for n in LEGS if not args.only or n in args.only.split(",")]
    rng = random.Random(args.seed)
    history, kept, failures = {n: [] for n in names}, 0, 0
    current = _state(host)
    previous = "start"
    stopping = {"now": False}
    # SIGTERM as well as SIGINT, so an unattended run killed rather than
    # interrupted still finishes its leg and prints the distribution.
    for caught in (signal.SIGINT, signal.SIGTERM):
        signal.signal(caught, lambda *_: stopping.__setitem__("now", True))

    deadline = time.time() + args.hours * 3600 if args.hours else None
    index = 0
    with out.open("a") as log:
        while not stopping["now"] and (args.legs == 0 or index < args.legs) \
                and (deadline is None or time.time() < deadline):
            index += 1
            name = next_leg(rng, names, previous)
            leg = LEGS[name]
            started = time.time()
            actions = apply_leg(host, plan(current, leg))
            settled = g.acquired_and_settled(host, limit_s=60.0)
            reading, rgb = score(host, leg, args.settle, args.frames, args.cell, dev)

            worst = (reading.get("cmod") or {}).get("worst", 0.0)
            bad = not settled or reading.get("black") or not all(
                step["ok"] for step in actions)
            flagged = bad or is_outlier(worst, history[name])
            if not bad:
                history[name].append(worst)

            shot = None
            if flagged and kept < args.keep_frames:
                shot = shots / f"{index:05d}-{name}.png"
                write_png(shot, rgb[0])
                kept += 1

            record = dict(leg=index, name=name, after=previous,
                          at=time.strftime("%Y-%m-%dT%H:%M:%S"),
                          seconds=round(time.time() - started, 1),
                          actions=actions, settled=bool(settled),
                          flagged=bool(flagged),
                          frame=str(shot) if shot else None, **reading)
            log.write(json.dumps(record) + "\n")
            log.flush()

            where = (reading.get("cmod") or {}).get("at")
            print(f"{index:5} {name:19} after {previous:19} "
                  f"cmod {worst:7.4f} at {where}  move "
                  f"{(reading.get('move_sd') or {}).get('worst', 0.0):7.4f}  "
                  f"{'BAD ' if bad else ''}{'kept' if shot else ''}", flush=True)

            if bad:
                failures += 1
                print(f"      recovery: {_recover(host)}", flush=True)
                if failures >= args.give_up:
                    print(f"stopping: {failures} legs failed", flush=True)
                    break
            else:
                failures = 0
            previous, current = name, dict(input=leg["input"],
                                           passthrough=leg["passthrough"],
                                           output=leg["output"] or current["output"])
    print(f"\n{index} legs, log at {out}")
    report(out)
    return 0


def report(path):
    """Per leg type, the distribution of the worst cell's contrast modulation."""
    rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line]
    if not rows:
        print("nothing logged")
        return
    print(f"\n{'leg':20} {'n':>4} {'bad':>4} {'median':>8} {'p90':>8} {'worst':>8}"
          f"  worst at")
    for name in sorted({r["name"] for r in rows}):
        mine = [r for r in rows if r["name"] == name]
        scores = [(r.get("cmod") or {}).get("worst", 0.0) for r in mine
                  if r.get("cmod")]
        bad = sum(1 for r in mine if not r["settled"] or r.get("black"))
        if not scores:
            print(f"{name:20} {len(mine):4} {bad:4}   nothing measurable")
            continue
        top = max(mine, key=lambda r: (r.get("cmod") or {}).get("worst", 0.0))
        print(f"{name:20} {len(mine):4} {bad:4} {statistics.median(scores):8.4f} "
              f"{np.percentile(scores, 90):8.4f} {max(scores):8.4f}"
              f"  leg {top['leg']} at {(top.get('cmod') or {}).get('at')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host")
    parser.add_argument("--out", default="runs/transition-soak.jsonl")
    parser.add_argument("--report", default=None,
                        help="summarise an existing log and exit")
    parser.add_argument("--hours", type=float, default=0.0)
    parser.add_argument("--legs", type=int, default=0, help="0 means until --hours")
    parser.add_argument("--only", default=None, help="comma-separated leg names")
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--cell", type=int, default=64)
    parser.add_argument("--settle", type=float, default=10.0,
                        help="seconds after the source holds before capturing; "
                             "the dongle delivers black for some of it")
    parser.add_argument("--keep-frames", type=int, default=60)
    parser.add_argument("--give-up", type=int, default=10,
                        help="consecutive failed legs before stopping")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--device", default=None)
    args = parser.parse_args()

    if args.report:
        report(Path(args.report))
        return 0
    if not args.host:
        parser.error("--host is required unless --report")
    if not args.hours and not args.legs:
        parser.error("give --hours or --legs")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
