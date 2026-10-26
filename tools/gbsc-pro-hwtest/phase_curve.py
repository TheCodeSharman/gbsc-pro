#!/usr/bin/env python3
"""The ADC phase curve, and what moves it. Pass-through, where the phase bites.

    python3 phase_curve.py --host 192.168.88.108
    python3 phase_curve.py --host … --legs ADC_FLTR=0,1,2,3
    python3 phase_curve.py --host … --relatch 5
    python3 phase_curve.py --host … --pulse PLLAD_LAT,PLLAD_LEN,PLLAD_VCORST

**THE DRAW IS HELD UNLESS THE PLL IS RE-LATCHED.** Where the ADC PLL locks
re-rolls the optimum across the field, and that is the whole phenomenon -- so a
comparison of two register settings is only a comparison while nothing
re-latches between them. `/phase?adc=` does not re-latch, and neither does a
write by name, so `--legs` holds one draw across every setting and repeats the
first leg last as the control. `/sampleclock` DOES re-latch, which is what
`--relatch` uses to draw again in about six seconds rather than bouncing the
source for two and a half minutes.

**A LEG THAT CHANGES THE DIVIDER IS NOT COMPARABLE**, so `--legs` refuses one:
the divider moves and rescales the picture under a box found once, and it
rewrites `SP_RT_HS_SP`, whose formula is calibrated for a sampling density and
costs the grating far more than the phase does.

**WHAT A DRAW CHANGES IS THE CURVE'S OFFSET, NOT ITS SHAPE**, so the reading is
the best circular SHIFT against the previous curve rather than the optimum's
position: measured across thirteen draws, rotating cuts the difference to
1.4..6.6 from 4.7..24.4, where a control repeat sits at 0.7. An optimum on a
plateau several steps wide hops without the curve moving, so it is the noisier
of the two statements.

`--pulse` is what bisects the draw. It writes the complement of what a one-bit
field holds and writes it straight back -- a rising edge on a bit that rests
high, a reset pulse on one that rests low -- and reports the shift that
followed, so the parts of the PLL restart can be asked one at a time.

docs/investigations/the-adc-sampling-phase-is-a-lottery.md
"""

import argparse
import sys
import time
import urllib.request

import numpy as np

import wedge
from gbs_unit import read_fields, read_named, write_named
from hdmi_capture import BLACK, device, frames, luma

WIDTH, HEIGHT = 1920, 1080
CLIP, WARMUP, SETTLE = 12, 8, 0.8
PHASE_MAX = 31

# Writing one of these invalidates a held box, so a leg may not name one.
RESCALES = ("PLLAD_MD", "PLLAD_KS", "PLLAD_CKOS", "SP_RT_HS_SP")

WITNESS = ["PLLAD_MD", "SP_RT_HS_SP", "PA_SP_S", "STATUS_SYNC_PROC_HTOTAL",
           "STATUS_SYNC_PROC_VTOTAL", "DAC_RGBS_BYPS2DAC"]


def route(host, path):
    return urllib.request.urlopen(f"http://{host}{path}", timeout=10).read().decode()


def lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), (
        "the capture is entirely black. Ask the board before the connector: "
        "s0_46's resets, then s0_45 and s0_49. hdmi_capture.py")
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def sweep(host, dev, box):
    """Contrast at every phase of the field, the box held throughout."""
    curve = []
    for phase in range(PHASE_MAX + 1):
        route(host, f"/phase?adc={phase}")
        time.sleep(SETTLE)
        greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        lit = (greys[0].mean(axis=0) > BLACK).any()
        curve.append(wedge.score(greys, box)[0] if lit else 0.0)
    return curve


def shiftAgainst(curve, reference):
    """The circular shift that best aligns this curve to the reference, and what
    is left over at it. A shift near zero with a residual near the control's is
    a curve that did not move."""
    best = min((float(np.abs(np.roll(curve, -k) - reference).mean()), k)
               for k in range(len(reference)))
    residual, steps = best
    return (steps if steps <= len(reference) // 2 else steps - len(reference),
            residual)


def report(label, curve, extra="", csv=False, reference=None):
    peak, trough = max(curve), min(curve)
    moved = ""
    if reference is not None:
        steps, residual = shiftAgainst(np.array(curve), np.array(reference))
        moved = "  shift %+3d residual %5.2f" % (steps, residual)
    print("%-18s optimum %2d  peak %5.1f  trough %5.1f  swing %5.2fx  mean %5.1f%s%s"
          % (label, int(np.argmax(curve)), peak, trough,
             peak / max(trough, 0.01), float(np.mean(curve)), moved, extra),
          flush=True)
    if csv:
        print("curve,%s,%s" % (label, ",".join("%.1f" % v for v in curve)),
              flush=True)


def locate(dev):
    anchor = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
    return wedge.locate(anchor[0], lit_box(anchor[0]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--legs", default=None,
                        help="NAME=v1,v2,… -- one sweep per value, first repeated as control")
    parser.add_argument("--csv", action="store_true",
                        help="print each curve in full, so two can be compared "
                             "as shapes rather than by their optimum alone")
    parser.add_argument("--relatch", type=int, default=0,
                        help="draw again N times, reporting the optimum of each")
    parser.add_argument("--pulse", default=None,
                        help="NAME,NAME,… -- pulse each one-bit field in turn and "
                             "report the shift it caused, a no-op first as the control")
    args = parser.parse_args()

    held = read_fields(args.host, WITNESS)
    print("held:", held, flush=True)
    if held["DAC_RGBS_BYPS2DAC"] != 1:
        print("NOT in pass-through, where the scaling path's resampling leaves "
              "no phase response to measure -- /uc?x switches. "
              "docs/investigations/the-adc-sampling-phase-is-a-lottery.md")

    dev = device()
    box = locate(dev)
    print("wedge box", box, "(found once and held)", flush=True)

    try:
        if args.legs:
            name, _, values = args.legs.partition("=")
            if name in RESCALES:
                sys.exit(f"{name} rescales the picture under a held box, so a "
                         f"leg naming it measures the box and not the setting")
            keep = read_named(args.host, name)
            settings = [int(v) for v in values.split(",")]
            curves = []
            for setting in settings + [settings[0]]:
                write_named(args.host, name, setting)
                time.sleep(1.0)
                got = read_named(args.host, name)
                assert got == setting, f"{name} read back {got}, wanted {setting}"
                curves.append(sweep(args.host, dev, box))
                report(f"{name}={setting}", curves[-1],
                       "  CONTROL" if len(curves) > len(settings) else "",
                       csv=args.csv)
            write_named(args.host, name, keep)
            first, control = np.array(curves[0]), np.array(curves[-1])
            print("\ncontrol, first leg vs its repeat: r = %.3f  max |diff| %.1f"
                  % (np.corrcoef(first, control)[0, 1],
                     np.abs(first - control).max()))
            print("A leg differing from the first by no more than the control "
                  "does has not been shown to do anything.")
        else:
            report("as found", sweep(args.host, dev, box), csv=args.csv)

        previous = None
        for draw in range(args.relatch):
            route(args.host, f"/sampleclock?md={held['PLLAD_MD']}")
            time.sleep(8)
            after = read_fields(args.host, ["PLLAD_MD", "SP_RT_HS_SP"])
            curve = sweep(args.host, dev, box)
            report("after re-latch %d" % (draw + 1), curve,
                   "  origin %d" % after["SP_RT_HS_SP"], csv=args.csv,
                   reference=previous)
            previous = curve

        if args.pulse:
            previous = sweep(args.host, dev, box)
            report("before any pulse", previous, csv=args.csv)
            for name in ["none"] + args.pulse.split(","):
                if name in RESCALES:
                    sys.exit(f"{name} rescales the picture under a held box")
                if name != "none":
                    held_bit = read_named(args.host, name)
                    write_named(args.host, name, 0 if held_bit else 1)
                    write_named(args.host, name, held_bit)
                    time.sleep(2.0)
                curve = sweep(args.host, dev, box)
                report("pulsed %s" % name, curve, csv=args.csv,
                       reference=previous)
                previous = curve
            print("\nA pulse whose shift is no bigger than the none leg's has "
                  "not been shown to redraw the sampling phase.")
    finally:
        route(args.host, "/phase?clear")

    print("\nwitnesses now:", read_fields(args.host, WITNESS))


if __name__ == "__main__":
    main()
