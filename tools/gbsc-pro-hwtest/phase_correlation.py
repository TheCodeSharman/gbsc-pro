#!/usr/bin/env python3
"""Does the ADC phase's optimum follow the sync processor's chosen phase?

    python3 phase_correlation.py --host 192.168.88.108 --modeserv 192.168.88.10 \
        --mode "MODE X1280 Y720 C256 F60" --trials 10

**THE QUESTION.** `PA_ADC_S` is where in the ADC clock the converter samples,
and its best value moves between acquisitions of one unchanged mode --
measured at 1280x720@60, divider 2038, two acquisitions peaked at phase 25 and
at 30 with the troughs five apart to match. Nothing on the chip measures
picture sharpness, so a search needs something already observed to key on.

`PA_SP_S` is the candidate, and the reason is that the engine already searches
it per acquisition and it is known to land bimodally -- the scan's heavy-dither
band centres on 13 or on 22 with nothing between, and the two outcomes differ
7.2x in picture quality over 183 acquisitions. That was read as `PA_SP_S` being
"a marker of which state the lock fell into, not a cause", and a marker is
exactly what a derivation needs: if the PLL locks into one of two phase
relationships with the incoming signal, the ADC's best phase moves with it
while the engine holds `PA_ADC_S` at the middle of its field.

So this re-acquires the same mode repeatedly, records the phase the engine's
own search chose, and finds the ADC phase that resolves the grating best. A
constant difference between the two is a derivation and needs no search at all.
`docs/investigations/the-pass-through-shimmer-is-the-capture-origin.md`

**THE ADC HOLD IS RELEASED BEFORE EACH RE-ACQUISITION**, or the engine's own
choice is never seen and the sweep scores a phase this tool put there.
"""

import argparse
import socket
import time
import urllib.request

import numpy as np

import wedge
from gbs_unit import read_fields
from hdmi_capture import BLACK, device, frames, luma

WIDTH, HEIGHT = 1920, 1080
CLIP = 12
WARMUP = 8
SETTLE_S = 0.8
PHASE_MAX = 31

# How long to let an acquisition finish. Acquisition is seconds on this bench,
# so this is a ceiling rather than a budget.
ACQUIRE_S = 22


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
    return urllib.request.urlopen(f"http://{host}{path}", timeout=10).read().decode()


def lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), "the capture is entirely black"
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def contrast_at(host, dev, box, phase):
    route(host, f"/phase?adc={phase}")
    time.sleep(SETTLE_S)
    greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
    return wedge.score(greys, box)[0]


def best_phase(host, dev, box, step):
    """The phase with the most surviving grating, and the whole curve.

    The curve WRAPS, so the peak is taken on a circular argmax rather than by
    assuming the field's ends are far from it -- measured, one acquisition
    peaked at 30 with 0 and 31 within a grey level of it.
    """
    phases = list(range(0, PHASE_MAX + 1, step))
    scores = [contrast_at(host, dev, box, phase) for phase in phases]
    return phases[int(np.argmax(scores))], list(zip(phases, scores))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--other", default="MODE X800 Y600 C256 F60",
                        help="a mode to bounce through, so the source really leaves")
    parser.add_argument("--trials", type=int, default=8)
    parser.add_argument("--step", type=int, default=2)
    args = parser.parse_args()

    dev = device()
    pairs = []
    for trial in range(args.trials):
        # The engine's own choice, so the hold has to go first.
        route(args.host, "/phase?clear")
        modeserv(args.modeserv, args.other)
        time.sleep(ACQUIRE_S)
        modeserv(args.modeserv, args.mode)
        modeserv(args.modeserv, "PATTERN PM5544")
        modeserv(args.modeserv, "ANIM OFF")
        time.sleep(ACQUIRE_S)

        chosen = read_fields(args.host, ["PA_SP_S", "PA_ADC_S", "PLLAD_MD"])
        anchor = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        try:
            box = wedge.locate(anchor[0], lit_box(anchor[0]))
        except AssertionError as why:
            print(f"trial {trial + 1}: no grating found, skipped -- {why}")
            continue

        best, curve = best_phase(args.host, dev, box, args.step)
        at_engine = dict(curve).get(chosen["PA_ADC_S"])
        peak = max(score for _, score in curve)
        trough = min(score for _, score in curve)
        offset = (best - chosen["PA_SP_S"]) % 32
        pairs.append((chosen["PA_SP_S"], chosen["PA_ADC_S"], best, offset, peak, trough))
        print(f"trial {trial + 1:2d}: PA_SP_S {chosen['PA_SP_S']:2d}  "
              f"engine PA_ADC_S {chosen['PA_ADC_S']:2d}  best {best:2d}  "
              f"offset {offset:2d}  peak {peak:5.1f}  trough {trough:5.1f}"
              + (f"  at engine's {at_engine:5.1f}" if at_engine is not None else ""))

    route(args.host, "/phase?clear")

    if not pairs:
        print("\nnothing measured")
        return
    print("\nPA_SP_S   best PA_ADC_S   (best - sp) mod 32")
    for sp, engine, best, offset, _, _ in pairs:
        print(f"  {sp:3d}        {best:3d}            {offset:3d}")
    offsets = np.array([p[3] for p in pairs])
    sps = np.array([p[0] for p in pairs])
    bests = np.array([p[2] for p in pairs])
    print(f"\nPA_SP_S took {len(set(sps.tolist()))} distinct value(s): "
          f"{sorted(set(sps.tolist()))}")
    print(f"best PA_ADC_S took {len(set(bests.tolist()))}: {sorted(set(bests.tolist()))}")
    # Circular spread, because an offset of 31 and one of 0 are one apart.
    angles = offsets * 2 * np.pi / 32
    resultant = np.hypot(np.cos(angles).mean(), np.sin(angles).mean())
    print(f"offset mean {offsets.mean():.1f}, circular concentration {resultant:.3f} "
          f"(1.000 is a constant offset, 0 is no relationship)")
    if len(set(sps.tolist())) < 2:
        print("PA_SP_S never moved, so this run cannot separate a constant "
              "offset from a constant best phase. Re-run until it does.")


if __name__ == "__main__":
    main()
