#!/usr/bin/env python3
"""Where the source's picture sits in the input formatter's counter, measured by
clipping it out rather than by locating it on screen.

    python3 tools/gbsc-pro-hwtest/counter_origin.py --host <ip> \\
        --modes "X640 Y480 C256 F60,X1024 Y768 C256 F60"

**A POSITION READ OFF THE EMITTED FRAME CARRIES AN ANCHOR AND THIS DOES NOT.**
Converting a dongle column to a capture unit assumes the dongle's column zero is
the first output pixel the display window shows, which holds only at 1080p and
carries the transmitted window's latch. Creeping the capture window's own start
until the feature leaves the capture asks the counter directly: the value it
leaves at IS the feature's coordinate, whatever the output is doing.

The feature is `PATTERN CARD`'s green frame, one source pixel on the outermost
framebuffer pixel.

**ONLY THE WINDOW'S START CAN CLIP A FEATURE CLEANLY.** Shortening the window
instead leaves the playback fetch sized for the old width, so past the picture's
new end the aperture shows memory nothing wrote -- which is as often green as it
is anything else, and reads as a feature that never leaves. The ruler is the
counter's own: `IF_HSYNC_RST + 1` units span one source line, so units per
source pixel is that over the line the mode file states. `full_margins.py`
is what checks that total against the source, and reads it within a third of a
pixel.

Automation must be frozen, or the solver rewrites the window under the creep.
`ANIM OFF` on ModeServ, or the card's flash rides on every reading.
"""
import argparse
import sys
import time

import numpy as np

sys.path.insert(0, "/home/msharman/Projects/gbsc-pro/tools/gbsc-pro-hwtest")
import card_edges
import full_margins
import gbs_unit
import hdmi_capture
import setfield
import shear

# Dongle pixels either side of where the feature is expected. Wide enough for
# the interpolator's smear and the step's own rounding, narrow enough that the
# card's green colour blocks stay outside it.
SEARCH_HALF = 8

# How far before the expected crossing the creep starts. A transition that
# happens between two frames nobody saw is not a measurement, so the value is
# crept TO rather than landed on.
RUN_UP = 25
OVERRUN = 10

# Frames per step, and the dongle warm-up they are taken after. The link does
# not move during a walk, so the warm-up a source change needs is most of it --
# and with the card's animation off a handful of frames is noise alone.
STEP_FRAMES = 4
STEP_WARMUP = 10

# The output resolutions /uc takes, for the invariance check.
OUTPUTS = {"1080p": "s", "960p": "f", "720p": "g", "1024p": "p"}

FIELDS = ["PLLAD_MD", "IF_HSYNC_RST", "IF_HB_SP2", "IF_HB_ST2",
          "IF_VB_SP", "IF_VB_ST", "VDS_HSCALE", "VDS_VSCALE",
          "VDS_HSYNC_RST", "VDS_VSYNC_RST", "SP_RT_HS_SP",
          "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HLOW_LEN", "IF_HBIN_SP"]


def set_field(host, spec, value):
    """One field, quietly, preserving every other bit in the bytes it spans."""
    writes = setfield.byte_writes(host, spec, value)
    if writes is None:
        return False
    for register, _old, new in writes:
        if gbs_unit.write_reg(host, spec["seg"], register, new) is None:
            return False
    return setfield.read_field(host, spec) == value


def greenest(clip, axis):
    """Each position's greenest moment along `axis`, as green DOMINANCE.

    Dominance rather than brightness: the frame is one source pixel wide and the
    scaler interpolates its edges into whatever sits beside it, so its captured
    brightness varies while its hue does not.
    """
    best = np.zeros(clip.shape[1 + axis], np.float32)
    for rgb in clip:
        r, g, b = (rgb[:, :, i].astype(np.float32) for i in range(3))
        best = np.maximum(best, (g - np.maximum(r, b)).mean(axis=1 - axis))
    return best


def amplitude(profile, at):
    """The feature's green weight in a band that follows where it should be."""
    low = max(0, int(round(at)) - SEARCH_HALF)
    high = min(len(profile), int(round(at)) + SEARCH_HALF + 1)
    if high <= low:
        return 0.0
    return float(np.maximum(profile[low:high] - card_edges.GREEN_HUE, 0).sum())


def half_crossing(walk):
    """Where the feature is half gone, from (register, amplitude) in order.

    The frame is one source pixel and the window's start is a whole unit, so it
    is captured in part for a unit or two either side. A first-reading-with-no-
    green test follows the threshold; a half-amplitude point does not.
    """
    if not walk:
        return None
    full = max(level for _at, level in walk)
    if full <= 0.0:
        return None
    half = full / 2.0
    for index in range(1, len(walk)):
        before, after = walk[index - 1], walk[index]
        if before[1] >= half > after[1]:
            span = before[1] - after[1]
            if span <= 0:
                return float(after[0])
            share = (before[1] - half) / span
            return before[0] + (after[0] - before[0]) * share
    return None


def creep(host, dev, spec, axis, start, stop, expected_at, per_register):
    """Creep one window edge through the feature, a unit at a time.

    `expected_at` is where the feature sits at `start`, and `per_register` how
    far it travels per register unit -- zero for an edge whose movement does not
    displace it.
    """
    step = 1 if stop >= start else -1
    walk = []
    for value in range(start, stop + step, step):
        if not set_field(host, spec, value):
            return None, walk
        time.sleep(0.45)
        clip = hdmi_capture.frames(STEP_FRAMES, dev, warmup=STEP_WARMUP)
        at = expected_at - (value - start) * per_register
        walk.append((value, amplitude(greenest(clip, axis), at)))
    return half_crossing(walk), walk


def near_run(clip, axis):
    """The card frame's two edges, as centroids in captured pixels.

    The outermost green runs, not the outermost within a quarter of the frame:
    the card's own colour blocks sit between them, so the frame's edges are the
    first and last green anywhere. A quarter test instead refuses a reading
    whose far edge lands a column inside the boundary.
    """
    runs = card_edges.green_runs(clip, axis)
    if len(runs) < 2:
        return None, None
    profile = greenest(clip, axis)

    def centroid(run):
        at = np.arange(run[0], run[1] + 1, dtype=np.float32)
        weight = np.maximum(profile[run[0]:run[1] + 1] - card_edges.GREEN_HUE, 0)
        return float((at * weight).sum() / weight.sum()) if weight.sum() else None

    return centroid(runs[0]), centroid(runs[-1])


def choose_output(host, command):
    """Ask for an output resolution and wait for the raster to hold still.

    The route is queued for loop() and the re-solve takes seconds, while
    card_edges.settled() watches the CAPTURE and so answers before the output
    has moved at all. Waiting on the raster itself is what says it landed.
    """
    if gbs_unit.get(host, f"/uc?{command}")[0] != 200:
        return False

    def held():
        first = gbs_unit.read_named(host, "VDS_HSYNC_RST")
        time.sleep(1.0)
        return first if first and first == gbs_unit.read_named(
            host, "VDS_HSYNC_RST") else None

    return gbs_unit.wait_for(held, timeout=30.0) is not None


def measure(host, dev, mode, h, v, label):
    field = gbs_unit.read_fields(host, FIELDS)
    doubling = max(1, int(round(field["PLLAD_MD"] / float(field["IF_HSYNC_RST"]))))
    units = field["IF_HSYNC_RST"] + 1
    per_unit = (1024.0 / field["VDS_HSCALE"] * full_margins.ENCODER_TOTAL
                / (field["VDS_HSYNC_RST"] + 1.0))
    per_line = 1024.0 / field["VDS_VSCALE"]
    active_px, active_ln = int(h[3]), int(v[3])

    clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, dev)
    across = near_run(clip, 1)
    down = near_run(clip, 0)
    if across[0] is None or down[0] is None:
        print(f"  {label}: the card's frame is not on both edges")
        return None

    print(f"  {label}: md {field['PLLAD_MD']} (x{doubling}) units {units}"
          f"  raster {field['VDS_HSYNC_RST'] + 1}"
          f"  window h {field['IF_HB_SP2']}..{field['IF_HB_ST2']}"
          f" v {field['IF_VB_SP']}..{field['IF_VB_ST']}"
          f"  {units / float(sum(h)):.2f} units/px", flush=True)
    # No floor on the sampling density here. The frame is unreadable below about
    # 1.4 capture units per source pixel when its POSITION is wanted; a clip is
    # a presence test, and 1024x768@60 reads to a tenth of a sample at 1.07.

    specs = setfield.load_map()
    was = {name: field[name] for name in
           ("IF_HB_SP2", "IF_HB_ST2", "IF_VB_SP", "IF_VB_ST")}

    # Only a START edge clips cleanly: the first captured unit is written at the
    # same output pixel whatever the window holds, so the feature travels
    # through the band as the start rises and is gone once it passes.
    plan = (
        ("IF_HB_SP2", 1, across[0], per_unit, field["IF_HB_SP2"]),
        ("IF_VB_SP", 0, down[0], per_line, field["IF_VB_SP"]),
    )
    found = {}
    try:
        for name, axis, at, travel, window_start in plan:
            crosses = window_start + at / travel
            start, stop = int(round(crosses)) - RUN_UP, int(round(crosses)) + OVERRUN
            if not set_field(host, specs[name], start):
                print(f"    {name}: the run-up would not take")
                continue
            time.sleep(1.0)
            at_start = at - (start - window_start) * travel
            crossing, walk = creep(host, dev, specs[name], axis, start, stop,
                                   at_start, travel)
            found[name] = crossing
            print(f"    {name:10} crept {start}..{stop}, expected {crosses:.0f}"
                  f"  crossing {crossing if crossing is None else round(crossing, 1)}"
                  f"   ({len(walk)} steps)", flush=True)
            set_field(host, specs[name], was[name])
            time.sleep(0.6)
    finally:
        for name, value in was.items():
            set_field(host, specs[name], value)

    return report(field, h, v, doubling, units, active_px, active_ln, found)


def report(field, h, v, doubling, units, active_px, active_ln, found):
    near, top = found.get("IF_HB_SP2"), found.get("IF_VB_SP")
    units_px = units / float(sum(h))
    if near is not None:
        lead = near / units_px
        filed = h[0] + h[1] + h[2]
        samples = (lead - filed) * doubling * units_px
        print(f"    h lead {lead:.1f} px against {filed}  ->  {lead - filed:+.1f} px"
              f" = {samples:+.1f} ADC samples"
              f"   null at SP_RT_HS_SP {field['SP_RT_HS_SP'] + samples:.0f}"
              f" or IF_HBIN_SP {field['IF_HBIN_SP'] + samples:.0f}"
              f" (engine writes {field['SP_RT_HS_SP']} and {field['IF_HBIN_SP']})")
    if top is not None:
        # Against the vsync pulse's LEADING edge, which is the edge a video
        # standard counts from. Taking the trailing edge instead puts the pulse
        # in the comparison, and the bench's pulses are 2, 3 and 6 lines -- so
        # two sources read as disagreeing about the origin when they agree.
        lines = top / doubling
        filed = v[0] + v[1] + v[2]
        print(f"    v lead {lines:.1f} lines against {filed} from the pulse's"
              f" leading edge  ->  the origin sits {filed - lines:.1f} lines in")
    return None if near is None else (near / units_px - (h[0] + h[1] + h[2]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", default=full_margins.MODESERV)
    parser.add_argument("--modes", required=True)
    parser.add_argument("--outputs", default="")
    parser.add_argument("--hold", type=int, default=0,
                        help="hold PLLAD_MD here, so the whole engine solves "
                             "around it. 0 releases a hold a previous run left.")
    parser.add_argument("--hbin", default="",
                        help="IF_HBIN_SP values to read the origin at, comma "
                             "separated. The line doubler's FIFO reset, which "
                             "pans the picture on the doubled path.")
    args = parser.parse_args()

    dev = hdmi_capture.device()
    for mode in [m.strip() for m in args.modes.split(",")]:
        found = full_margins.stated(mode)
        if found is None:
            print(f"{mode}: no entry in the mode file")
            continue
        h, v, _clock = found
        print(f"{mode}", flush=True)
        for output in [o.strip() for o in args.outputs.split(",") if o.strip()] or [None]:
            shear.freeze(args.host, False)
            if output and not choose_output(args.host, OUTPUTS[output]):
                print(f"  {output}: the output raster never settled")
                continue
            reply = gbs_unit.mode_serv(args.modeserv, f"MODE {mode}")
            if not reply or not reply.startswith("OK"):
                print(f"  source refused it: {reply!r}")
                continue
            if not card_edges.settled(args.host):
                print("  never acquired")
                continue
            gbs_unit.mode_serv(args.modeserv, "PATTERN CARD")
            # Always sent, because 0 RELEASES a hold a previous run left behind
            # and a stale one is invisible in the reading it corrupts.
            gbs_unit.get(args.host, f"/sampleclock?hold={args.hold}")
            time.sleep(8.0)
            if full_margins.open_wide(args.host) is None:
                print("  the framing would not open")
                continue
            time.sleep(2.0)
            if not shear.freeze(args.host, True):
                print("  the freeze would not take")
                continue
            specs = setfield.load_map()
            held = gbs_unit.read_named(args.host, "IF_HBIN_SP")
            try:
                for hbin in [h.strip() for h in args.hbin.split(",") if h.strip()] or [None]:
                    label = output or "engine's output"
                    if hbin is not None:
                        if not set_field(args.host, specs["IF_HBIN_SP"], int(hbin)):
                            print(f"  IF_HBIN_SP {hbin}: the write did not take")
                            continue
                        time.sleep(1.0)
                        label = f"{label}, IF_HBIN_SP {hbin}"
                    measure(args.host, dev, mode, h, v, label)
            finally:
                set_field(args.host, specs["IF_HBIN_SP"], held)
                shear.freeze(args.host, False)
        print(flush=True)


if __name__ == "__main__":
    main()
