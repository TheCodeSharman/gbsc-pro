#!/usr/bin/env python3
"""Sweep the source's modes and judge the DEFAULT framing against the card.

    python3 tools/gbsc-pro-hwtest/card_edges.py --host 192.168.88.108
    ... --modes "X800 Y600 C256 F60,X1024 Y768 C256 F60"   just these
    ... --keep-framing                        judge what is stored, reset nothing

`PATTERN CARD` draws a one-pixel green frame on the source's outermost pixels
(`PROCframe` in PatLib), so it marks exactly where the source's active video
begins and ends.

**THE GOAL, WHICH IS WHAT THIS ASSERTS.** A default framing puts the source's
outermost pixel on the emitted frame's outermost pixel:

    left    the green column is flush, column 0
    top     the green row is flush, row 0
    bottom  the green row is flush, the last row
    right   almost flush -- ONE pixel of slack, for the width parity the
            HSCALE corruption workaround carries

`docs/investigations/full-screen-framing-on-the-vesa-modes.md` is the goal and
what has been measured against it.

Read off the USB capture rather than the panel, so an edge is a column index
and not a judgement. `docs/bench-output-capture.md`.

Both the span and each edge's own margin are reported. **The margin carries the
encoder in it**: the transmitted window's start is latched from `VDS_DIS_HB_SP`
at link-up and lands a little before it, so a correct framing still reads a few
columns at the near edge and none at the far one. It is repeatable -- ten
re-locks at one state give the same margins to the pixel -- but a STALE latch,
left from before the framing moved, reads differently, so judge a margin only
after the link has re-acquired. The span is free of both, since the two edges
move together.

  both edges, full span   the window is the source's active window
  both edges, short span  the window is WIDER -- the source's own blanking is
                          on screen, by 1920 less the span
  one edge                the window is SHIFTED -- blanking at one end, picture
                          lost at the other
  no edge                 the window is NARROWER than the source's picture

`docs/investigations/the-transmitted-window-is-latched-from-our-blanking.md`.

**THE CARD'S OUTERMOST RING FLASHES YELLOW AND WHITE**, twice a second, and the
green frame is drawn over it in both phases. **ONE FRAME IS NOT ENOUGH TO SEE
IT**: in the phase where the ring beside it is yellow the green smears into it
and falls under any hue test, so a single capture reports the edge missing and
the window shifted. Read over a clip and take each column's GREENEST moment.
"""

import argparse
import sys
import time

import numpy as np

import gbs_unit
import hdmi_capture

MODESERV = "192.168.88.10"

# Green dominance rather than brightness: the frame is one source pixel wide and
# the scaler interpolates its edges toward the ring beside it, so its captured
# brightness varies while its hue does not.
#
# The MEAN of it down the line, never a count of how many rows are green enough.
# The frame is one source pixel wide, so at the edges it lands on a fraction of
# an output pixel and blends with whatever the ring beside it is doing -- dark
# where the castellation is black, pale where it is white. Measured at
# 800x600@60 the left column cleared a per-row test on 70 rows of 1080 while
# the right cleared it on all of them, so a row count reports the left edge
# missing and the window shifted when both edges are on screen.
GREEN_HUE = 20.0

# A green edge smeared into the ring beside it can fall under the hue test for a
# column or two, so runs this close together are one edge.
JOIN = 8

# Over half a second at 30 fps, so both phases of the ring's flash are in it.
CLIP_FRAMES = 40

# What "flush" allows, in output pixels. The green frame is one source pixel
# wide and lands on a fraction of an output one, so its captured edge is a
# column either way.
FLUSH = 1

# The far edge additionally carries the width parity, which the HSCALE
# corruption workaround can leave a pixel of.
PARITY = 1


def green_runs(clip, axis):
    """Where the frame's green lies along `axis`, as (start, stop) runs.

    Each position's greenest moment across the clip, so the phase where the
    ring beside the frame is yellow cannot hide it.
    """
    greenest = np.zeros(clip.shape[1 + axis], np.float32)
    for rgb in clip:
        r, g, b = (rgb[:, :, i].astype(np.float32) for i in range(3))
        greenest = np.maximum(greenest,
                              (g - np.maximum(r, b)).mean(axis=1 - axis))
    found = np.where(greenest > GREEN_HUE)[0]
    runs, start = [], None
    for i, at in enumerate(found):
        if start is None:
            start = at
        elif at > found[i - 1] + JOIN:
            runs.append((int(start), int(found[i - 1])))
            start = at
    if start is not None:
        runs.append((int(start), int(found[-1])))
    return runs


# A frame's two edges are near the two ends of the picture. Green anywhere else
# is the card's own blocks, and two runs a few columns apart are one edge read
# twice -- both of which read as a frame spanning almost nothing.
QUARTER = 4


def judge(clip, axis, allowance):
    """What the green frame says about the window on this axis."""
    runs = green_runs(clip, axis)
    extent = clip.shape[1 + axis]
    near = [run for run in runs if run[1] < extent // QUARTER]
    far = [run for run in runs if run[0] > extent - extent // QUARTER]
    if not near and not far:
        return None, None, "neither edge: the window is narrower than the picture"
    if not near or not far:
        return None, None, "one edge: the window is shifted off the picture"
    margins = (near[0][0], extent - 1 - far[-1][1])
    shown = extent - (far[-1][1] - near[0][0] + 1)
    # Each edge on its own, because the goal is per-edge: the near edge and both
    # vertical edges are flush, and only the far edge carries the parity. A span
    # test passes a picture that is short at one end and over at the other.
    over = []
    if margins[0] > allowance:
        over.append(f"{margins[0]} at the near edge")
    if margins[1] > allowance + PARITY:
        over.append(f"{margins[1]} at the far edge")
    if over:
        return shown, margins, "not flush: " + ", ".join(over)
    return shown, margins, None


def settled(host, limit_s=60.0, holds=3):
    """Wait for the engine to acquire the mode and stop re-solving.

    A mode change does not clear `state` the instant the source leaves, so
    acquisition alone is not evidence the NEW mode is what was solved: this
    waits for the reported capture to hold still as well.
    """
    started, last, held = time.monotonic(), None, 0
    while time.monotonic() - started < limit_s:
        got = gbs_unit.get_json(host, "/geometry")[1]
        if got and got.get("state") == "acquired":
            now = (got.get("ch"), got.get("cv"), got.get("lineRateHz"))
            held = held + 1 if now == last else 0
            last = now
            if held >= holds:
                return True
        else:
            held, last = 0, None
        time.sleep(1.0)
    return False


def sweep_mode(host, dev, source, mode, allowance, keep_framing):
    reply = gbs_unit.mode_serv(source, f"MODE {mode}")
    if not reply or not reply.startswith("OK"):
        return None, f"source refused the mode: {reply!r}"
    if not settled(host):
        return None, "never acquired, or never stopped re-solving"
    # AFTER the mode change, which repaints the source's default pattern.
    gbs_unit.mode_serv(source, "PATTERN CARD")
    if not keep_framing and gbs_unit.reset_framing(host) is None:
        return None, "the framing never reset"
    time.sleep(2.0)

    # A clip rather than a frame: the card flashes twice a second, so a single
    # capture can miss a green edge entirely. CLIP_FRAMES spans both phases.
    clip = hdmi_capture.frames(CLIP_FRAMES, dev)
    found, whys = {}, []
    for name, axis in (("across", 1), ("down", 0)):
        shown, margins, why = judge(clip, axis, allowance)
        found[name] = (shown, margins)
        if why:
            whys.append(f"{name}: {why}")
    return found, "; ".join(whys)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", default=MODESERV)
    parser.add_argument("--modes", default=None,
                        help="comma separated, e.g. 'X800 Y600 C256 F60'; default is "
                             "every mode the monitor definition allows at 256 colours")
    parser.add_argument("--allowance", type=int, default=FLUSH,
                        help=f"output pixels to tolerate at a flush edge (default {FLUSH})")
    parser.add_argument("--keep-framing", action="store_true")
    args = parser.parse_args()

    source = args.modeserv
    if args.modes:
        modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    else:
        listing = gbs_unit.mode_serv(source, "MODES", timeout=15) or ""
        seen, modes = set(), []
        for line in listing.splitlines():
            parts = line.split()
            if len(parts) == 4 and parts[2] == "C256":
                key = (parts[0], parts[1], parts[3])
                if key not in seen:
                    seen.add(key)
                    modes.append(" ".join(parts))
    if not modes:
        sys.exit("no modes to sweep -- is ModeServ answering?")

    dev = hdmi_capture.device()
    print(f"{len(modes)} modes, {args.host}, flush within {args.allowance} px "
          f"({args.allowance + PARITY} at the far edge)\n")
    failed = []
    for mode in modes:
        found, why = sweep_mode(args.host, dev, source, mode, args.allowance,
                                args.keep_framing)
        if found is None:
            print(f"  {mode:24} SKIP  {why}")
            continue
        shown = "  ".join(
            f"{k} {v[0]} ({v[1][0]}|{v[1][1]})" if v[0] is not None else f"{k} -"
            for k, v in found.items())
        print(f"  {mode:24} {'FAIL' if why else 'ok  '}  {shown}"
              + (f"   [{why}]" if why else ""))
        if why:
            failed.append(mode)

    print(f"\n{len(modes) - len(failed)} of {len(modes)} frame the picture")
    if failed:
        print("not framed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
