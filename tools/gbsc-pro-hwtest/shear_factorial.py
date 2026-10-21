#!/usr/bin/env python3
"""Score the composite shear against a randomised factorial of register settings.

    python3 tools/gbsc-pro-hwtest/shear_factorial.py --host <ip> --hours 9

**THE EVENT RATE WANDERS BY THIRTY TIMES ON ITS OWN**, over tens of minutes,
with nothing written: adjacent 600 s legs have measured 0 and 6 bursts. So a
sweep taken in a fixed order correlates with whatever was set during a busy
patch rather than with the setting, and two legs an hour apart compare patches.
Every cell is revisited many times in a RANDOMISED order for that reason, so
each one samples the quiet and the busy patches alike.

**AND RARE EVENTS OVER MANY CELLS FIND CORRELATIONS BY CHANCE.** At about half
an event a minute, resolving 1.5x needs 60 to 80 events a cell -- hours, not
minutes -- and the same events spread over twenty settings yield two or three
convincing-looking correlations from noise alone. The cells here are a
FACTORIAL, so each main effect is measured on the whole run rather than on its
own two cells.

Scored on the burst and the count collapse, which are the mechanism -- a burst
of mis-measured pulses, then the count collapsing, then the displaced frames.
NOT on the ADC PLL lock duty, which `the-composite-shear-rides-a-half-locked-
adc-pll.md` shows does not track the shear, and NOT on `picture_stability`,
which is a vertical instrument and nearly blind to this axis.

**THE SHEAR IS THE SPREAD ACROSS BANDS INSIDE ONE FRAME, NOT A DISPLACEMENT
FROM A REFERENCE.** A coast of 0/0 displaces the picture bodily, and a metric
taken against a reference frame then scores every frame of that cell as
sheared. The spread across bands cancels a displacement the whole frame shares.

Every block carries the picture's own mean luma and its bodily displacement, so
a cell reading zero events can be told from a cell with no picture to score --
the coast pair's header warns that 0/0 is a state the picture cannot be left
on.
"""

import argparse
import collections
import csv
import os
import random
import re
import subprocess
import sys
import threading
import time
import urllib.request

import numpy as np

import gbs_unit
import hdmi_capture

W, H, BANDS = 480, 270, 8
WID = re.compile(r"^wid,(\d+),(.*)$")

# A frame is sheared when its bands disagree by this many columns of the
# downscaled capture, which is four times that in emitted pixels.
SHEAR_COLUMNS = 2

# The count outside this is a collapse rather than the dither every healthy
# frame shows between 308 and 309 on this source.
COUNT_LOW, COUNT_HIGH = 306, 311

# Seconds discarded after a cell is applied. The coast route re-solves, and a
# block scored across that carries the transition rather than the state.
DISCARD_S = 12.0

# Seconds scored in each block. Short against the wander's own ten minutes, so
# adjacent blocks see the same patch and the wander is common to both.
SCORE_S = 48.0

# How often the held setting is checked inside a block. A solve re-runs the
# phase search and takes PA_SP_S back to the engine's own choice, and the
# engine only ever reverts TOWARDS that choice -- so discarding whole blocks on
# a failed read-back would discard the arms that disagree with it and leave the
# one that agrees intact. The window is truncated at the first deviation
# instead, and the block reports the exposure it really had.
CHECK_S = 8.0

# The factorial's default cells. Three coast levels against two sampling
# phases: the engine's own pair, the long post coast no session has tried on
# this source, and no hold at all, which bounds what any pre, post or sub coast
# setting can do.
#
# A run that moves the pre and post lines TOGETHER cannot say which of them
# carries an effect, so attributing one needs the pair crossed instead:
# --coasts 0/0,0/2,7/0,7/2 --phases 16.
COASTS = [(0, 0), (7, 2), (7, 6)]
PHASES = [16, 13]

WITNESSES = ["SP_PRE_COAST", "SP_POST_COAST", "PA_SP_S", "PLLAD_MD",
             "STATUS_SYNC_PROC_VTOTAL", "ADC_SOGCTRL"]


class Console:
    """The width log's rows, collected with the host clock that saw them."""

    def __init__(self, host):
        self.host = host
        self.rows = collections.deque(maxlen=400000)
        self.stop = threading.Event()
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        import websocket
        while not self.stop.is_set():
            try:
                ws = websocket.create_connection(
                    "ws://%s:81" % self.host, subprotocols=["arduino"],
                    timeout=5)
                ws.settimeout(0.5)
            except Exception:
                time.sleep(5)
                continue
            while not self.stop.is_set():
                try:
                    line = ws.recv().strip()
                except Exception as e:
                    if "timed out" in str(e):
                        continue
                    break
                m = WID.match(line)
                if not m:
                    continue
                parts = m.group(2).split(",")
                if len(parts) < 8:
                    continue
                try:
                    with self.lock:
                        self.rows.append((time.time(),
                                          [int(x) for x in parts[:8]]))
                except ValueError:
                    pass
            try:
                ws.close()
            except Exception:
                pass

    def between(self, start, end):
        with self.lock:
            return [r for t, r in self.rows if start <= t <= end]


class Widths:
    """Keeps a width log queued, re-queueing it as each run expires."""

    def __init__(self, host, chunk_s=600):
        self.host = host
        self.chunk_s = chunk_s
        self.stop = threading.Event()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while not self.stop.is_set():
            try:
                reply = urllib.request.urlopen(
                    "http://%s/samplinglog?width=10&lo=120&hi=200&for=%d"
                    % (self.host, self.chunk_s * 1000), timeout=10
                ).read().decode()
            except Exception:
                reply = "unreachable"
            wait = self.chunk_s - 5 if "queued" in reply else 20
            self.stop.wait(wait)


class Frames:
    """Per-frame band profiles off the capture, with the host clock."""

    def __init__(self):
        self.frames = collections.deque(maxlen=900000)
        self.reference = None
        self.stop = threading.Event()
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def _open(self):
        return subprocess.Popen(
            ["ffmpeg", "-hide_banner", "-v", "error", "-f", "v4l2",
             "-input_format", "yuyv422", "-video_size", "1920x1080",
             "-i", hdmi_capture.device(), "-vf", "scale=%d:%d" % (W, H),
             "-pix_fmt", "gray", "-f", "rawvideo", "-"],
            stdout=subprocess.PIPE)

    def _run(self):
        edges = [round(i * H / BANDS) for i in range(BANDS + 1)]
        while not self.stop.is_set():
            ff = self._open()
            warm = []
            while not self.stop.is_set():
                buf = ff.stdout.read(W * H)
                if len(buf) < W * H:
                    break
                now = time.time()
                f = np.frombuffer(buf, np.uint8).reshape(H, W).astype(np.float32)
                bands = np.array([f[edges[i]:edges[i + 1]].mean(axis=0)
                                  for i in range(BANDS)])
                if self.reference is None:
                    warm.append(bands)
                    if len(warm) >= 90:
                        ref = np.median(np.array(warm), axis=0)
                        self.reference = [np.fft.rfft(ref[i] - ref[i].mean())
                                          for i in range(BANDS)]
                    continue
                shifts = [self._shift(bands[i], self.reference[i])
                          for i in range(BANDS)]
                with self.lock:
                    self.frames.append((now, shifts, float(f.mean())))
            try:
                ff.kill()
                ff.wait(timeout=5)
            except Exception:
                pass
            if not self.stop.is_set():
                time.sleep(2)

    @staticmethod
    def _shift(band, reference):
        a = band - band.mean()
        c = np.fft.irfft(np.fft.rfft(a) * np.conj(reference), n=W)
        k = int(np.argmax(c))
        return k - W if k > W // 2 else k

    def between(self, start, end):
        with self.lock:
            return [(s, luma) for t, s, luma in self.frames
                    if start <= t <= end]


def apply_cell(host, coast, phase):
    """Both held in the engine, so neither is taken back by a solve.

    A phase written straight to PA_SP_S does not survive the search, and the
    search runs on the solve the coast route asks for -- so poking it measured
    the engine's determination, and measured it unequally, because the engine
    reverts towards its own choice and only one arm disagrees with that.
    """
    try:
        gbs_unit.get(host, "/phase?sp=%d" % phase)
        gbs_unit.get(host, "/coast?pre=%d&post=%d" % coast)
    except Exception:
        return False
    time.sleep(6.0)
    return True


def hold(host, coast, phase, start):
    """Score until the window ends or the setting stops being the one asked for."""
    held = dict.fromkeys(WITNESSES, -1)
    while True:
        remaining = SCORE_S - (time.time() - start)
        if remaining <= 0:
            return time.time(), held, False
        time.sleep(min(CHECK_S, remaining))
        try:
            held = gbs_unit.read_fields(host, WITNESSES)
        except Exception:
            continue
        if (held.get("SP_PRE_COAST") != coast[0]
                or held.get("SP_POST_COAST") != coast[1]
                or held.get("PA_SP_S") != phase):
            return time.time(), held, True


def score(console, frames, start, end):
    rows = console.between(start, end)
    seen = [f for f in frames.between(start, end)]
    bursts = sum(1 for r in rows if r[3] >= 5)
    collapses = sum(1 for r in rows
                    if r[7] and (r[6] < COUNT_LOW or r[7] > COUNT_HIGH))
    sheared = 0
    spread = 0.0
    bodily = 0.0
    for shifts, _luma in seen:
        across = max(shifts) - min(shifts)
        spread = max(spread, across)
        bodily += float(np.mean(shifts))
        if across >= SHEAR_COLUMNS:
            sheared += 1
    luma = float(np.mean([l for _s, l in seen])) if seen else 0.0
    return {
        "windows": len(rows),
        "readings": sum(r[0] for r in rows),
        "bursts": bursts,
        "collapses": collapses,
        "frames": len(seen),
        "sheared": sheared,
        "worst_spread": spread,
        "bodily": bodily / len(seen) if seen else 0.0,
        "luma": luma,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--hours", type=float, default=9.0)
    ap.add_argument("--out", default="shear_factorial.csv")
    ap.add_argument("--coasts", default=None,
                    help="pre/post pairs, as in 0/0,0/2,7/0,7/2")
    ap.add_argument("--phases", default=None, help="as in 16,13")
    args = ap.parse_args()

    coasts = COASTS
    if args.coasts:
        coasts = [tuple(int(n) for n in pair.split("/"))
                  for pair in args.coasts.split(",")]
    phases = [int(n) for n in args.phases.split(",")] if args.phases \
        else PHASES
    cells = [(c, p) for c in coasts for p in phases]
    print("%d cells: %s" % (len(cells), cells), flush=True)
    console = Console(args.host)
    widths = Widths(args.host)
    frames = Frames()

    print("waiting for the capture's reference", flush=True)
    for _ in range(120):
        if frames.reference is not None:
            break
        time.sleep(1)
    if frames.reference is None:
        sys.exit("no capture; is the dongle attached?")

    fields = ["block", "at_s", "coast_pre", "coast_post", "phase", "scored_s",
              "windows", "readings", "bursts", "collapses", "frames",
              "sheared", "worst_spread", "bodily", "luma", "held_ok"]
    fields += WITNESSES
    fresh = not os.path.exists(args.out)
    out = open(args.out, "a", newline="")
    writer = csv.DictWriter(out, fieldnames=fields)
    if fresh:
        writer.writeheader()

    t0 = time.time()
    block = 0
    try:
        while time.time() - t0 < args.hours * 3600:
            order = cells[:]
            random.shuffle(order)
            for coast, phase in order:
                if time.time() - t0 >= args.hours * 3600:
                    break
                applied = apply_cell(args.host, coast, phase)
                time.sleep(max(DISCARD_S - 6.0, 0))
                start = time.time()
                end, held, drifted = hold(args.host, coast, phase, start)
                row = score(console, frames, start, end)
                row.update(held)
                row["scored_s"] = round(end - start, 1)
                row["held_ok"] = int(applied and not drifted)
                row.update({"block": block, "at_s": round(start - t0, 1),
                            "coast_pre": coast[0], "coast_post": coast[1],
                            "phase": phase})
                writer.writerow(row)
                out.flush()
                print("block %4d  %5.0fs  coast %d/%d phase %2d  %4.0fs  "
                      "bursts %2d collapses %3d sheared %4d/%4d  "
                      "luma %5.1f bodily %+5.1f  held %d"
                      % (block, start - t0, coast[0], coast[1], phase,
                         row["scored_s"],
                         row["bursts"], row["collapses"], row["sheared"],
                         row["frames"], row["luma"], row["bodily"],
                         row["held_ok"]), flush=True)
                block += 1
    finally:
        console.stop.set()
        widths.stop.set()
        frames.stop.set()
        out.close()
        for route in ("/coast?clear=1", "/phase?clear=1"):
            try:
                gbs_unit.get(args.host, route)
            except Exception:
                pass
        print("overrides cleared", flush=True)


if __name__ == "__main__":
    main()
