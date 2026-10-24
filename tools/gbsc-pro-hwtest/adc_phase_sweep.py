#!/usr/bin/env python3
"""Walk the ADC's sampling phase and score what the finest grating does.

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
"""

import argparse
import socket
import time

import numpy as np

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


def reading(dev):
    """(contrast, movement, wedge left edge) for one clip."""
    greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
    box = wedge.locate(greys[0], lit_box(greys[0]))
    contrast, movement = wedge.score(greys, box)
    return contrast, movement, box[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv")
    parser.add_argument("--mode", help="ModeServ request to put the source in")
    parser.add_argument("--passes", type=int, default=3)
    parser.add_argument("--step", type=int, default=1)
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

    taken = {phase: [] for phase in phases}
    try:
        for index in range(args.passes):
            for phase in phases:
                apply_phase(args.host, phase)
                taken[phase].append(reading(dev))
            print(f"  pass {index + 1} of {args.passes} done")
    finally:
        route(args.host, "/phase?clear")

    print("\nphase  contrast (sd)   movement (sd)   ratio   wedge left")
    rows = []
    for phase in phases:
        contrast = np.array([r[0] for r in taken[phase]])
        movement = np.array([r[1] for r in taken[phase]])
        edges = {r[2] for r in taken[phase]}
        ratio = movement.mean() / contrast.mean()
        rows.append((phase, contrast.mean(), ratio))
        mark = "  <== engine" if phase == chosen else ""
        print("  %3d  %6.1f (%4.1f)  %6.2f (%4.2f)   %5.3f   %s%s"
              % (phase, contrast.mean(), contrast.std(),
                 movement.mean(), movement.std(), ratio,
                 ",".join(str(e) for e in sorted(edges)), mark))

    best = max(rows, key=lambda r: r[1])
    worst = min(rows, key=lambda r: r[1])
    print(f"\nclearest phase {best[0]} at contrast {best[1]:.1f}, "
          f"murkiest {worst[0]} at {worst[1]:.1f} -- "
          f"{best[1] / max(worst[1], 0.01):.2f}x")
    print("A wedge left edge that moves between phases means the phase panned "
          "the picture, and the box followed it.")


if __name__ == "__main__":
    main()
