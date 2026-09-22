#!/usr/bin/env python3
"""Acceptance over the VESA DMT modes the bench definition carries.

A mode passes when the engine takes the framing the published raster states,
and the card's one-pixel green frame -- which lands on the source's outermost
row and column -- reaches all four edges of the panel. Anything the engine
derives from a raster the source does not actually emit is untestable, so the
set is the DMT entries SourceTiming has a published raster for.

The green frame is the instrument because it is the only feature that is the
source's extreme: every other marking stops short of it, and a picture judged
against one of those cannot tell a correct edge from a cropped one.

Needs the RetroScaler on --host, ModeServ on --source, and tv-snap pointed at
the panel. docs/bench-sources.md.
"""

import argparse
import socket
import subprocess
import sys
import time
import urllib.request
import json

import numpy as np

from gbs_unit import read_fields

# name, x, y, SELECT rate, line total, active start px, active px, frame lines,
# active start line, active lines -- SourceTiming::Published, which is what the
# engine derives the default framing from.
#
# The select rate is what MODE asks for, and RISC OS rounds the mode's real
# field rate to pick it: DMT 5 is labelled 72 and runs at 72.81, so it is
# reachable as F73 and not as F72.
PUBLISHED = [
    ("640x480@60", 640, 480, 60, 800, 144, 640, 525, 35, 480),
    ("640x480@72", 640, 480, 73, 832, 192, 640, 520, 31, 480),
    ("640x480@75", 640, 480, 75, 840, 184, 640, 500, 19, 480),
    ("800x600@56", 800, 600, 56, 1024, 200, 800, 625, 24, 600),
    ("800x600@60", 800, 600, 60, 1056, 216, 800, 628, 27, 600),
    ("800x600@72", 800, 600, 72, 1040, 184, 800, 666, 29, 600),
    ("800x600@75", 800, 600, 75, 1056, 240, 800, 625, 24, 600),
    ("1024x768@60", 1024, 768, 60, 1344, 296, 1024, 806, 35, 768),
    ("1024x768@70", 1024, 768, 70, 1328, 280, 1024, 806, 35, 768),
    ("1024x768@75", 1024, 768, 75, 1312, 272, 1024, 800, 31, 768),
    ("1280x1024@60", 1280, 1024, 60, 1688, 360, 1280, 1066, 41, 1024),
]

# Motion adapt latches on a separate-sync source and never clears itself, and a
# clean-looking picture is not evidence it did. docs/known-issues.md
ENGAGED = {
    "MADPT_Y_MI_OFFSET": 127,
    "MADPT_Y_MI_DET_BYPS": 1,
    "RFF_FETCH_NUM": 1,
    "WFF_ENABLE": 0,
    "RFF_ENABLE": 0,
    "MAPDT_VT_SEL_PRGV": 1,
}

# A green frame reads as G-R against a white or yellow ring, both of which give
# zero. The camera's white balance sits the whole frame a little negative, so
# the bar is a rise above that floor rather than an absolute level.
GREEN_MIN = 2.0


def modeserv(source, command, timeout=12):
    host, port = source.split(":")
    with socket.create_connection((host, int(port)), timeout=timeout) as s:
        s.sendall((command + "\n").encode())
        out = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                return out.decode(errors="replace").strip()
            out += chunk


def geometry(host):
    with urllib.request.urlopen("http://%s/geometry" % host, timeout=5) as r:
        return json.load(r)


def acquire(host, seconds=30):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            g = geometry(host)
            if g["state"] == "acquired":
                return g
        except Exception:
            pass
        time.sleep(2)
    return None


def snap():
    out = subprocess.run(["tv-snap"], capture_output=True, text=True).stdout
    return out.strip().split()[0]


def frame(path):
    raw = subprocess.run(
        ["ffmpeg", "-loglevel", "error", "-y", "-i", path,
         "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
        capture_output=True).stdout
    px = len(raw) // 3
    for w, h in ((1600, 900), (1920, 1080), (1280, 720)):
        if w * h == px:
            return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(float)
    raise SystemExit("unexpected capture size: %d pixels" % px)


def green_edges(im):
    """Peak G-R on each edge, over the outer eighth, as (value, position)."""
    gr = im[:, :, 1] - im[:, :, 0]
    h, w, _ = im.shape
    rows = gr[:, w // 3: 2 * w // 3].mean(axis=1)
    cols = gr[h // 3: 2 * h // 3, :].mean(axis=0)
    band = lambda p, a, b: (float(p[a:b].max()), a + int(p[a:b].argmax()))
    return {
        "top": band(rows, 0, h // 8),
        "bottom": band(rows, 7 * h // 8, h),
        "left": band(cols, 0, w // 8),
        "right": band(cols, 7 * w // 8, w),
    }


def best_of(shots):
    """The card's ring flashes twice a second and covers the frame in one
    phase, so a single still can miss a frame that is being emitted."""
    out = {}
    for edge in ("top", "bottom", "left", "right"):
        out[edge] = max((s[edge] for s in shots), key=lambda v: v[0])
    return out


def framing_default(g, spec):
    """The framing the published raster states, in ten-thousandths."""
    _, _, _, _, ltot, astart, active, frame_, vstart, vactive = spec
    want = (round(astart * 10000 / ltot), round(active * 10000 / ltot),
            round(vstart * 10000 / frame_), round(vactive * 10000 / frame_))
    got = (g["poh"], g["peh"], g["pov"], g["pev"])
    off = tuple(a - b for a, b in zip(got, want))
    return want, got, off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--source", default="192.168.88.10:6502")
    ap.add_argument("--shots", type=int, default=4)
    ap.add_argument("--only", help="substring of the mode name")
    args = ap.parse_args()

    print(modeserv(args.source, "VERSION"))
    modeserv(args.source, "PATTERN CARD")

    results = []
    for spec in PUBLISHED:
        name, x, y, rate = spec[0], spec[1], spec[2], spec[3]
        if args.only and args.only not in name:
            continue
        print("\n=== %s ===" % name, flush=True)

        reply = modeserv(args.source, "MODE X%d Y%d C256 F%d" % (x, y, rate))
        if not reply.startswith("OK"):
            results.append((name, "SOURCE", reply, None, None))
            print("  source refused: %s" % reply)
            continue

        g = acquire(args.host)
        if g is None:
            results.append((name, "NO LOCK", "", None, None))
            print("  never acquired")
            continue

        urllib.request.urlopen("http://%s/sc?B" % args.host, timeout=5).read()
        time.sleep(4)
        modeserv(args.source, "PATTERN CARD")
        g = geometry(args.host)

        want, got, off = framing_default(g, spec)
        engaged = read_fields(args.host, list(ENGAGED))
        latched = [k for k, v in ENGAGED.items() if engaged[k] != v]

        shots = []
        for _ in range(args.shots):
            shots.append(green_edges(frame(snap())))
        edges = best_of(shots)

        framed = all(abs(d) <= 1 for d in off)
        green = {e: edges[e][0] >= GREEN_MIN for e in edges}
        ok = framed and all(green.values()) and not latched

        print("  rate %5d Hz  raster %d x %d" % (g["lineRateHz"], g["ch"], g["cv"]))
        print("  framing want %s got %s  off %s  %s"
              % (want, got, off, "DEFAULT" if framed else "NOT DEFAULT"))
        for e in ("top", "bottom", "left", "right"):
            v, at = edges[e]
            print("  %-6s green %6.1f @ %4d   %s"
                  % (e, v, at, "yes" if green[e] else "NO"))
        if latched:
            print("  motion adapt ENGAGED: %s" % ", ".join(latched))
        print("  %s" % ("PASS" if ok else "FAIL"))
        results.append((name, "PASS" if ok else "FAIL", off, edges, latched))

    print("\n==================== summary ====================")
    for name, verdict, off, edges, latched in results:
        extra = ""
        if edges:
            missing = [e for e in ("top", "bottom", "left", "right")
                       if edges[e][0] < GREEN_MIN]
            if missing:
                extra += "  no green: " + ",".join(missing)
            if any(abs(d) > 1 for d in off):
                extra += "  framing off by %s" % (off,)
            if latched:
                extra += "  motion adapt engaged"
        print("  %-14s %-6s%s" % (name, verdict, extra))
    passed = sum(1 for r in results if r[1] == "PASS")
    print("\n  %d of %d passed" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
