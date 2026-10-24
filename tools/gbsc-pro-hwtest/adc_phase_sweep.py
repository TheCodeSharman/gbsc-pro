#!/usr/bin/env python3
"""Walk the ADC's sampling phase and score what it costs the picture.

    python3 adc_phase_sweep.py --host 192.168.88.108 --modeserv 192.168.88.10 \
        --mode "MODE X1280 Y720 C256 F60" --passes 3

`PA_ADC_S` is where in the ADC clock the converter takes its sample. Below two
ADC samples per source pixel the card's finest grating sits above the
sampler's Nyquist, so the phase decides how much of it survives -- and nothing
else on the card is near enough the limit to show it.

**NOTHING SEARCHES IT.** `Adc::acquirePhase()` sets it to the middle of its
five-bit field, which is not a sampling instant, and only the sync processor's
phase is walked. This is the bench instrument for asking what a search would
be worth and where it should land.

**THE WEDGE IS NOT THE VERDICT.** It is sustained periodic content at Nyquist
and beats rather than softens, so a swing there is not a picture-quality
claim. `sharpness.py` scores the rest of the lit picture off the SAME clips --
grid lines, castellations, bar edges -- and the vertical gradient is the
control, the phase being horizontal. A picture that moves like the control
while the wedge moves 3x is a phase that costs nothing a viewer sees.

**PASSES ARE INTERLEAVED, NOT REPEATED.** One reading per phase is noise: a
phase-versus-origin probe once showed an optimum invert and it did not survive
repeats. Walking the whole field `passes` times and pairing readings by phase
puts any drift across every phase instead of into the phases measured late.

**IT GOES THROUGH `/phase?adc=`, NOT THROUGH THE REGISTER.** `Adc` HOLDS the
phase rather than reading it back, so a poked `PA_ADC_S` leaves the held value
a lie and the next solve applies the stale one -- and the adjuster loads the
register only on a rising edge of `PA_ADC_LAT` in any case, so a bare write
reads back the new phase while the converter still samples at the old. The
route holds the value against the engine's own choice, so the walk needs no
freeze and the sync processor's search still runs.

`/phase?clear` gives both phases back to the engine, and this tool does it on
the way out whatever happened.

**AND IT CERTIFIES THE RUN.** A steady source is re-solved on an intermittent
13 s cadence, and holding both phases does not stop the arm taking the sync pad
away or the divider being re-installed -- so a walk taken across one compares
two states and the drift reads as the phase's own instability. `resolve_watch`
holds the console for the walk and every table below is reported with what
landed on it. A run that is not `console quiet` is not a measurement.
docs/known-issues.md
"""

import argparse
import socket
import time

import numpy as np

import resolve_watch
import sharpness
import wedge
from gbs_unit import read_named
from hdmi_capture import BLACK, device, frames, luma

WIDTH, HEIGHT = 1920, 1080

# Frames per reading. The link is not re-locking between phases, so this is a
# noise average rather than a wait: `movement` is a standard deviation over
# exactly these.
CLIP = 16
WARMUP = 8

# How long the queued phase is given to reach the converter. /phase is deferred
# to loop() like every command route, so a capture taken immediately is the
# previous phase's.
SETTLE_S = 1.0

PHASE_MAX = 31


def modeserv(host, command):
    link = socket.create_connection((host, 6502), timeout=10)
    link.sendall((command + "\n").encode())
    out = b""
    while True:
        chunk = link.recv(4096)
        if not chunk:
            break
        out += chunk
    link.close()
    return out.decode(errors="replace").strip()


def route(host, path):
    import urllib.request
    return urllib.request.urlopen(f"http://{host}{path}", timeout=8).read().decode()


def pin_sync_phase(host, phase):
    """Hold `PA_SP_S` so the walk is not also walking the sync processor's own.

    The engine searches that phase per solve and the ADC's is never searched at
    all, so an ADC walk taken while it re-rolls scores the two together -- a
    probe that sampled it at 16, 31, 8 and 24 put three of four readings
    outside the band where the count is stable, and read the swamping as the
    ADC phase doing nothing.
    """
    route(host, f"/phase?sp={phase}")
    time.sleep(SETTLE_S)


def apply_phase(host, phase):
    """Hold the phase against the engine's own choice.

    Queued for loop(), so a 200 means understood rather than applied: the
    settle below is what makes the next capture that phase's.
    """
    route(host, f"/phase?adc={phase}")
    time.sleep(SETTLE_S)


def lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), (
        "the capture is entirely black -- ask the board before the connector, "
        "hdmi_capture.py")
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def reading(dev, box):
    """One clip's readings: the wedge's, the picture's, and where it sits.

    **THE WEDGE AND THE PICTURE ARE SCORED ON THE SAME FRAMES.** The wedge is
    adversarial by construction and a swing there is not a picture-quality
    claim, so the question is whether ordinary content moves with it -- and two
    separate runs could not answer that, the optimum re-rolling per
    acquisition. One clip, both measures, and the comparison holds.

    THE BOX IS HELD, NOT RE-FOUND PER PHASE. Locating it each time makes the
    instrument follow the thing it is measuring: a phase that aliases the
    finest grating into grey moves the step out of flat card to the RIGHT, so
    the box slides onto coarser bars and the worse phase scores HIGHER. Seen
    on 1024x768@60, where re-finding put the box at 364 for phases 3..21 and
    at 460..614 for 29..31, and the field's apparent best landed in the
    latter.

    The picture does not move with the phase -- measured at 1280x720@60, the
    located box read one value across all 32 -- so a held box is scoring the
    same pixels. The lit edge comes back with every reading so that a frame
    that DID move is visible in the results rather than hidden in them.
    """
    greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
    contrast, movement = wedge.score(greys, box)
    lit = lit_box(greys[0])
    global_ = sharpness.measure(greys, lit, exclude_rows=(box[2], box[3]))
    return contrast, movement, lit[0], global_


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv")
    parser.add_argument("--mode", help="ModeServ request to put the source in")
    parser.add_argument("--passes", type=int, default=3)
    parser.add_argument("--step", type=int, default=1)
    parser.add_argument("--sp", type=int,
                        help="hold PA_SP_S here for the walk, rather than "
                             "letting each solve re-choose it")
    args = parser.parse_args()

    if args.mode:
        if not args.modeserv:
            parser.error("--mode needs --modeserv")
        print(modeserv(args.modeserv, args.mode))
        modeserv(args.modeserv, "PATTERN PM5544")
        modeserv(args.modeserv, "ANIM OFF")
        time.sleep(20)

    dev = device()
    phases = list(range(0, PHASE_MAX + 1, args.step))

    # THE ENGINE'S OWN CHOICE, not whatever a previous run left held: the hold
    # survives until something gives the phase back, so a bare read names the
    # last experiment rather than the firmware.
    route(args.host, "/phase?clear")
    time.sleep(SETTLE_S)
    chosen = read_named(args.host, "PA_ADC_S")
    divider = read_named(args.host, "PLLAD_MD")
    print(f"PLLAD_MD {divider}, PA_ADC_S in force {chosen}, "
          f"{len(phases)} phases x {args.passes} interleaved passes")

    # Found once, at the phase the engine chose, and held for the walk.
    anchor = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
    lit = lit_box(anchor[0])
    box = wedge.locate(anchor[0], lit)
    print(f"wedge box x {box[0]}..{box[1]}, rows {box[2]}..{box[3]}, "
          f"found at phase {chosen} and held")

    if args.sp is not None:
        pin_sync_phase(args.host, args.sp)
        print(f"PA_SP_S held at {args.sp} for the walk")

    taken = {phase: [] for phase in phases}
    with resolve_watch.attached(args.host) as watch:
        try:
            for index in range(args.passes):
                for phase in phases:
                    apply_phase(args.host, phase)
                    taken[phase].append(reading(dev, box))
                print(f"  pass {index + 1} of {args.passes} done, "
                      f"{watch.summary()}")
        finally:
            route(args.host, "/phase?clear")

    print("\nphase  contrast (sd)   ratio   mean|dx|  p99|dx|  mean|dy|   luma  lit left")
    rows = []
    for phase in phases:
        contrast = np.array([r[0] for r in taken[phase]])
        movement = np.array([r[1] for r in taken[phase]])
        edges = {r[2] for r in taken[phase]}
        picture = {key: np.mean([r[3][key] for r in taken[phase]])
                   for key in taken[phase][0][3]}
        ratio = movement.mean() / contrast.mean()
        rows.append((phase, contrast.mean(), ratio, picture))
        mark = "  <== engine" if phase == chosen else ""
        print("  %3d  %6.1f (%4.1f)  %5.3f   %7.3f  %7.2f  %7.3f  %5.1f   %s%s"
              % (phase, contrast.mean(), contrast.std(), ratio,
                 picture["mean_dx"], picture["p99_dx"], picture["mean_dy"],
                 picture["luma"], ",".join(str(e) for e in sorted(edges)), mark))

    best = max(rows, key=lambda r: r[1])
    worst = min(rows, key=lambda r: r[1])
    print(f"\nclearest phase {best[0]} at contrast {best[1]:.1f}, "
          f"murkiest {worst[0]} at {worst[1]:.1f} -- "
          f"{best[1] / max(worst[1], 0.01):.2f}x")

    # WHAT THE SWINGS ARE COMPARED WITH. The wedge is adversarial and the
    # picture is not, so the question the sweep exists to answer is the RATIO
    # of the two swings: a picture that barely moves while the wedge moves 3x
    # says the phase is a test-card artefact. mean|dy| is the control -- the
    # phase is horizontal and cannot move it.
    print("\nswing across the field, peak over trough:")
    for key, what in (("mean_dx", "mean |dx|, the whole picture"),
                      ("p99_dx", "p99 |dx|, its sharpest edges"),
                      ("mean_dy", "mean |dy|, THE CONTROL"),
                      ("luma", "picture luma")):
        values = np.array([r[3][key] for r in rows])
        print("  %-30s %8.3f .. %8.3f   %5.3fx   at phases %d and %d"
              % (what, values.min(), values.max(),
                 values.max() / max(values.min(), 1e-9),
                 rows[int(values.argmin())][0], rows[int(values.argmax())][0]))
    wedge_swing = best[1] / max(worst[1], 0.01)
    picture_swing = max(
        np.array([r[3][key] for r in rows]).max()
        / max(np.array([r[3][key] for r in rows]).min(), 1e-9)
        for key in ("mean_dx", "p99_dx"))
    print(f"\nthe wedge swings {wedge_swing:.2f}x and the picture "
          f"{picture_swing:.3f}x. A picture swing near the control's is the "
          f"phase costing nothing a viewer sees.")

    print(f"\n{watch.summary()}")
    if not watch.quiet():
        print("  " + "\n  ".join(watch.lines()[:12]))
        print("  A run that is not quiet measured more than the phase: the arm "
              "takes the sync pad away, which re-places the sink's window, and "
              "the re-install re-latches the ADC PLL.")

    print(f"\nThe box was held at x {box[0]}..{box[1]} throughout. A lit left "
          f"edge that is not one value means the picture moved under it, and "
          f"those phases are not comparable with the rest.")


if __name__ == "__main__":
    main()
