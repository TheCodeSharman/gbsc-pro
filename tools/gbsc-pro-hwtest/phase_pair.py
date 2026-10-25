#!/usr/bin/env python3
"""Do the two sampling phases share a cause? Both picture-bests, per acquisition.

    python3 phase_pair.py --host 192.168.88.108 --modeserv 192.168.88.10 \
        --mode "MODE X1280 Y720 C256 F60" --trials 8

**WHY THIS IS NOT `phase_correlation.py`.** That tool correlated the best
`PA_ADC_S` against the phase the ENGINE CHOSE for `PA_SP_S`, which is the scan
band's antipode -- 29 or 6, nothing between. Over eight re-acquisitions the
chosen value took 30, 31 and 0, all one band, so the run held constant the very
quantity it was correlating against and had no power either way. The picture's
own best `PA_SP_S` is a different quantity: on 800x600@60 separate it is a band
at 8..12 while the chosen sits at 29.
`docs/investigations/the-pass-through-shimmer-is-the-capture-origin.md`

**AND IT IS THE HYPOTHESIS THE REGISTER SPACE CANNOT REFUTE.** Six acquisitions
with full 1536-register dumps were byte-identical but for `PA_SP_S` while the
best `PA_ADC_S` moved 4 to 20, so what re-rolls is not a register. Where the
sync edge falls inside the ADC clock period is in no register either, and it is
upstream of both adjusters -- so both bests moving together is exactly what a
shared hidden cause looks like, and a fixed offset between them would be a
derivation needing no search.

**THE ORIGIN IS RECORDED, NOT HELD.** `SP_RT_HS_SP` places the capture
counter's origin and a wrong one costs the grating 9 to 13 times, far more than
either phase, so an origin re-rolling per acquisition would scatter both bests
and hide a real correlation. It was byte-identical across those six dumps and
no override route exists, so this records it as a witness: a run whose origin
moved is one to discard rather than to interpret.

**ONE PASS PER PHASE.** The best phase is repeatable inside one acquisition, so
interleaving buys nothing here and step 1 over both fields is affordable
instead -- which matters, the plateau being about six wide and a step of four
able to miss a narrower peak.

**THE BOX IS FOUND ONCE AND HELD ACROSS BOTH SWEEPS.** Re-finding it per point
makes the instrument follow the thing it measures: a phase that aliases the
finest block into grey slides the box onto coarser bars and the worse phase
scores higher.
"""

import argparse
import collections
import csv
import re
import socket
import sys
import threading
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
FIELD = PHASE_MAX + 1

# Acquisition is seconds on this bench, so this is a ceiling and not a budget.
ACQUIRE_S = 22

WITNESSES = ["PA_SP_S", "PA_ADC_S", "PLLAD_MD", "SP_RT_HS_SP",
             "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HTOTAL"]

# Adc::MidField, the phase the search keeps whenever the scan says nothing
# against it.
MID_FIELD = 16

SCAN = re.compile(r"phase scan:\s*(\S+)")


class Console:
    """The engine's own lines, with the host clock that saw them.

    `reportScan()` prints the walk's whole scoreboard, one character per phase,
    so what the search DISCARDED is readable from here -- which is what makes
    the keep-the-default branch measurable without touching the firmware.
    """

    def __init__(self, host):
        self.host = host
        self.lines = collections.deque(maxlen=40000)
        self.stop = threading.Event()
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        import websocket
        while not self.stop.is_set():
            try:
                link = websocket.create_connection(
                    "ws://%s:81" % self.host, subprotocols=["arduino"],
                    timeout=5)
                link.settimeout(0.5)
            except Exception:
                time.sleep(3)
                continue
            while not self.stop.is_set():
                try:
                    text = link.recv().strip()
                except Exception as why:
                    if "timed out" in str(why):
                        continue
                    break
                with self.lock:
                    self.lines.append((time.time(), text))
            try:
                link.close()
            except Exception:
                pass

    def since(self, when):
        with self.lock:
            return [text for seen, text in self.lines if seen >= when]


def scan_since(console, when):
    """The LAST scoreboard printed since `when`, or None."""
    found = None
    for text in console.since(when):
        match = SCAN.search(text)
        if match:
            found = match.group(1)
    return found


def middle_of_widest_clean_run(scan):
    """Adc::middleOfWidestCleanRun, off the console's own scoreboard.

    scanDigit() prints '.' for a phase whose count read the divider exactly, so
    a dot is a dither of 0 and nothing else is one.
    """
    width = len(scan)
    best_start, best_length, start, length = 0, 0, 0, 0
    for step in range(2 * width):
        phase = step % width
        if scan[phase] != ".":
            length = 0
            continue
        if length == 0:
            start = phase
        length += 1
        if length > best_length and length <= width:
            best_length, best_start = length, start
    if best_length == 0:
        return MID_FIELD
    return (best_start + best_length // 2) % width


def apart(a, b, field=FIELD):
    """How far two phases sit, the short way round."""
    gap = (a - b) % field
    return min(gap, field - gap)


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
    assert len(columns) and len(rows), (
        "the capture is entirely black. Ask the board before the connector: "
        "s0_46's resets, then s0_45 and s0_49. hdmi_capture.py")
    return int(columns[0]), int(columns[-1]), int(rows[0]), int(rows[-1])


def sweep(host, dev, box, which, step):
    """(best, curve) for one adjuster, the peak taken on a CIRCULAR argmax.

    The curve wraps, so assuming the field's ends are far from the peak is
    wrong -- one acquisition peaked at 30 with 0 and 31 within a grey level.
    """
    curve = []
    for phase in range(0, PHASE_MAX + 1, step):
        route(host, f"/phase?{which}={phase}")
        time.sleep(SETTLE_S)
        greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        contrast, movement = wedge.score(greys, box)
        left = lit_box(greys[0])[0]
        curve.append((phase, contrast, movement, left))
    best = max(curve, key=lambda row: row[1])[0]
    return best, curve


def concentration(offsets):
    """1.0 is a fixed offset, 0.0 is scattered over the field."""
    if not offsets:
        return 0.0
    turns = np.array(offsets) * 2.0 * np.pi / FIELD
    return float(abs(np.exp(1j * turns).mean()))


def circular_span(values):
    """The narrowest arc containing every reading, in steps."""
    if not values:
        return 0
    ordered = sorted(set(values))
    gaps = [(ordered[(i + 1) % len(ordered)] - v) % FIELD
            for i, v in enumerate(ordered)]
    return FIELD - max(gaps)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--other", default="MODE X800 Y600 C256 F60",
                        help="a mode to bounce through, so the source really leaves")
    parser.add_argument("--trials", type=int, default=8)
    parser.add_argument("--step", type=int, default=1)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    dev = device()
    console = Console(args.host)
    rows = []
    for trial in range(args.trials):
        # The engine's own choices, so both holds go first.
        route(args.host, "/phase?clear")
        modeserv(args.modeserv, args.other)
        time.sleep(ACQUIRE_S)
        asked = time.time()
        reply = modeserv(args.modeserv, args.mode)
        if "F-1" in reply:
            print(f"trial {trial + 1}: source read back {reply!r}, no usable "
                  f"raster -- skipped")
            continue
        modeserv(args.modeserv, "PATTERN PM5544")
        modeserv(args.modeserv, "ANIM OFF")
        time.sleep(ACQUIRE_S)

        held = read_fields(args.host, WITNESSES)
        scan = scan_since(console, asked)
        anchor = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=WARMUP))
        try:
            box = wedge.locate(anchor[0], lit_box(anchor[0]))
        except AssertionError as why:
            print(f"trial {trial + 1}: no grating found, skipped -- {why}")
            continue

        # The ADC first, with the engine's own sync phase in force; then the
        # sync phase with the ADC pinned where the engine left it, so each best
        # is measured against one baseline rather than against the other sweep.
        best_adc, adc_curve = sweep(args.host, dev, box, "adc", args.step)
        route(args.host, f"/phase?adc={held['PA_ADC_S']}")
        time.sleep(SETTLE_S)
        best_sp, sp_curve = sweep(args.host, dev, box, "sp", args.step)

        after = read_fields(args.host, ["SP_RT_HS_SP"])
        offset = (best_adc - best_sp) % FIELD
        run_middle = middle_of_widest_clean_run(scan) if scan else None
        row = {
            "scan": scan or "",
            "run_middle": run_middle if run_middle is not None else -1,
            "sp_off_by_chosen": apart(best_sp, held["PA_SP_S"]),
            "sp_off_by_run": (apart(best_sp, run_middle)
                              if run_middle is not None else -1),
            "trial": trial + 1,
            "chosen_sp": held["PA_SP_S"], "chosen_adc": held["PA_ADC_S"],
            "best_sp": best_sp, "best_adc": best_adc, "offset": offset,
            "adc_peak": max(r[1] for r in adc_curve),
            "adc_trough": min(r[1] for r in adc_curve),
            "sp_peak": max(r[1] for r in sp_curve),
            "sp_trough": min(r[1] for r in sp_curve),
            "origin": held["SP_RT_HS_SP"],
            "origin_after": after["SP_RT_HS_SP"],
            "divider": held["PLLAD_MD"],
            "count": held["STATUS_SYNC_PROC_VTOTAL"],
            "left_span": max(r[3] for r in adc_curve) - min(r[3] for r in adc_curve),
        }
        rows.append(row)
        print("trial %2d: best sp %2d (chosen %2d)  best adc %2d (chosen %2d)  "
              "offset %2d  adc %5.1f/%5.1f  sp %5.1f/%5.1f  origin %4d->%4d  "
              "left span %d  run middle %s (off %s vs chosen off %d)"
              % (row["trial"], best_sp, row["chosen_sp"], best_adc,
                 row["chosen_adc"], offset, row["adc_peak"], row["adc_trough"],
                 row["sp_peak"], row["sp_trough"], row["origin"],
                 row["origin_after"], row["left_span"],
                 row["run_middle"], row["sp_off_by_run"],
                 row["sp_off_by_chosen"]), flush=True)

    route(args.host, "/phase?clear")
    console.stop.set()

    if not rows:
        sys.exit("no trial produced a reading")

    if args.out:
        with open(args.out, "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"wrote {args.out}")

    sps = [r["best_sp"] for r in rows]
    adcs = [r["best_adc"] for r in rows]
    origins = sorted({r["origin"] for r in rows} | {r["origin_after"] for r in rows})
    print()
    print("best sp spans %d steps over %d trials, best adc spans %d"
          % (circular_span(sps), len(rows), circular_span(adcs)))
    print("offset concentration %.3f" % concentration([r["offset"] for r in rows]))
    print("SP_RT_HS_SP across the run: %s" % origins)

    if len(origins) > 1:
        print("THE ORIGIN MOVED. It costs the grating more than either phase, "
              "so these trials are not comparable -- discard rather than "
              "interpret.")
    scored = [r for r in rows if r["sp_off_by_run"] >= 0]
    if scored:
        by_chosen = float(np.mean([r["sp_off_by_chosen"] for r in scored]))
        by_run = float(np.mean([r["sp_off_by_run"] for r in scored]))
        print("the best sync phase sits %.2f steps from what the engine chose "
              "and %.2f from the widest clean run's middle, over %d trials"
              % (by_chosen, by_run, len(scored)))
        print("REMOVING THE KEEP-THE-DEFAULT BRANCH WOULD %s"
              % ("HELP" if by_run < by_chosen - 0.5 else
                 "HURT" if by_run > by_chosen + 0.5 else
                 "CHANGE NOTHING MEASURABLE"))
    else:
        print("no phase scan reached the console, so the branch's "
              "counterfactual is unmeasured -- is a client taking delivery, "
              "and is the heap gate open? docs/gbs-control-debug-interface.md")

    if circular_span(sps) <= 1:
        print("THE BEST SYNC PHASE DID NOT MOVE, so nothing here can correlate "
              "with it whatever the ADC phase did. A run that never varied the "
              "predictor is the fault phase_correlation.py had.")
