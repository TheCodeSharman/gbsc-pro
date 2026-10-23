#!/usr/bin/env python3
"""Re-acquire the source N times and ask whether the sampling clock locked.

    python3 tools/gbsc-pro-hwtest/reacquire_soak.py --host 192.168.88.108
    ... --src ypbpr --other vga -n 12       which input, and what to bounce via
    ... --via restart                        restart the unit instead of bouncing
    ... --settle 22                         seconds to allow per acquisition

**THE FAULT THIS EXISTS FOR IS PER ACQUISITION, NOT PER SECOND.** The ADC PLL
either locks when the source is acquired or never does, and the picture follows
it -- so a unit watched for a minute says nothing and the measurement is a RUN
of acquisitions. One that never locks shows as jittery edges and a picture that
jumps, with every configuration register reading correct.

**THE VERDICT IS `STATUS_MISC_PLLAD_LOCK` AS A SHARE, WHICH NEEDS THE SAMPLING
LOG.** The bit is a duty cycle rather than a state: it flickers about nine
times a second on a picture judged perfect, so one HTTP read has better than a
one in eight chance of reading 0 while everything is right, and HTTP answers at
tens of hertz at best. Measured at 25 ms from `loop()` the two states are not
near a threshold -- a healthy acquisition reads 1 in 84..87% of samples and a
dead one reads 0 in every sample of 769.
../../docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md

The phase each acquisition landed on is recorded beside it, because the choice
being a LOTTERY is the thing a soak can show and a single reading cannot.

**WHICH ROUTE PROVOKES THE ACQUISITION IS PART OF THE TEST.** An input bounce
re-measures the source through the reference clock already held and locked 7 of
7 when the fault was live on the panel, so it does not reach the state. A
restart runs the whole of boot, installing the ADC PLL from nothing against a
chip that kept its registers across the ESP reset -- which is the state a
session opens in, and where the fault has been seen.
"""

import argparse
import collections
import json
import os
import statistics
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit

try:
    import websocket
except ImportError:
    websocket = None

# A dead acquisition reads this, and it is here so a test can build one.
DEAD_SAMPLE = [0]

# Where a healthy acquisition and a dead one are separated. They sit at 85% and
# 0%, so nothing is near this and it needs no precision -- what it must not be
# is high enough for the duty cycle's own gaps to fail a locked clock.
LOCKED_SHARE = 0.5

# Below this the window was missed rather than the clock dead. The console
# drops bursts, so a run that collected almost nothing is a capture fault and
# reporting it as a firmware one is how flakiness reads as a regression.
LEAST_SAMPLES = 40

LOCKED, UNLOCKED, UNRESOLVED = "locked", "unlocked", "unresolved"

VIA_INPUT, VIA_RESTART = "input", "restart"

# Long enough for /input to be read out of loop() and the preference written.
PreferenceWriteS = 4.0

# How long a boot is deaf for. The console delivers nothing for the first
# seconds and HTTP answers from the network stack before loop() runs, so a poll
# started earlier reads the boot that is going away.
BOOT_DEAF_S = 12.0


def lock_share(samples):
    """How much of the time the ADC PLL reported lock."""
    return sum(1 for s in samples if s) / float(len(samples)) if samples else 0.0


def enough(samples):
    """Whether the capture collected enough to be a verdict at all."""
    return len(samples) >= LEAST_SAMPLES


def locked(samples):
    return lock_share(samples) > LOCKED_SHARE


def judge(samples, phase, htotal=()):
    """One acquisition's verdict, the phase it chose, and what it read."""
    if not enough(samples):
        verdict = UNRESOLVED
    else:
        verdict = LOCKED if locked(samples) else UNLOCKED
    return {"verdict": verdict, "phase": phase, "samples": len(samples),
            "share": lock_share(samples),
            "htotal_sd": statistics.pstdev(htotal) if len(htotal) > 1 else 0.0}


def summarise(runs):
    """The soak's figure: locked of JUDGED, with the phases chosen beside it.

    An acquisition the instrument missed belongs in neither half, so it is
    counted apart rather than scored as a failure.
    """
    judged = [r for r in runs if r["verdict"] != UNRESOLVED]
    return {"judged": len(judged),
            "locked": sum(1 for r in judged if r["verdict"] == LOCKED),
            "unresolved": len(runs) - len(judged),
            "phases": dict(collections.Counter(r["phase"] for r in runs))}


def sample_lock(host, ms=25, span_ms=6000, timeout_s=30):
    """`pllad_lock` and `sp_htotal` from the sampling log, as two lists.

    Queued from the process holding the websocket: the log prints to the
    console, so a capture has to be listening before it starts, and queuing
    from a separate shell races both ways -- the route answers `already
    running`, or the log finishes before the socket is attached.
    """
    if websocket is None:
        sys.exit("websocket-client is not installed -- enter the dev shell")
    ws = None
    for _ in range(8):
        try:
            ws = websocket.create_connection(f"ws://{host}:81",
                                             subprotocols=["arduino"], timeout=5)
            break
        except OSError:
            time.sleep(3)
    if ws is None:
        return [], []
    ws.settimeout(1)

    def queue():
        time.sleep(0.4)
        for _ in range(20):
            try:
                reply = urllib.request.urlopen(
                    f"http://{host}/samplinglog?ms={ms}&for={span_ms}", timeout=8).read()
                if b"queued" in reply:
                    return
            except OSError:
                pass
            time.sleep(0.5)

    threading.Thread(target=queue, daemon=True).start()
    names, rows, started = None, [], time.time()
    while time.time() - started < timeout_s:
        try:
            line = ws.recv().strip()
        except Exception:
            continue
        if line.startswith("smp,"):
            parts = line.split(",")
            if parts[1] == "header":
                names = [parts[0]] + parts[2:]
                continue
            rows.append(parts)
        if "done" in line:
            break
    ws.close()
    if not names or not rows:
        return [], []
    where = {name: i for i, name in enumerate(names)}

    def column(name):
        i = where[name]
        return [int(r[i]) for r in rows if len(r) > i and r[i].lstrip("-").isdigit()]

    return column("pllad_lock"), column("sp_htotal")


def measured_wanted_source(host, src):
    """Whether the unit is on the source the run is about.

    A run that came back on another input is not a verdict about this one, and
    nothing else says so: the phase, the lock share and the count are all
    perfectly good readings OF THE WRONG SOURCE.

    **IT CANNOT ALWAYS ANSWER, SO THE COUNT IS PRINTED PER RUN AS WELL.**
    /preferences.txt is served behind a heap gate and refuses below 10000 free,
    which this fork reaches routinely -- measured at 9520 free with /fs/dir
    answering {"error":"low heap"}. An unreadable preference is not evidence of
    the wrong source, so it passes, and the frame count in each row is what
    makes a switch visible: 525 for the Wii at 480p against 311 for the bench
    RISC PC. ../../docs/preferences-file.md
    """
    at = gbs_unit.setting(host, "input")
    return at is None or at == src


def reacquire(host, src, other, settle_s, via=VIA_INPUT):
    """Provoke a fresh acquisition, then wait for the source to be counted.

    **THE TWO ROUTES ARE NOT THE SAME TEST.** An input bounce re-selects the
    mux and re-measures the source through the reference clock already held; a
    restart runs the whole of boot, so detection re-runs the reference-clock
    search and installs the ADC PLL from nothing, against a chip that kept its
    registers across the ESP reset. Only the second reaches the state a session
    opens in.

    Polls the COUNT rather than the state: /geometry's `state` is stale across
    an input change and reads `acquired` from the source that just went away.
    """
    if via == VIA_RESTART:
        # SELECTED BEFORE THE RESTART, NOT AFTER. A boot comes back on whatever
        # /preferences.txt holds, so a route that only restarts measures
        # whichever source the last thing to touch the unit left selected.
        # Selecting first persists it and leaves the boot to restore it, so the
        # ADC PLL is still installed from nothing with no input change in the
        # way.
        gbs_unit.select_input(host, src)
        time.sleep(PreferenceWriteS)
        try:
            urllib.request.urlopen(f"http://{host}/restart", timeout=5).read()
        except OSError:
            pass
        # The unit is deaf for the first seconds of a boot, and an HTTP read
        # that answers before it has gone down answers for the old boot.
        time.sleep(BOOT_DEAF_S)
    else:
        gbs_unit.select_input(host, other)
        time.sleep(settle_s / 2.0)
        gbs_unit.select_input(host, src)
    deadline = time.time() + settle_s * 2
    while time.time() < deadline:
        time.sleep(2.0)
        try:
            geometry = gbs_unit.get_json(host, "/geometry")[1]
        except Exception:
            continue
        if geometry.get("cv") and geometry.get("lineRateHz"):
            time.sleep(4.0)
            return True
    return False


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--src", default="ypbpr", help="the input under test")
    parser.add_argument("--other", default="vga",
                        help="the input to bounce via, which forces a fresh acquisition")
    parser.add_argument("-n", "--runs", type=int, default=12)
    parser.add_argument("--settle", type=float, default=22.0)
    parser.add_argument("--via", choices=(VIA_INPUT, VIA_RESTART), default=VIA_INPUT,
                        help="what provokes each acquisition")
    parser.add_argument("--out", default=None, help="write the runs as JSON")
    args = parser.parse_args()

    runs = []
    # Flushed per line: a soak is minutes long and redirecting it to a file
    # otherwise shows nothing until it ends, which reads as a hung run.
    how = (f"bouncing via {args.other}" if args.via == VIA_INPUT
           else "restarting the unit")
    print(f"{args.runs} acquisitions of {args.src}, {how}\n", flush=True)
    print(f"{'#':>3} {'phase':>5} {'lock':>7} {'n':>5} {'htotal sd':>9} "
          f"{'lines':>6}  verdict", flush=True)
    for run in range(1, args.runs + 1):
        if not reacquire(args.host, args.src, args.other, args.settle,
                         args.via):
            print(f"{run:3} {'-':>5} {'-':>7} {'-':>5} {'-':>9} {'-':>6}  "
                  f"never acquired", flush=True)
            runs.append({"verdict": UNRESOLVED, "phase": None, "samples": 0,
                         "share": 0.0, "htotal_sd": 0.0})
            continue
        if not measured_wanted_source(args.host, args.src):
            print(f"{run:3} {'-':>5} {'-':>7} {'-':>5} {'-':>9} {'-':>6}  "
                  f"came back on another input", flush=True)
            runs.append({"verdict": UNRESOLVED, "phase": None, "samples": 0,
                         "share": 0.0, "htotal_sd": 0.0})
            continue
        phase = gbs_unit.read_named(args.host, "PA_SP_S")
        lines = gbs_unit.read_named(args.host, "STATUS_SYNC_PROC_VTOTAL")
        lock, htotal = sample_lock(args.host)
        one = judge(lock, phase, htotal)
        one["lines"] = lines
        runs.append(one)
        print(f"{run:3} {phase:5} {one['share'] * 100:6.1f}% {one['samples']:5} "
              f"{one['htotal_sd']:9.2f} {lines:6}  {one['verdict']}", flush=True)

    got = summarise(runs)
    print(f"\n{got['locked']} of {got['judged']} acquisitions locked"
          + (f", {got['unresolved']} unresolved" if got["unresolved"] else ""))
    print("phases chosen: " + ", ".join(f"{p}x{n}" for p, n in sorted(
        got["phases"].items(), key=lambda kv: (kv[0] is None, kv[0]))))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as out:
            json.dump({"runs": runs, "summary": got}, out, indent=1)
        print(f"written to {args.out}")
    return 0 if got["judged"] and got["locked"] == got["judged"] else 1


if __name__ == "__main__":
    sys.exit(main())
