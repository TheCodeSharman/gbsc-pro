#!/usr/bin/env python3
"""Does the sync phase scan's DISTRIBUTION predict the ADC phase's optimum?

    python3 phase_distribution.py --host 192.168.88.108 --modeserv 192.168.88.10 \
        --mode "MODE X1280 Y720 C256 F60" --trials 8

**WHY THE DISTRIBUTION AND NOT THE CHOSEN VALUE.** The chosen `PA_SP_S` was
tried and carries nothing: over eight re-acquisitions of one mode it took the
three adjacent values 30, 31 and 0 while the best `PA_ADC_S` ranged over 2..28.
But the chosen value is a lossy summary -- the middle of the widest clean run --
and a run that is wide and flat lands in the same place while the band that
defines it moves. `phase_distribution.py` reads the scan ITSELF.

The scan is already taken on every acquisition: `Adc::acquirePhase()` walks all
32 phases, counts how often `STATUS_SYNC_PROC_HTOTAL` misses the divider at
each, and `reportScan()` prints the array as 32 characters. That array is a
measurement of where the incoming sync edge lies inside the ADC clock period --
the same underlying phase shift the ADC's sampling instant is expressed in.

So if the band's position predicts the best `PA_ADC_S`, the value is derivable
with no new measurement at all, and no picture disturbance: the scan runs
already, and the derivation lands where the phase is chosen.

**THE LAST SCAN IS THE ONE THAT COUNTS.** Several run while a source settles
and the engine keeps the last; a run that averaged them would mix states.
"""

import argparse
import re
import socket
import threading
import time
import urllib.request

import numpy as np
import websocket

import wedge
from gbs_unit import read_fields
from hdmi_capture import BLACK, device, frames, luma

WIDTH, HEIGHT = 1920, 1080
CLIP = 12
WARMUP = 8
SETTLE_S = 0.8
PHASE_MAX = 31

# SamplesPerPhase in Adc.cpp: the most dither one phase can score, which is
# what an upper-case 'far' entry is worth -- scanDigit() prints the far count
# in place of the dither, and a phase with far misses is the worst there is.
SAMPLES_PER_PHASE = 20

ACQUIRE_S = 22
SCAN = re.compile(r"phase scan:\s*(\S+)")


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


def parse_scan(text):
    """The 32 dither counts the scan printed, as a float array.

    scanDigit(): '.' is clean, '1'..'9' is that many of SamplesPerPhase, 'a'
    upward is 10 and up, and an UPPER-case letter means the phase also missed
    the divider by more than one -- printed in place of the dither, and the
    worst a phase can be.
    """
    out = []
    for ch in text[:PHASE_MAX + 1]:
        if ch == '.':
            out.append(0.0)
        elif ch.isdigit():
            out.append(float(ch))
        elif ch.islower():
            out.append(10.0 + (ord(ch) - ord('a')))
        else:
            out.append(float(SAMPLES_PER_PHASE))
    return np.array(out)


def circular_centroid(weights):
    """Where the weight sits in a field that wraps, in phase steps."""
    angles = np.arange(len(weights)) * 2 * np.pi / len(weights)
    x = (weights * np.cos(angles)).sum()
    y = (weights * np.sin(angles)).sum()
    if x == 0 and y == 0:
        return None, 0.0
    centre = (np.arctan2(y, x) * len(weights) / (2 * np.pi)) % len(weights)
    concentration = np.hypot(x, y) / max(weights.sum(), 1e-9)
    return float(centre), float(concentration)


def listen(host, seconds, out):
    try:
        ws = websocket.create_connection(f"ws://{host}:81",
                                         subprotocols=["arduino"], timeout=5)
    except Exception:
        return
    ws.settimeout(0.5)
    end = time.time() + seconds
    while time.time() < end:
        try:
            out.append(ws.recv().strip())
        except Exception:
            pass
    try:
        ws.close()
    except Exception:
        pass


def lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), "the capture is entirely black"
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def best_phase(host, dev, box, step):
    phases = list(range(0, PHASE_MAX + 1, step))
    scores = []
    for phase in phases:
        route(host, f"/phase?adc={phase}")
        time.sleep(SETTLE_S)
        greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        scores.append(wedge.score(greys, box)[0])
    # The peak is FLAT -- several phases within a grey level of each other --
    # so a bare argmax is noise. The centroid of the top of the curve is the
    # estimator, taken circularly because the field wraps.
    scores = np.array(scores)
    lift = scores - scores.min()
    weight = np.where(lift >= 0.75 * lift.max(), lift, 0.0)
    full = np.zeros(PHASE_MAX + 1)
    for phase, w in zip(phases, weight):
        full[phase] = w
    centre, _ = circular_centroid(full)
    return centre, int(phases[int(np.argmax(scores))]), list(zip(phases, scores.tolist()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--other", default="MODE X800 Y600 C256 F60")
    parser.add_argument("--trials", type=int, default=8)
    parser.add_argument("--step", type=int, default=2)
    args = parser.parse_args()

    dev = device()
    rows = []
    for trial in range(args.trials):
        route(args.host, "/phase?clear")
        modeserv(args.modeserv, args.other)
        time.sleep(ACQUIRE_S)

        lines = []
        watcher = threading.Thread(target=listen,
                                   args=(args.host, ACQUIRE_S + 4, lines), daemon=True)
        watcher.start()
        time.sleep(0.5)
        modeserv(args.modeserv, args.mode)
        modeserv(args.modeserv, "PATTERN PM5544")
        modeserv(args.modeserv, "ANIM OFF")
        watcher.join(ACQUIRE_S + 8)

        scans = [m.group(1) for ln in lines for m in [SCAN.search(ln)] if m]
        if not scans:
            print(f"trial {trial + 1}: no phase scan seen, skipped")
            continue
        dither = parse_scan(scans[-1])
        band, concentration = circular_centroid(dither)
        chosen = read_fields(args.host, ["PA_SP_S", "PA_ADC_S"])

        anchor = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        try:
            box = wedge.locate(anchor[0], lit_box(anchor[0]))
        except AssertionError as why:
            print(f"trial {trial + 1}: no grating, skipped -- {why}")
            continue
        centre, peak, curve = best_phase(args.host, dev, box, args.step)

        rows.append((band, concentration, chosen["PA_SP_S"], centre, peak, len(scans)))
        print(f"trial {trial + 1:2d}: scan {scans[-1]}")
        print(f"          band centre {band:5.1f} (conc {concentration:.2f}, "
              f"{len(scans)} scans)  PA_SP_S {chosen['PA_SP_S']:2d}  "
              f"best ADC {centre:5.1f} (argmax {peak:2d})")

    route(args.host, "/phase?clear")
    if len(rows) < 2:
        print("\nnot enough trials")
        return

    bands = np.array([r[0] for r in rows])
    bests = np.array([r[3] for r in rows])
    offsets = (bests - bands) % 32
    print("\n band centre   best ADC   (best - band) mod 32")
    for band, _, _, centre, _, _ in rows:
        print(f"   {band:8.1f}   {centre:8.1f}   {(centre - band) % 32:8.1f}")
    angles = offsets * 2 * np.pi / 32
    resultant = float(np.hypot(np.cos(angles).mean(), np.sin(angles).mean()))
    print(f"\nband centre spread {bands.min():.1f}..{bands.max():.1f}")
    print(f"best ADC spread    {bests.min():.1f}..{bests.max():.1f}")
    print(f"offset circular concentration {resultant:.3f} "
          f"(1.000 is a constant offset, 0 is no relationship)")
    if bands.max() - bands.min() < 2.0:
        print("The band never moved, so this run cannot separate a constant "
              "offset from a constant best phase.")


if __name__ == "__main__":
    main()
