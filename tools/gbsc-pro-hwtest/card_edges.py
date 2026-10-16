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
SINK's own window placement in it**, which is latched when the link acquires and
held until it acquires again, so a correct framing can still read a few columns
at the near edge and none at the far one.

**WHAT THE SINK KEYS ON IS NOT ESTABLISHED, AND IT IS NOT OUR BLANKING.**
Taking its origin from `VDS_DIS_HB_SP` is REFUTED -- do not reach for it -- and
the encoder tuning the near edge to the content it receives is PASS-THROUGH
only. `docs/known-issues.md`, `docs/investigations/
the-encoder-tunes-the-left-edge-in-pass-through.md`.

It is repeatable to the pixel across re-locks at one state, though the
sub-pixel landing inside the edge column does move: measured frozen, with every
output register identical either side of a `PAD_SYNC_OUT_ENZ` toggle, the
horizontal green ramp moved while the vertical held to a tenth. A STALE latch,
left from before the framing moved, reads differently again, so judge a margin
only after the link has re-acquired. The span is free of all of it, since the
two edges move together.

  both edges, full span   the window is the source's active window
  both edges, short span  the window is WIDER -- the source's own blanking is
                          on screen, by 1920 less the span
  one edge                the window is SHIFTED -- blanking at one end, picture
                          lost at the other. The margin at the end still on the
                          frame is what bounds the shift, the two ends moving
                          together
  no edge                 the window is NARROWER than the source's picture, by
                          an amount no feature in the frame bounds

**EVERY VERDICT CARRIES A SIZE**, `out_by`, the worst edge's distance from
flush in output pixels. A green column a pixel off the frame and one a hundred
pixels off are the same fault at two sizes, and only the size says which modes
are nearly right -- so it is reported whether the verdict passes or fails,
which is what keeps a widened allowance from hiding a drift.

The model this instrument used to assume, and the measurements that killed it:
`docs/investigations/the-transmitted-window-is-latched-from-our-blanking.md`.

**THE CARD'S OUTERMOST RING FLASHES YELLOW AND WHITE**, twice a second, and the
green frame is drawn over it in both phases. **ONE FRAME IS NOT ENOUGH TO SEE
IT**: in the phase where the ring beside it is yellow the green smears into it
and falls under any hue test, so a single capture reports the edge missing and
the window shifted. Read over a clip and take each column's GREENEST moment.
"""

import argparse
import collections
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

# What the INSTRUMENT allows at an edge, in output pixels. The green frame is
# one source pixel wide and lands on a fraction of an output one, so its
# captured edge is a column either way.
FLUSH = 1

# What the SOLVE lands apart from one run to the next, in output pixels. With
# no declared shape the picture fills the raster exactly, so rounding at either
# end moves the outermost green column a pixel or two and can take it off the
# frame altogether -- which is why one mode reads flush in one run and two
# pixels in at the near edge in the next.
SPREAD = 2

# What a framing is judged at, and the default both the sweep and the
# acceptance suite use, so there is one statement of what flush means. The
# magnitude is reported whatever this is, so widening it cannot hide a drift
# that is still growing.
ALLOWANCE = FLUSH + SPREAD

# The far edge additionally carries the width parity, which the HSCALE
# corruption workaround can leave a pixel of.
PARITY = 1


def greenest_along(clip, axis):
    """Each position's greenest moment across the clip, so the phase where the
    ring beside the frame is yellow cannot hide it."""
    greenest = np.zeros(clip.shape[1 + axis], np.float32)
    for rgb in clip:
        r, g, b = (rgb[:, :, i].astype(np.float32) for i in range(3))
        greenest = np.maximum(greenest,
                              (g - np.maximum(r, b)).mean(axis=1 - axis))
    return greenest


# WHETHER THE OUTERMOST PIXEL IS WHOLE, which a MARGIN cannot say. The margin
# is the first green run's index, so a border sitting wholly on row 0 and one
# half off the frame above it both report 0 -- and the acceptance is the card's
# frame being VISIBLE, not an index.
#
# The frame is one SOURCE pixel, several output pixels wide under magnification,
# so a partly-shown one reads as a fraction of a whole one. A lit position AT
# the frame's own edge is a border that continues past it; what it reads against
# the profile's peak is how much of it survived.
#
# Measured on `vga` into 1080p with the shape off: 320x256@50 shows 43% of a row
# at the bottom where one source pixel is 4.22 rows, and 640x512@50 shows 49%
# where it is 2.11 -- about 0.45 output rows in both, because the solve fills
# the raster and the last source pixel straddles the end of it.
# docs/known-issues.md
def edge_wholeness(greenest):
    """How much of the border survives at each end, as a fraction of a whole
    one. 1.0 is a border clear of the frame's edge; less is one running past
    it; 0.0 is no green at that edge at all."""
    peak = float(greenest.max())
    if peak <= GREEN_HUE:
        return (0.0, 0.0)
    return tuple(1.0 if at <= GREEN_HUE else round(float(at) / peak, 2)
                 for at in (greenest[0], greenest[-1]))


def green_runs(clip, axis):
    """Where the frame's green lies along `axis`, as (start, stop) runs."""
    greenest = greenest_along(clip, axis)
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


# WHAT AN EDGE OFF THE FRAME IS OUT BY, in output pixels, where the opposite
# edge is flush. The green column is simply not in the capture, so nothing in
# the frame sizes the overrun directly -- what sizes it is the margin at the
# edge still ON the frame, because a shift moves the two ends together. Gone at
# one end and flush at the other leaves only the width, and a width wrong by
# more than the allowance puts BOTH columns off, which is the verdict that
# bounds nothing and fails whatever the allowance is.
OFF_FRAME_LEAST = 1

# A frame's two edges are near the two ends of the picture. Green anywhere else
# is the card's own blocks, and two runs a few columns apart are one edge read
# twice -- both of which read as a frame spanning almost nothing.
QUARTER = 4

# `shown` is what the two margins leave, `margins` is each edge's own, `out_by`
# is the worst edge's distance from flush in output pixels, and `why` is None
# where every edge is inside the allowance.
#
# **`out_by` IS THE DISTANCE FROM THE GOAL, NOT THE AMOUNT BY WHICH A THRESHOLD
# WAS MISSED**, so it is reported whether the verdict passes or fails: raising
# the allowance to cover the pixel or two the solve lands apart between runs
# then cannot hide a drift that is still growing.
#
# None for `shown` or a `margins` entry is an edge off the frame; None for
# `out_by` is an error no feature in the frame bounds.
Judgement = collections.namedtuple("Judgement",
                                   "shown margins out_by why whole")

ENDS = ("near", "far")


def judge(clip, axis, allowance):
    """What the green frame says about the window on this axis."""
    runs = green_runs(clip, axis)
    whole = edge_wholeness(greenest_along(clip, axis))
    extent = clip.shape[1 + axis]
    near = [run for run in runs if run[1] < extent // QUARTER]
    far = [run for run in runs if run[0] > extent - extent // QUARTER]
    margins = (near[0][0] if near else None,
               extent - 1 - far[-1][1] if far else None)
    if not near and not far:
        return Judgement(None, margins, None,
                         "neither edge: the picture overruns both ends, and "
                         "nothing in the frame says by how much", whole)
    # Each edge on its own, because the goal is per-edge: the near edge and both
    # vertical edges are flush, and only the far edge carries the parity. A span
    # test passes a picture that is short at one end and over at the other.
    out_by, over = 0, []
    for end, margin, limit in zip(ENDS, margins,
                                  (allowance, allowance + PARITY)):
        from_flush = OFF_FRAME_LEAST if margin is None else margin
        out_by = max(out_by, from_flush)
        if from_flush > limit:
            over.append(f"the {end} edge is off the frame" if margin is None
                        else f"{margin} at the {end} edge")
    shown = None if None in margins else margins[0] + margins[1]
    if over:
        return Judgement(shown, margins, out_by,
                         f"out by {out_by}: " + ", ".join(over), whole)
    return Judgement(shown, margins, out_by, None, whole)


def reading(judged):
    """One axis' verdict as a report prints it: the span the two margins leave,
    then each edge's own, then the distance from flush. A dash is an edge the
    emitted frame does not carry, which bounds nothing on its own."""
    def px(value):
        return "-" if value is None else str(value)
    return (f"{px(judged.shown)} ({px(judged.margins[0])}|"
            f"{px(judged.margins[1])}) out by {px(judged.out_by)} "
            f"whole {judged.whole[0]:.2f}|{judged.whole[1]:.2f}")


def sweep_mode(host, dev, source, mode, allowance, keep_framing):
    reply = gbs_unit.mode_serv(source, f"MODE {mode}")
    if not reply or not reply.startswith("OK"):
        return None, f"source refused the mode: {reply!r}"
    if not gbs_unit.acquired_and_settled(host):
        return None, "never acquired, or never stopped re-solving"
    # AFTER the mode change, which repaints the source's default pattern.
    gbs_unit.mode_serv(source, "PATTERN CARD")
    if not keep_framing and gbs_unit.reset_framing(host) is None:
        return None, "the framing never reset"
    time.sleep(2.0)

    # A clip rather than a frame: the card flashes twice a second, so a single
    # capture can miss a green edge entirely. CLIP_FRAMES spans both phases.
    clip = hdmi_capture.frames(CLIP_FRAMES, dev)
    found = {name: judge(clip, axis, allowance)
             for name, axis in (("across", 1), ("down", 0))}
    whys = [f"{name}: {judged.why}" for name, judged in found.items()
            if judged.why]
    return found, "; ".join(whys)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", default=MODESERV)
    parser.add_argument("--modes", default=None,
                        help="comma separated, e.g. 'X800 Y600 C256 F60'; default is "
                             "every mode the monitor definition allows at 256 colours")
    parser.add_argument("--allowance", type=int, default=ALLOWANCE,
                        help="output pixels to tolerate at a flush edge "
                             f"(default {ALLOWANCE})")
    parser.add_argument("--keep-framing", action="store_true")
    args = parser.parse_args()

    source = args.modeserv
    if args.modes:
        modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    else:
        modes = gbs_unit.list_modes(source)
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
        read = "  ".join(f"{name} {reading(judged)}"
                         for name, judged in found.items())
        print(f"  {mode:24} {'FAIL' if why else 'ok  '}  {read}"
              + (f"   [{why}]" if why else ""))
        if why:
            failed.append(mode)

    print(f"\n{len(modes) - len(failed)} of {len(modes)} frame the picture")
    if failed:
        print("not framed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
