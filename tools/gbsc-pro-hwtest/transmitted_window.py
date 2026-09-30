#!/usr/bin/env python3
"""Measure the window the chain actually carries, per output mode.

    python3 tools/gbsc-pro-hwtest/transmitted_window.py --host 192.168.88.108
    ... --outputs 1080p,720p          just these
    ... --sources "X800 Y600 C256 F60,X800 Y600 C256 F75"
    ... --relock                      re-acquire the link at every step

The raster is what the SOURCE's field rate makes of the output mode's frame
height, so driving the source across its rates walks the raster with everything
else held -- which is what separates a delay in samples from a fraction of the
line.

`OutputMode::solve()` places the aperture at `sync + backPorch +
TransmittedWindowDelayPx`, spanning `horizontalTotal x carriedPx / totalPx`.
This asks the emitted frame where that window really is.

Each edge is found by walking one of our own blanking registers INTO the window
and extrapolating where the blanking lands back to zero. That position tracks
the register linearly while the register is inside what the chain carries, so
the crossing is the window's edge -- and it does not depend on where the picture
happens to sit, which is what makes it a measurement of the WINDOW rather than
of the framing.

**The blanking edge is found by DIFFERENCING against a reference frame, not by
looking for the first lit column.** The card's concentric bands put whole black
columns inside the picture, so a detector keyed on brightness runs past the
blanking edge to the next bright band and reads tens of pixels late -- which
looks like a jump in an otherwise straight line rather than like an artefact.
The columns that changed against the reference ARE the strip the register
blanked, whatever is drawn in them.

The slope is the second half of the measurement: the chain resamples our line
into the standard's active pixel count, so emitted pixels per raster unit says
how wide the window is independently of the two crossings agreeing.

Read off the USB capture rather than the panel. `docs/bench-output-capture.md`.
"""

import argparse
import sys
import time

import numpy as np

import gbs_unit
import hdmi_capture
import setfield

# A column counts as changed when its mean luma moved this far against the
# reference. Above the capture's own noise, which repeats to a tenth of a level.
MOVED = 6.0

# The register walks INTO the picture, so far enough to clear whatever source
# blanking the framing leaves at that edge and still stay inside the picture.
STEP = 20
POINTS = 12

# Whether the solve got the scale it ASKED for, which is what makes the aperture
# the thing the window can be compared against. Tv5725::Scale clamps to 342 and
# 1023, and an output mode with fewer active lines than the source needs a
# minification the part cannot express -- so the picture is cropped instead and
# no longer fills the aperture, while VDS_HSCALE sits nowhere near a bound.
#
# Asking the register is therefore not enough. `capture x Unity / scale` against
# the aperture's own width catches both, and says by how much.
SCALE_UNITY = 1024
FILLS_APERTURE_PX = 2.0


def columns(dev):
    return hdmi_capture.luma(hdmi_capture.frames(3, dev)[-1]).mean(axis=0)


# A changed run shorter than this is the card animating or the run-up past the
# end of the picture dithering, not a strip the register blanked.
RUN = 4

# How far a run's width may be from the step's own, as a fraction of it.
WIDTH_TOLERANCE = 0.4


def runs_of(mask):
    """Contiguous True runs of `mask`, as (start, stop) inclusive."""
    found, start = [], None
    for at, on in enumerate(mask):
        if on and start is None:
            start = at
        elif not on and start is not None:
            found.append((start, at - 1))
            start = None
    if start is not None:
        found.append((start, len(mask) - 1))
    return [run for run in found if run[1] - run[0] + 1 >= RUN]


def blanking_edge(current, previous, near, expected):
    """Where the register's blanking now reaches, in emitted columns.

    Against the PREVIOUS step rather than a fixed reference, so what changed is
    the one strip between the two blanking positions -- and the strip is picked
    by its WIDTH, which the step size says in advance. The card animates, so the
    frame carries changed columns that have nothing to do with the register, and
    neither "the outermost run" nor "the run nearest the edge" tells them apart:
    a flashing block can lie further out than the strip. Its width does.
    """
    width = current.size
    half = width // 2
    moved = np.abs(current - previous) > MOVED
    found = runs_of(moved[:half]) if near else \
            [(start + half, stop + half) for start, stop in runs_of(moved[half:])]
    if not found:
        return None
    strip = min(found, key=lambda run: abs(run[1] - run[0] + 1 - expected))
    if abs(strip[1] - strip[0] + 1 - expected) > expected * WIDTH_TOLERANCE:
        return None
    return int(strip[1] + 1) if near else int(width - strip[0])


# A source with its own border leaves black at the edge being walked, and
# blanking black changes nothing -- so the walk measures the source's framing
# instead of the window, and reads as a register that does nothing.
LIT = 24.0
FILL_ATTEMPTS = 4
FILL_SLACK = 8


def fill(host, dev):
    """Zoom the framing until picture reaches both ends of the emitted frame.

    The aperture is the raster's active window whatever the framing, so this
    moves what is measured THROUGH the window without moving the window.
    """
    for _ in range(FILL_ATTEMPTS):
        column = columns(dev)
        lit = np.where(column > LIT)[0]
        if lit.size == 0:
            return False
        left, right = int(lit[0]), int(column.size - 1 - lit[-1])
        if left <= FILL_SLACK and right <= FILL_SLACK:
            return True
        gbs_unit.get(host, f"/sc?I={left + right + 4 * FILL_SLACK}")
        time.sleep(1.5)
        gbs_unit.get(host, f"/sc?+={left + 2 * FILL_SLACK}")
        time.sleep(1.5)
    return False


def relock(host, regs):
    setfield.apply(host, "PAD_SYNC_OUT_ENZ", regs["PAD_SYNC_OUT_ENZ"], 1, False)
    time.sleep(0.4)
    setfield.apply(host, "PAD_SYNC_OUT_ENZ", regs["PAD_SYNC_OUT_ENZ"], 0, False)
    time.sleep(3.0)


def walk(host, dev, regs, field, first, step, points, near, do_relock, expected):
    """Register value against where its blanking lands, walking into the picture."""
    setfield.apply(host, field, regs[field], first, False)
    time.sleep(0.5)
    previous, taken = columns(dev), first
    seen = []
    for i in range(1, points):
        value = first + i * step if near else first - i * step
        setfield.apply(host, field, regs[field], value, False)
        time.sleep(0.5)
        if do_relock:
            relock(host, regs)
        current = columns(dev)
        at = blanking_edge(current, previous, near,
                           expected * abs(value - taken))
        if at is not None:
            seen.append((value, at))
            previous, taken = current, value
    setfield.apply(host, field, regs[field], first, False)
    return seen


# A point off the line by more than this is the strip having no content to
# change -- the card's black bands do it -- rather than the window moving.
OUTLIER_PX = 4.0


def crossing(seen):
    """Where the blanking reaches the frame's edge, and emitted px per unit."""
    if len(seen) < 3:
        return None, None, len(seen), 0.0
    x = np.array([value for value, _ in seen], float)
    y = np.array([at for _, at in seen], float)
    for _ in range(len(seen)):
        slope, intercept = np.polyfit(x, y, 1)
        off = np.abs(y - (slope * x + intercept))
        if off.max() <= OUTLIER_PX or x.size <= 3:
            break
        keep = off < off.max()
        x, y = x[keep], y[keep]
    residual = float(np.abs(y - (slope * x + intercept)).max())
    return -intercept / slope, abs(slope), x.size, residual


def measure(host, dev, output, step, points, do_relock):
    regs = setfield.load_map()
    # The engine's own solve is what this compares against, so it has to be the
    # one in force: a unit left frozen carries the previous source's raster and
    # every register reads self-consistent.
    gbs_unit.get(host, "/freeze?on=0")
    if not gbs_unit.acquired_and_settled(host):
        return None
    filled = fill(host, dev)
    # The capture the scale is fitted to, read in the same breath as the scale:
    # the two together say what the solve PRODUCED, and a solve that did not get
    # the scale it asked for produces something narrower than its own aperture.
    capture = (gbs_unit.get_json(host, "/geometry")[1] or {}).get("eh", 0)
    solved = gbs_unit.read_fields(host, ["VDS_DIS_HB_SP", "VDS_DIS_HB_ST",
                                         "VDS_HSYNC_RST", "VDS_HSCALE"])
    opens, closes = solved["VDS_DIS_HB_SP"], solved["VDS_DIS_HB_ST"]
    gbs_unit.get(host, "/freeze?on=1")
    # Emitted pixels per raster unit, near enough to size the strip a step
    # blanks: the chain resamples the aperture into the whole emitted frame.
    expected = float(columns(dev).size) / float(closes - opens)
    try:
        near = walk(host, dev, regs, "VDS_DIS_HB_SP", opens, step, points,
                    True, do_relock, expected)
        far = walk(host, dev, regs, "VDS_DIS_HB_ST", closes, step, points,
                   False, do_relock, expected)
    finally:
        gbs_unit.get(host, "/freeze?on=0")
    start, nearSlope, nearUsed, nearOff = crossing(near)
    stop, farSlope, farUsed, farOff = crossing(far)
    gbs_unit.get(host, "/sc?B")
    scale = solved["VDS_HSCALE"]
    produced = (float(capture) * SCALE_UNITY / scale) if scale else 0.0
    aperture = float(closes - opens)
    return dict(output=output, filled=filled, scale=scale, produced=produced,
                clamped=abs(produced - aperture) > FILLS_APERTURE_PX,
                total=solved["VDS_HSYNC_RST"] + 1,
                apertureStart=opens, apertureStop=closes,
                start=start, stop=stop, nearSlope=nearSlope, farSlope=farSlope,
                nearUsed=nearUsed, farUsed=farUsed, near=near, far=far,
                worst=max(nearOff, farOff))


def report(found, verbose):
    if found["start"] is None or found["stop"] is None:
        print(f"  {found['output']:7} raster {found['total']:5}   "
              f"NOT MEASURED -- the blanking never moved with the register "
              f"scale {found['scale']}, "
              f"({found['nearUsed']} near, {found['farUsed']} far points"
              f"{'' if found['filled'] else ', and the picture never filled the frame'})")
        return
    if found["clamped"]:
        aperture = found["apertureStop"] - found["apertureStart"]
        print(f"  {found['output']:7} raster {found['total']:5}   "
              f"EXCLUDED -- the scale is clamped: VDS_HSCALE {found['scale']} "
              f"produces {found['produced']:.1f} into an aperture of {aperture}")
        return
    width = found["stop"] - found["start"]
    aperture = found["apertureStop"] - found["apertureStart"]
    print(f"  {found['output']:7} raster {found['total']:5}   "
          f"aperture {found['apertureStart']:5} .. {found['apertureStop']:5} "
          f"({aperture:5})   "
          f"window {found['start']:8.2f} .. {found['stop']:8.2f} ({width:7.2f})   "
          f"start {found['start'] - found['apertureStart']:+7.2f}  "
          f"width {width - aperture:+7.2f}   "
          f"px/unit {found['nearSlope']:.4f} / {found['farSlope']:.4f}"
          f"   scale {found['scale']:4}   worst {found['worst']:.1f} px")
    if verbose:
        for name in ("near", "far"):
            print("      " + name + ": " +
                  "  ".join(f"{v}:{m}" for v, m in found[name]))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--outputs", default="1080p,1024p,960p,720p")
    parser.add_argument("--sources", default=None,
                        help="comma separated source modes to drive ModeServ to")
    parser.add_argument("--modeserv", default="192.168.88.10")
    parser.add_argument("--step", type=int, default=STEP)
    parser.add_argument("--points", type=int, default=POINTS)
    parser.add_argument("--relock", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    wanted = [name.strip() for name in args.outputs.split(",") if name.strip()]
    unknown = [name for name in wanted if name not in gbs_unit.OUTPUT_COMMANDS]
    if unknown:
        sys.exit(f"unknown output mode(s): {', '.join(unknown)}")

    dev = hdmi_capture.device()
    print(f"{args.host}  step {args.step}  {args.points} points"
          f"{'  re-locking at every step' if args.relock else ''}\n")
    for source in [m.strip() for m in (args.sources or "").split(",") if m.strip()] or [None]:
        if source is not None:
            reply = gbs_unit.mode_serv(args.modeserv, f"MODE {source}")
            if not reply or not reply.startswith("OK"):
                print(f"  {source:24} SKIP  source refused the mode: {reply!r}")
                continue
            # AFTER the mode change, which repaints the source's default pattern.
            gbs_unit.mode_serv(args.modeserv, "PATTERN CARD")
            print(f"  {source}")
            time.sleep(2.0)
        sweep(args, dev, wanted)
    return 0


def sweep(args, dev, wanted):
    for name in wanted:
        gbs_unit.get(args.host, "/uc?" + gbs_unit.OUTPUT_COMMANDS[name])
        time.sleep(3.0)
        if not gbs_unit.acquired_and_settled(args.host):
            print(f"  {name:7} SKIP  never acquired, or never stopped re-solving")
            continue
        found = measure(args.host, dev, name, args.step, args.points,
                        args.relock)
        if found is None:
            print(f"  {name:7} SKIP  never settled after unfreezing")
            continue
        report(found, args.verbose)


if __name__ == "__main__":
    sys.exit(main())
