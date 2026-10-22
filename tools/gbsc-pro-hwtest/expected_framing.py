#!/usr/bin/env python3
"""What the engine's own placement costs each edge of the emitted frame.

    python3 tools/gbsc-pro-hwtest/expected_framing.py --host <ip> --mdf <file> \
        --mode "X640 Y480 C256 F60"

A source running a published raster is placed from that raster's stated active
window, and where TWO authorities state the same resolution the standard keeps
priority -- so a machine emitting a mode file's own variant of a standard mode
is framed where the standard puts it. The difference is black active video on
one side of the picture and porch on the other, electrically identical on the
wire, so no measurement on this chip can separate them and the offset is BY
DESIGN. ../../docs/bench-sources.md, ../../CLAUDE.md.

**NOTHING HERE RE-RUNS THE MATCH.** /geometry states the proportions the engine
PLACED -- poh, peh, pov and pev, in ten-thousandths of the line and the frame,
counted the way a video standard counts them -- so the offset is the difference
between a measurement of the engine and honest arithmetic on the mode file. A
second copy of the lookup is what let a mirror of the rule sit on a superseded
version through a whole firmware change without failing.

Measured over 37 states of one sweep, the engine's placed proportions agree with
the mode file's own picture area to 0.2 of a pixel on every mode whose matched
row IS its own raster, which is what makes the difference readable as the cost of
answering with another row.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit
import mdf_modes

# The bench's own, as MODESERV is in the tools beside this.
MDF = "/home/msharman/Projects/RiscPc/tools/video-source/RetroScaler-Acorn.mdf"


def entry_for(entries, mode):
    """The mode file entry `MODE` picks for this request: X and Y exactly, the
    field rate nearest. None where the file states no such resolution."""
    want = dict(re.findall(r"([XYF])(\d+)", mode))
    if "X" not in want or "Y" not in want:
        return None
    best = None
    for entry in entries:
        if entry.get("x_res") != want["X"] or entry.get("y_res") != want["Y"]:
            continue
        timing = stated(entry)
        if timing is None:
            continue
        off = abs(timing["field_rate"] - float(want.get("F", 0)))
        if best is None or off < best[0]:
            best = (off, entry)
    return best[1] if best else None


def stated(entry):
    """The mode file entry's own raster: where its picture sits and how far it
    runs, as fractions of the line and the frame counted from each pulse's
    leading edge, with the totals and the counts beside them.

    h_timings and v_timings are sync, back porch, border, display, border,
    front porch. The card's green frame is drawn on the FRAMEBUFFER, so the
    picture starts after the border and the leading run is sync + porch +
    border -- which is the same quantity a standard states as its own active
    start, the split between porch and border being invisible on the wire.
    """
    try:
        h = [int(v) for v in entry["h_timings"].split(",")]
        v = [int(v) for v in entry["v_timings"].split(",")]
        clock = int(entry["pixel_rate"]) * 1000.0
    except (KeyError, ValueError):
        return None
    if len(h) < 6 or len(v) < 6 or not sum(h) or not sum(v):
        return None
    h_total, v_total = sum(h), sum(v)
    return dict(h_total=h_total, v_total=v_total,
                active_px=h[3], active_ln=v[3],
                h_start=(h[0] + h[1] + h[2]) / h_total, h_extent=h[3] / h_total,
                v_start=(v[0] + v[1] + v[2]) / v_total, v_extent=v[3] / v_total,
                field_rate=clock / h_total / v_total)


def placed(geometry):
    """The proportions the engine placed, as fractions. /geometry reports them
    in ten-thousandths, the ESP's printf having no %f."""
    return dict(h_start=geometry["poh"] / 10000.0, h_extent=geometry["peh"] / 10000.0,
                v_start=geometry["pov"] / 10000.0, v_extent=geometry["pev"] / 10000.0)


def offsets(entry, geometry):
    """Where the engine's window opens and closes against the mode's own
    picture, in the mode's own pixels and lines.

    Positive at a NEAR edge: the window opens AFTER the picture starts, so that
    much of the picture is never captured and its outermost pixel is off the
    emitted frame. Positive at a FAR edge: the window closes after the picture
    ends, so that much of the source's own blanking is shown. Zero on an edge
    where the engine answered with the mode's own raster.
    """
    timing = stated(entry)
    if timing is None:
        return None
    put = placed(geometry)
    return dict(
        left=(put["h_start"] - timing["h_start"]) * timing["h_total"],
        right=((put["h_start"] + put["h_extent"])
               - (timing["h_start"] + timing["h_extent"])) * timing["h_total"],
        top=(put["v_start"] - timing["v_start"]) * timing["v_total"],
        bottom=((put["v_start"] + put["v_extent"])
                - (timing["v_start"] + timing["v_extent"])) * timing["v_total"])


def margins(entry, geometry, emitted):
    """What the design puts at each edge of the emitted frame, in its pixels.

    One signed figure per edge: POSITIVE is black the design shows, NEGATIVE is
    how much of the outermost source pixel the design does not capture. `emitted`
    is the frame's (width, height), which at a default framing IS the picture --
    so one source pixel is the frame over the mode's own active count and no
    register read can improve on it.

    Returned per axis as (near, far), the order card_edges judges in.
    """
    timing, off = stated(entry), offsets(entry, geometry)
    if timing is None or off is None:
        return None
    across = emitted[0] / timing["active_px"]
    down = emitted[1] / timing["active_ln"]
    return dict(across=(-off["left"] * across, off["right"] * across),
                down=(-off["top"] * down, off["bottom"] * down))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--mdf", default=MDF)
    parser.add_argument("--mode", required=True,
                        help="the mode the source is RUNNING, as MODE states it")
    parser.add_argument("--emitted", default="1920x1080")
    args = parser.parse_args()

    entries = mdf_modes.parse(args.mdf)
    entry = entry_for(entries, args.mode)
    if entry is None:
        sys.exit(f"{args.mdf} states no {args.mode}")
    geometry = gbs_unit.get_json(args.host, "/geometry")[1]
    emitted = tuple(int(v) for v in args.emitted.split("x"))
    off = offsets(entry, geometry)
    want = margins(entry, geometry, emitted)
    print(f"{args.mode}  placed {placed(geometry)}")
    print("  offsets, the mode's own px/lines: "
          + "  ".join(f"{k} {v:+.2f}" for k, v in off.items()))
    print(f"  emitted px at each edge: across {want['across'][0]:+.1f} "
          f"{want['across'][1]:+.1f}   down {want['down'][0]:+.1f} {want['down'][1]:+.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
