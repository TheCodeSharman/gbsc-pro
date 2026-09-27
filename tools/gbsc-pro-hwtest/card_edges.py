#!/usr/bin/env python3
"""Sweep the source's modes and judge the DEFAULT framing against the card.

    python3 tools/gbsc-pro-hwtest/card_edges.py --host 192.168.88.108
    ... --modes "X800 Y600 C256 F60,X1024 Y768 C256 F60"   just these
    ... --keep-framing                        judge what is stored, reset nothing

`PATTERN CARD` draws a one-pixel green frame on the source's outermost pixels
(`PROCframe` in PatLib), so it marks exactly where the source's active video
begins and ends. A default framing is correct when the capture window is the
source's active window: both green edges land on the emitted frame and nothing
outside them is shown.

Read off the USB capture rather than the panel, so an edge is a column index
and not a judgement. `docs/bench-output-capture.md`.

**WHERE THE PICTURE SITS IN THE EMITTED FRAME IS LATCHED AT LINK-UP AND IS NOT
THE BOARD'S.** Two acquisitions of the same registers differ by tens of columns,
so "how far the green frame sits from the left edge" measures the encoder as
much as the engine. What survives that is which green edges are present and how
far apart they are, and those are what this judges:

  both edges, full span   the window is the source's active window
  both edges, short span  the window is WIDER -- the source's own blanking is
                          on screen, by 1920 less the span
  one edge                the window is SHIFTED -- blanking at one end, picture
                          lost at the other
  no edge                 the window is NARROWER than the source's picture

`docs/investigations/the-default-capture-window-opens-before-the-picture.md`.

**THE CARD'S OUTERMOST RING FLASHES YELLOW AND WHITE**, twice a second. The
green frame is drawn over it in both phases and does not flash, which is why it
is what this reads.
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
GREEN_OVER = 25
GREEN_FLOOR = 45

# A green edge smeared into the ring beside it can fall under the hue test for a
# column or two, so runs this close together are one edge.
JOIN = 8


def green_runs(rgb, axis):
    """Where the frame's green lies along `axis`, as (start, stop) runs."""
    r, g, b = (rgb[:, :, i].astype(np.int16) for i in range(3))
    mask = (g > r + GREEN_OVER) & (g > b + GREEN_OVER) & (g > GREEN_FLOOR)
    across = mask.shape[1 - axis]
    found = np.where(mask.sum(axis=1 - axis) > across // 2)[0]
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


def judge(rgb, axis, allowance):
    """What the green frame says about the window on this axis."""
    runs = green_runs(rgb, axis)
    extent = rgb.shape[0] if axis == 0 else rgb.shape[1]
    near = [run for run in runs if run[1] < extent // QUARTER]
    far = [run for run in runs if run[0] > extent - extent // QUARTER]
    if not near and not far:
        return None, "neither edge: the window is narrower than the picture"
    if not near or not far:
        return None, "one edge: the window is shifted off the picture"
    shown = extent - (far[-1][1] - near[0][0] + 1)
    if shown > allowance:
        return shown, f"{shown} of the source's own blanking is on screen"
    return shown, None


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

    rgb = hdmi_capture.frames(3, dev)[-1]
    found, whys = {}, []
    for name, axis in (("across", 1), ("down", 0)):
        shown, why = judge(rgb, axis, allowance)
        found[name] = shown
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
    parser.add_argument("--allowance", type=int, default=4,
                        help="output pixels of source blanking to tolerate (default 4)")
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
    print(f"{len(modes)} modes, {args.host}, {args.allowance} px of blanking "
          f"allowed\n")
    failed = []
    for mode in modes:
        found, why = sweep_mode(args.host, dev, source, mode, args.allowance,
                                args.keep_framing)
        if found is None:
            print(f"  {mode:24} SKIP  {why}")
            continue
        shown = "  ".join(f"{k} {v}" if v is not None else f"{k} -"
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
