#!/usr/bin/env python3
"""Sweep both sampling phases jointly, on ONE acquisition.

    python3 tools/gbsc-pro-hwtest/phase_grid.py --host 192.168.88.108 --step 4

**EVERY EARLIER HUNT COMPARED TWO MARGINALS.** The chosen `PA_SP_S` against
the best `PA_ADC_S`, across acquisitions -- which can only find an additive
law, and confounds any effect of the sync phase with the re-roll between
acquisitions. Readings from two acquisitions put the best `PA_ADC_S` at 24
with `PA_SP_S` 20 and at 12 with `PA_SP_S` 0, which no single-variable
reading can attribute. A grid taken inside one acquisition can.

It answers three questions at once: whether the reachable PEAK depends on
`PA_SP_S` -- in which case the ceiling that varies between acquisitions is
the sync phase the engine happened to choose, and not a lottery at all --
whether the optimum tracks it and with what slope, and whether 0 is special.

**A SYNC-PHASE CHANGE BLANKS THE ENCODER BRIEFLY AND THE PICTURE COMES BACK.**
A run is not void for that: the capture is retaken until the frame is lit
again, and the retries are reported per point so a point that needed many is
visible rather than hidden. `STATUS_SYNC_PROC_VTOTAL` comes back with every
point too, because a sync phase can stop the count agreeing.

The box is found ONCE and held for the whole grid, for the reason
`adc_phase_sweep.py` states: re-finding it makes the instrument follow the
thing it measures.
"""

import argparse
import time
import urllib.request

import numpy as np

import resolve_watch
import wedge
from gbs_unit import read_named
from hdmi_capture import BLACK, device, frames, luma

WIDTH, HEIGHT = 1920, 1080
CLIP = 16
WARMUP = 8
SETTLE_S = 1.2
PHASE_MAX = 31

# How long the picture is given to come back after a sync-phase change, and how
# often it is asked. Measured: an ADC-only change needed none of this.
BLANK_TRIES = 12
BLANK_WAIT_S = 2.0


def route(host, path):
    return urllib.request.urlopen(f"http://{host}{path}", timeout=8).read().decode()


def lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    if not len(columns) or not len(rows):
        return None
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def lit_clip(dev):
    """A clip of a frame that is actually lit, and how many retakes it cost."""
    for attempt in range(BLANK_TRIES):
        greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP)).astype(np.float32)
        if lit_box(greys[-1]) is not None:
            return greys, attempt
        time.sleep(BLANK_WAIT_S)
    return None, BLANK_TRIES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--step", type=int, default=4)
    parser.add_argument("--passes", type=int, default=1)
    parser.add_argument("--sp", type=int, action="append", default=None,
                        help="score only these PA_SP_S values, repeatable -- "
                             "for a mode where two particular phases are the "
                             "question, such as the shear case's 13 and 16")
    args = parser.parse_args()

    dev = device()
    phases = list(range(0, PHASE_MAX + 1, args.step))
    syncPhases = args.sp if args.sp else phases

    # THE BOX IS LOCATED IN WHATEVER STATE THE ENGINE IS IN, not at a pinned
    # pair: a pin chosen for one mode can be in that mode's murky band, and a
    # box found on an aliased wedge lands on the wrong block.
    anchor, _ = lit_clip(dev)
    box = wedge.locate(anchor[0], lit_box(anchor[0]))
    print(f"PLLAD_MD {read_named(args.host, 'PLLAD_MD')}, wedge box {box}, "
          f"{len(phases)}x{len(phases)} grid x {args.passes}")

    taken = {}
    with resolve_watch.attached(args.host) as watch:
        try:
            for index in range(args.passes):
                for sp in syncPhases:
                    route(args.host, f"/phase?sp={sp}")
                    time.sleep(SETTLE_S)
                    for adc in phases:
                        route(args.host, f"/phase?adc={adc}")
                        time.sleep(SETTLE_S)
                        greys, retries = lit_clip(dev)
                        if greys is None:
                            taken.setdefault((sp, adc), []).append(None)
                            print(f"  sp {sp:2d} adc {adc:2d}  NO PICTURE")
                            continue
                        contrast, movement = wedge.score(greys, box)
                        count = read_named(args.host, "STATUS_SYNC_PROC_VTOTAL")
                        taken.setdefault((sp, adc), []).append(
                            (contrast, movement, count, retries,
                             lit_box(greys[0])[0]))
                print(f"  pass {index + 1} of {args.passes}, {watch.summary()}")
        finally:
            # Back to the engine rather than to a pin, so a later acquisition
            # is the firmware's own choice and not this run's leftover.
            route(args.host, "/phase?clear")

    print("\ncontrast, PA_SP_S down the side and PA_ADC_S across:")
    print("  sp\\adc " + "".join(f"{a:7d}" for a in phases) + "     peak  at")
    for sp in syncPhases:
        row = []
        for adc in phases:
            got = [r for r in taken[(sp, adc)] if r is not None]
            row.append(np.mean([r[0] for r in got]) if got else float("nan"))
        best = int(np.nanargmax(row))
        print(f"  {sp:5d}  " + "".join(f"{v:7.1f}" for v in row)
              + f"   {row[best]:6.1f}  {phases[best]:3d}")

    print("\ncount and retries where either was not nominal:")
    for (sp, adc), rows in sorted(taken.items()):
        for r in rows:
            if r is None:
                print(f"  sp {sp:2d} adc {adc:2d}  no picture")
            elif r[2] != 749 or r[3]:
                print(f"  sp {sp:2d} adc {adc:2d}  count {r[2]}  "
                      f"retries {r[3]}  lit left {r[4]}")

    peaks = []
    for sp in syncPhases:
        got = [np.mean([r[0] for r in taken[(sp, a)] if r is not None])
               for a in phases]
        peaks.append(max(v for v in got if not np.isnan(v)))
    print("\nreachable peak by PA_SP_S: "
          + ", ".join(f"{sp}:{p:.1f}" for sp, p in zip(syncPhases, peaks)))
    print(f"  spread {min(peaks):.1f} .. {max(peaks):.1f} "
          f"= {max(peaks)/max(min(peaks), 0.01):.2f}x. A peak that depends on "
          f"PA_SP_S means the ceiling varying between acquisitions is the sync "
          f"phase the engine chose, not a re-roll.")


if __name__ == "__main__":
    main()
