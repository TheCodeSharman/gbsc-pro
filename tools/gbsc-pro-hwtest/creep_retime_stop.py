#!/usr/bin/env python3
"""Creep a counter-origin register and measure how far the picture moves.

    python3 tools/gbsc-pro-hwtest/creep_retime_stop.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/creep_retime_stop.py --host <ip> \\
        --field IF_HBIN_SP --start 150 --to 170

The retime window's stop is the input formatter's line counter ORIGIN, and
`retimeStopFor()` writes `PLLAD_MD - pulse + origin`. A pulse narrower than the
origin puts that past the end of the line, which is every CEA-861 HD raster at
the divider the engine picks -- so what the register does above PLLAD_MD decides
whether such a source can be placed at all. A stop that wraps modulo the line
can simply be written; one that is inert needs the residual moving into the
capture window instead.

Two registers carry that origin and both move the picture one ADC sample per
unit, so the same walk measures either: `SP_RT_HS_SP` on every source and
`IF_HBIN_SP` on a line-doubled one. Reading them on one instrument is what makes
a shift in one expressible as a correction to the other.

The readout is the picture's own displacement, cross-correlated against the
frame at the starting value: the content does not change during a walk, so the
correlation peak IS the shift, and it repeats to a hundredth of a dongle column
on an untouched unit. A green feature is not used -- at full framing the card's
frame sits on the boundary, where it cannot move both ways, and a torn capture
puts green everywhere and so reads as a feature that never leaves.

Automation is frozen throughout, because the engine rewrites this register on
every solve. Full framing is opened so the picture spans the raster and no edge
of it is the capture window's. Both, and the register, are restored on the way
out.

It creeps and does not jump: the question is what happens AT the line's end, and
a transition between two frames nobody captured is not a measurement. Run it
with ANIM OFF on the source.
"""
import argparse
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import full_margins                                    # noqa: E402
import gbs_unit                                        # noqa: E402
import hdmi_capture                                    # noqa: E402
from gbs_unit import freeze                           # noqa: E402
from shear import write_field                         # noqa: E402

# Rows of the dongle frame the column profile is averaged over. Inside the
# picture at the bench framing, so a vertical edge cannot enter the band.
BAND = slice(200, 900)

# How far either side of no movement the correlation is searched. Wider than any
# step can travel, narrow enough that the card's repeating structure cannot win.
SEARCH = 120

STEP_FRAMES = 6
STEP_WARMUP = 8
SETTLE_S = 0.35

FIELDS = ["PLLAD_MD", "SP_RT_HS_SP", "SP_RT_HS_ST", "IF_HSYNC_RST",
          "STATUS_SYNC_PROC_HTOTAL", "STATUS_SYNC_PROC_VTOTAL",
          "STATUS_SYNC_PROC_HLOW_LEN", "STATUS_SYNC_PROC_HSPOL",
          "SP_HS_INV_REG", "VDS_HSCALE", "VDS_HSYNC_RST",
          "IF_HB_SP2", "IF_HB_ST2", "ADC_CLK_ICLK1X", "ADC_CLK_ICLK2X"]

WATCH = ["SP_RT_HS_SP", "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HTOTAL",
         "STATUS_SYNC_PROC_HLOW_LEN"]


def profile(dev):
    clip = hdmi_capture.frames(STEP_FRAMES, dev, warmup=STEP_WARMUP)
    luma = clip.mean(axis=0) @ np.array([0.299, 0.587, 0.114], np.float32)
    return luma[BAND].mean(axis=0)


def displacement(reference, now):
    """How far `now` sits from `reference`, in dongle columns, to sub-pixel.

    Positive is a picture that moved RIGHT. A parabola through the peak and its
    neighbours, which is what carries the fraction.

    A CORRELATION PEAK IS NOT A DISPLACEMENT ON ITS OWN. Above the line the
    capture tears to a different offset per line, and the peak then answers
    something different every frame -- `spread` is what tells the two apart.
    """
    a, b = reference - reference.mean(), now - now.mean()
    correlation = np.correlate(a, b, "full")
    middle = len(a) - 1
    low, high = middle - SEARCH, middle + SEARCH
    peak = low + int(np.argmax(correlation[low:high + 1]))
    y0, y1, y2 = correlation[peak - 1], correlation[peak], correlation[peak + 1]
    curve = y0 - 2 * y1 + y2
    fraction = 0.5 * (y0 - y2) / curve if curve else 0.0
    return middle - peak - fraction


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--field", default="SP_RT_HS_SP")
    parser.add_argument("--start", type=int, default=0,
                        help="where to start; the engine's own value by default")
    parser.add_argument("--to", type=int, default=0,
                        help="where to stop; PLLAD_MD + --above by default")
    parser.add_argument("--above", type=int, default=16,
                        help="how far past PLLAD_MD to creep, for the retime stop")
    parser.add_argument("--repeats", type=int, default=2,
                        help="captures per step; their spread is what catches a tear")
    args = parser.parse_args()

    state = gbs_unit.read_fields(args.host, FIELDS + [args.field])
    if state is None:
        return print("could not read the unit") or 1

    divider = state["PLLAD_MD"]
    doubling = max(1, int(round(divider / float(state["IF_HSYNC_RST"]))))
    restore = state[args.field]
    start = args.start or restore
    stop = args.to or divider + args.above

    if not freeze(args.host, True):
        return print("could not freeze automation") or 1
    if full_margins.open_wide(args.host) is None:
        freeze(args.host, False)
        return print("could not open the framing wide") or 1

    state = gbs_unit.read_fields(args.host, FIELDS + [args.field])
    per_unit = (1024.0 / state["VDS_HSCALE"] * full_margins.ENCODER_TOTAL
                / (state["VDS_HSYNC_RST"] + 1.0))
    per_sample = per_unit / doubling
    ratio = 4 if state["ADC_CLK_ICLK2X"] else (2 if state["ADC_CLK_ICLK1X"] else 1)
    pulse = min(state["STATUS_SYNC_PROC_HLOW_LEN"],
                divider - state["STATUS_SYNC_PROC_HLOW_LEN"])

    print(f"md {divider}  line x{doubling}  units {state['IF_HSYNC_RST'] + 1}"
          f"  raster {state['VDS_HSYNC_RST'] + 1}  hscale {state['VDS_HSCALE']}"
          f"  oversample x{ratio}")
    print(f"pulse {pulse}  hspol {state['STATUS_SYNC_PROC_HSPOL']}"
          f"  inv {state['SP_HS_INV_REG']}  rt stop {state['SP_RT_HS_SP']}"
          f"  hbin sp {state.get('IF_HBIN_SP')}"
          f"  window {state['IF_HB_SP2']}..{state['IF_HB_ST2']}")
    print(f"{per_unit:.4f} dongle columns per capture unit,"
          f" {per_sample:.4f} per ADC sample")
    print(f"creeping {args.field} {start} -> {stop}, restoring {restore}\n")

    header = (f"{'value':>6} {'vs md':>6} {'moved px':>9} {'d px':>7}"
              f" {'samples':>8} {'spread':>7} {'vtotal':>7} {'htotal':>7}")
    print(header)
    print("-" * len(header))

    dev = hdmi_capture.device()
    rows, reference, previous = [], None, None
    try:
        for value in range(start, stop + 1):
            if not write_field(args.host, args.field, value):
                print(f"{value:6d}  write refused")
                break
            time.sleep(SETTLE_S)
            seen = []
            for _ in range(max(1, args.repeats)):
                now = profile(dev)
                if reference is None:
                    reference = now
                seen.append(displacement(reference, now))
            moved = float(np.median(seen))
            spread = max(seen) - min(seen)
            watch = gbs_unit.read_fields(args.host, WATCH) or {}
            step = moved - previous if previous is not None else 0.0
            previous = moved
            rows.append((value, moved, spread))
            print(f"{value:6d} {value - divider:+6d} {moved:9.3f} {step:+7.3f}"
                  f" {moved / per_sample:8.2f} {spread:7.2f}"
                  f" {watch.get('STATUS_SYNC_PROC_VTOTAL', -1):7d}"
                  f" {watch.get('STATUS_SYNC_PROC_HTOTAL', -1):7d}", flush=True)
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        write_field(args.host, args.field, restore)
        gbs_unit.get(args.host, "/framing/full?on=0")
        freeze(args.host, False)
        print(f"\nrestored {args.field} {restore}, framing released, automation live")

    steady = [r for r in rows if r[2] < 1.0]
    if len(steady) >= 3:
        at = np.array([r[0] for r in steady], float)
        px = np.array([r[1] for r in steady], float)
        slope = np.polyfit(at, px, 1)[0]
        print(f"over {len(steady)} steady steps: {slope:+.4f} dongle columns"
              f" per unit = {slope / per_sample:+.3f} ADC samples")
    torn = [r[0] for r in rows if r[2] >= 1.0]
    if torn:
        print(f"torn at {torn[0]}..{torn[-1]} -- the capture lost horizontal lock")
    return 0


if __name__ == "__main__":
    sys.exit(main())
