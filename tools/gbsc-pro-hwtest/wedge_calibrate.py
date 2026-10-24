#!/usr/bin/env python3
"""What the wedge reads on every mode, so a threshold can be set against it.

    python3 wedge_calibrate.py --host 192.168.88.108 --modeserv 192.168.88.10

`test_passthrough_acceptance.py` asks whether the finest grating resolved and
whether it sits still, and both questions need a bar. **A BAR SET FROM ONE
MODE AT ONE SAMPLING PHASE IS NOT A BAR.** The phase is never derived and its
optimum re-rolls per acquisition, so one reading of one mode lands anywhere in
a 3x range -- `docs/investigations/the-adc-sampling-phase-is-a-lottery.md`.

This walks the modes, and at each one reports the grating against the flattest
card clear of it, so the movement reading comes with the capture's own noise
beside it rather than as a bare number.

A full re-derivation also wants `adc_phase_sweep.py` on the mode with the
LOWEST sampling density, which is where the lottery's range is widest: the
contrast floor has to clear the trough of that range, not the reading this
tool happened to take.
"""

import argparse
import socket
import time

import numpy as np

import wedge
from gbs_unit import read_fields, wait_for
from hdmi_capture import BLACK, device, frames, luma

WIDTH, HEIGHT = 1920, 1080
CLIP = 16
WARMUP = 8

MODES = [
    ("640x480@60", "MODE X640 Y480 C256 F60", 480),
    ("800x600@60", "MODE X800 Y600 C256 F60", 600),
    ("1024x768@60", "MODE X1024 Y768 C256 F60", 768),
    ("1280x720@60", "MODE X1280 Y720 C256 F60", 720),
    ("1280x1024@60", "MODE X1280 Y1024 C256 F60", 1024),
]


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


def lit_box(grey):
    columns = np.where(grey.mean(axis=0) > BLACK)[0]
    rows = np.where(grey.mean(axis=1) > BLACK)[0]
    assert len(columns) and len(rows), "the capture is entirely black"
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def settle(host, source, request, picture_lines):
    """Poll the COUNT rather than /geometry's state, which is stale across a
    source change -- it answers `acquired` from the source that went away."""
    reply = modeserv(source, request)
    assert "F-1" not in reply, f"the source read back {reply!r}: no usable raster"
    seen = []

    def held():
        seen.append(read_fields(host, ["STATUS_SYNC_PROC_VTOTAL"])
                    .get("STATUS_SYNC_PROC_VTOTAL"))
        run = seen[-4:]
        return len(run) == 4 and run[0] and len(set(run)) == 1 and run[0] >= picture_lines
    if not wait_for(held, timeout=45.0, interval=0.5):
        return None
    return seen[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", required=True)
    args = parser.parse_args()

    dev = device()
    # NO SAMPLES-PER-PIXEL COLUMN. That is the divider over the source's LINE
    # TOTAL, blanking included, which nothing here can read -- the active pixel
    # count in the mode's name is not it. `published_rasters.py` states the
    # total per mode for whichever monitor definition is loaded.
    print("mode            count  PLLAD_MD   contrast  movement   flat  multiple")
    for name, request, lines in MODES:
        count = settle(args.host, args.modeserv, request, lines)
        if count is None:
            print(f"{name:14}  never held a count of {lines}+ -- skipped")
            continue
        modeserv(args.modeserv, "PATTERN PM5544")
        modeserv(args.modeserv, "ANIM OFF")
        time.sleep(12)

        held = read_fields(args.host, ["PLLAD_MD", "STATUS_SYNC_PROC_HTOTAL"])
        greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        lit = lit_box(greys[0])
        try:
            grating = wedge.locate(greys[0], lit)
        except AssertionError as why:
            print(f"{name:14}  {count:5}  {held['PLLAD_MD']:8}  no grating: {why}")
            continue
        contrast, movement = wedge.score(greys, grating)
        _, flat = wedge.score(greys, wedge.flattest(greys[0], lit, grating))
        print("%-14s  %5d  %8d   %8.2f  %8.2f  %5.2f  %6.1fx"
              % (name, count, held["PLLAD_MD"], contrast, movement, flat,
                 movement / max(flat, 0.01)))


if __name__ == "__main__":
    main()
