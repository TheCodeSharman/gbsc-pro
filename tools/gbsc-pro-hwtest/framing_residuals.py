#!/usr/bin/env python3
"""Where a sweep's modes landed against their own design, in capture units.

    python3 tools/gbsc-pro-hwtest/framing_residuals.py \
        tools/gbsc-pro-hwtest/sweeps/framing-20261022T133444Z.jsonl.gz

**IT READS THE BLACK COUNTED AT THE EMITTED EDGES AND NOTHING MODELLED.** A
sweep record carries a decomposition as well, and that decomposition's absolute
terms rest on the write-start constants and on where the encoder opens its
window -- so `dCapture` is an attribution and not a one-pixel measurement, and
it read -4.0 source px at 320x256@50 where the emitted frame said -0.7. What is
used here is `black_cols`, `geometry` and `emitted`.

**THE UNIT IS THE CAPTURE UNIT**, because that is the grid the engine places
on: a residual in output pixels mixes the placement with the magnification, and
one output pixel is a different fraction of a granule on every mode. The
conversion is `card_edges.capture_unit_px`, the window's own extent against the
frame it was magnified onto, and the design it is measured against is
`expected_framing`'s -- so neither the match rule nor the unit has a second
owner here.

**ONLY A MODE WHOSE MATCHED ROW IS THE RASTER THE MACHINE EMITS MEASURES THE
ENGINE.** Where two authorities state one resolution the standard answers, and
the mode file's own border is then off the frame by design; that offset is
known per mode and taken off before pooling, but the edge it clips carries no
border to read, so those modes are reported and pooled separately.
`../../docs/investigations/full-screen-framing-on-the-vesa-modes.md`
"""

import argparse
import gzip
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_edges
import expected_framing
import mdf_modes

# ACROSS is axis 1 and DOWN is axis 0, the order card_edges judges in.
EDGE_AXIS = {"left": 1, "right": 1, "top": 0, "bottom": 0}
EDGES = tuple(EDGE_AXIS)
AXIS_EDGES = {"across": ("left", "right"), "down": ("top", "bottom")}

# What counts as the engine having answered with the mode's own row, in the
# mode's own pixels and lines. Below this the two authorities agree and above
# it one of them is answering for the other.
OWN_ROW = 1.0


def records(path):
    """A sweep file's records, gzipped or not."""
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8") as sweep:
        for line in sweep:
            if line.strip():
                yield json.loads(line)


def residuals(record, entry):
    """How far each emitted edge is from what the design puts there, in capture
    units, plus the displacement that leaves in the source's own pixels.

    None for a record the sweep never judged, and for a mode the monitor
    definition states no timings for -- there the design is unknown and a
    residual would be against zero.
    """
    black = record.get("black_cols")
    if black is None or entry is None:
        return None
    geometry, emitted = record["geometry"], tuple(record["emitted"])
    timing = expected_framing.stated(entry)
    off = expected_framing.offsets(entry, geometry)
    want = expected_framing.margins(entry, geometry, emitted)
    if timing is None or off is None or want is None:
        return None
    # The design's own black at each edge. Negative is an edge it clips, where
    # there is no border on the frame to measure from.
    design = {"left": max(0.0, want["across"][0]),
              "right": max(0.0, want["across"][1]),
              "top": max(0.0, want["down"][0]),
              "bottom": max(0.0, want["down"][1])}
    units = {edge: ((black[edge] - design[edge])
                    / card_edges.capture_unit_px(geometry, emitted, axis))
             for edge, axis in EDGE_AXIS.items()}
    across = emitted[0] / float(timing["active_px"])
    down = emitted[1] / float(timing["active_ln"])
    return dict(mode=record["source"], emitted=emitted, black=black,
                design=design, offsets=off, units=units,
                # The window's own displacement: what the two margins say it
                # moved, less what the design moved it. Each margin carries the
                # sink's placement and they carry the same one, so differencing
                # them leaves the engine's.
                across=(black["right"] - black["left"]) / across
                       - (off["right"] + off["left"]) / 2.0,
                down=(black["bottom"] - black["top"]) / down
                     - (off["top"] + off["bottom"]) / 2.0)


def on_its_own_row(found, axis):
    """Whether the engine answered this AXIS from the raster the machine is
    emitting, which is what makes its residual a measurement of the engine.

    **PER AXIS, BECAUSE THE TWO AUTHORITIES CAN DISAGREE ON ONE OF THEM.** A
    mode the standard answers horizontally may state the same vertical window
    in both rows -- 1024x768@60 is 71 source pixels out across and half a line
    down -- so pooling both axes on one verdict throws away every such mode's
    good axis.
    """
    return all(abs(found["offsets"][edge]) < OWN_ROW
               for edge in AXIS_EDGES[axis])


def spread(values):
    """mean, sd and range, as a line."""
    array = np.array(values)
    return (f"{len(array):3} {array.mean():+6.2f} sd {array.std():5.2f} "
            f"[{array.min():+6.2f},{array.max():+6.2f}]")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sweeps", nargs="+")
    parser.add_argument("--mdf", default=expected_framing.MDF,
                        help="the monitor definition the SOURCE was running")
    args = parser.parse_args()

    entries = mdf_modes.parse(args.mdf)
    found = []
    for path in args.sweeps:
        for record in records(path):
            entry = expected_framing.entry_for(entries, record.get("source", ""))
            one = residuals(record, entry)
            if one is not None:
                found.append(one)
    if not found:
        sys.exit("no judged records in those sweeps")

    print(f"{'mode':22} {'emitted':10} "
          + " ".join(f"{edge:>7}" for edge in EDGES)
          + "   across    down   (units from the design, then source px)")
    for one in found:
        other = [axis for axis in AXIS_EDGES if not on_its_own_row(one, axis)]
        print(f"{one['mode']:22} {one['emitted'][0]}x{one['emitted'][1]:<5} "
              + " ".join(f"{one['units'][edge]:+7.2f}" for edge in EDGES)
              + f"  {one['across']:+7.2f} {one['down']:+7.2f}"
              + (f"   {' and '.join(other)} answered from another row"
                 if other else ""))

    print()
    for axis, edges in AXIS_EDGES.items():
        own = [one for one in found if on_its_own_row(one, axis)]
        print(f"  {axis:7} {len(own):3} of {len(found)} states on their own row")
        print(f"  {axis:7} edges     " + spread([one["units"][edge]
                                                 for one in own for edge in edges]))
        print(f"  {axis:7} displaced " + spread([one[axis] for one in own])
              + "  source px")
    return 0


if __name__ == "__main__":
    sys.exit(main())
