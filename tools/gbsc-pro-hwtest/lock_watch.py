#!/usr/bin/env python3
"""Watch one input for the sampling clock losing lock, and record what preceded it.

    python3 tools/gbsc-pro-hwtest/lock_watch.py --host 192.168.88.108
    ... --for 3600               how long to watch
    ... --every 300              seconds between unprompted lock samples

**THE TRIGGER IS WHAT THIS EXISTS FOR, NOT THE STATE.** `reacquire_soak.py`
scores a run of acquisitions, so it answers whether an acquisition locked; it
cannot answer what put a settled source into the unlocked state, because it
provokes every transition itself. This watches a source nobody is touching and
keeps the console either side of the moment it goes.

**A PHASE SCAN IS A FREE SAMPLE OF THE CLOCK STATE.** `Adc::acquirePhase()`
scores each phase on whether `STATUS_SYNC_PROC_HTOTAL` departs from `PLLAD_MD`,
and the sync processor counts in ADC clocks -- so an unlocked PLL makes the
count wander at EVERY phase and the scan returns noise, where a locked one
leaves 20-odd phases reading exactly the divider. The engine runs a scan on
every sync-pad transition and on the maintenance cadence while no phase has
been found, so a passive console capture collects them without asking.

A scan whose clean count is below the firmware's own gate is therefore the
cheap proxy, and it arms a lock sample to confirm it against
`STATUS_MISC_PLLAD_LOCK` -- which is a duty cycle rather than a state and needs
the sampling log at 25 ms to read at all.
../../docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md
"""

import argparse
import os
import statistics
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reacquire_soak

try:
    import websocket
except ImportError:
    websocket = None

# The firmware's own gate, of 32 phases. A scan below it applies no phase at
# all, which is what makes it visible from here rather than a judgement call.
SWEEP_GOOD_ENOUGH = 17

SCAN = "phase dither: "

# What is worth keeping either side of a loss. The engine re-solves about every
# 25 s in the unlocked state, so a window of a couple of minutes holds the
# transition and the solves around it.
KEEP_LINES = 600


def clean_phases(field):
    return sum(1 for c in field if c == "0")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--for", dest="watch_s", type=float, default=3600.0)
    parser.add_argument("--every", type=float, default=300.0,
                        help="seconds between unprompted lock samples")
    parser.add_argument("--out", default=None, help="write the whole console here")
    args = parser.parse_args()

    if websocket is None:
        sys.exit("websocket-client is not installed -- enter the dev shell")

    log = open(args.out, "w") if args.out else None
    started = time.time()
    recent = []
    lost = []
    due = {"at": started + args.every, "why": None}
    pending = {"rows": None}

    def say(text):
        stamp = time.time() - started
        line = f"{stamp:8.2f}  {text}"
        print(line, flush=True)
        if log:
            log.write(line + "\n")
            log.flush()

    def sample(why):
        """Queue a sampling log and read the share off the rows it prints."""
        pending["rows"] = {"lock": [], "htotal": [], "why": why}

        def queue():
            for _ in range(10):
                try:
                    reply = urllib.request.urlopen(
                        f"http://{args.host}/samplinglog?ms=25&for=6000", timeout=8).read()
                    if b"queued" in reply:
                        return
                except OSError:
                    pass
                time.sleep(0.5)

        threading.Thread(target=queue, daemon=True).start()

    ws = websocket.create_connection(f"ws://{args.host}:81",
                                     subprotocols=["arduino"], timeout=5)
    ws.settimeout(1)
    say(f"watching {args.host} for {args.watch_s:.0f}s, "
        f"unprompted lock sample every {args.every:.0f}s")

    names = None
    while time.time() - started < args.watch_s:
        try:
            line = ws.recv().strip()
        except Exception:
            line = ""
        if line:
            recent.append(line)
            if len(recent) > KEEP_LINES:
                recent.pop(0)
            if not line.startswith("smp,"):
                say(line)

        rows = pending["rows"]
        if line.startswith("smp,"):
            parts = line.split(",")
            if parts[1] == "header":
                names = [parts[0]] + parts[2:]
            elif rows is not None and names:
                where = {name: i for i, name in enumerate(names)}
                for key, name in (("lock", "pllad_lock"), ("htotal", "sp_htotal")):
                    i = where.get(name)
                    if i is not None and len(parts) > i and parts[i].lstrip("-").isdigit():
                        rows[key].append(int(parts[i]))

        if rows is not None and ("smp,done" in line or len(rows["lock"]) >= 240):
            share = reacquire_soak.lock_share(rows["lock"])
            n = len(rows["lock"])
            sd = statistics.pstdev(rows["htotal"]) if len(rows["htotal"]) > 1 else 0.0
            verdict = reacquire_soak.judge(rows["lock"], None,
                                           rows["htotal"])["verdict"]
            say(f"LOCK {rows['why']}: {share:.1%} of {n}, sp_htotal sd {sd:.2f} -> {verdict}")
            if n >= reacquire_soak.LEAST_SAMPLES and share < reacquire_soak.LOCKED_SHARE:
                lost.append((time.time() - started, rows["why"], share, n))
                say("=" * 70)
                say(f"UNLOCKED. the {len(recent)} console lines leading here:")
                for held in recent:
                    say("  | " + held)
                say("=" * 70)
            pending["rows"] = None
            due["at"] = time.time() + args.every

        if line.startswith(SCAN) and pending["rows"] is None:
            field = line[len(SCAN):].strip()
            clean = clean_phases(field)
            say(f"scan: {clean} of {len(field)} phases clean"
                f"{'  <- below the gate' if clean < SWEEP_GOOD_ENOUGH else ''}")
            if clean < SWEEP_GOOD_ENOUGH:
                sample(f"after a scan of {clean} clean")

        if pending["rows"] is None and time.time() > due["at"]:
            sample("unprompted")
            due["at"] = time.time() + args.every

    ws.close()
    say(f"done. {len(lost)} unlocked samples")
    for at, why, share, n in lost:
        say(f"  {at:8.2f}s  {why}: {share:.1%} of {n}")
    if log:
        log.close()
    return 1 if lost else 0


if __name__ == "__main__":
    sys.exit(main())
