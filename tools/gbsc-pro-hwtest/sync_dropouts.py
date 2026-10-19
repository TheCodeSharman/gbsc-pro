#!/usr/bin/env python3
"""Count the separator's pulse dropouts and the count collapses that follow.

    python3 tools/gbsc-pro-hwtest/sync_dropouts.py --host <ip> --for 900000
    python3 tools/gbsc-pro-hwtest/sync_dropouts.py --read capture.log

Needs a build carrying `Tv5725::SamplingLog` -- `flash-ota … GBS_SAMPLING_LOG=1`.

**A ROW A SAMPLE CANNOT SEE THIS FAULT.** The console delivers 138 lines a
second where the loop samples at 301, so a run asked for every sample is read
through an interval twice what it asked for -- and both events here last one
field or less. `changes=1` emits only where a watched reading moved, which
takes a 900 s run from 270000 lines to 10000 and the interval to 3.3 ms, and
the device reports its own sample count so a dropped line cannot be mistaken
for a sample never taken.

Two events, counted as EVENTS rather than rows: a collapse holds for about a
field, so anything else moving inside it emits a second row.

- a count collapse, `STATUS_SYNC_PROC_VTOTAL` under 300 against 308
- a pulse dropout, `STATUS_SYNC_PROC_HLOW_LEN` near zero against 153

The asymmetry is the whole argument. A dropout that FOLLOWED the collapse would
be a consequence of it; one that only ever leads it is not.

docs/investigations/the-large-composite-jump-is-a-torn-frame.md
"""
import argparse
import sys
import time
import urllib.request

import websocket

COLUMNS = ("ms divider pllad_lock sp_vtotal sp_htotal hlow_len "
           "hperiod_if vperiod_if hsact ifbits intstatus").split()
MS, VTOTAL, HTOTAL, HLOW, VPERIOD = 0, 3, 4, 5, 7

# Below the count a source line can produce, so a reading under it is a frame
# that restarted rather than one a line short.
COLLAPSE_BELOW = 300

# The separator's pulse gone rather than merely narrowed. 30 is where the
# correlation stops: the band 30..60 leads no collapse at all.
DROPOUT_BELOW = 30

# A collapse holds about a field, so rows closer than this are one event.
EVENT_HOLD_MS = 40


def queue(host, ms, duration):
    url = (f"http://{host}/samplinglog?ms={ms}&changes=1&for={duration}")
    for _ in range(40):
        try:
            body = urllib.request.urlopen(url, timeout=4).read().decode()
        except OSError as exc:
            print(f"  queue: {exc}", file=sys.stderr)
            time.sleep(0.5)
            continue
        if "queued" in body:
            return True
        print(f"  queue: {body.strip()}", file=sys.stderr)
        time.sleep(1.0)
    return False


def rows_of(lines):
    """The sample rows and the run's own (samples, span_ms)."""
    rows, done = [], None
    for line in lines:
        line = line.strip()
        if line.startswith("smp,done"):
            parts = line.split(",")
            if len(parts) >= 4:
                done = (int(parts[2]), int(parts[3]))
            continue
        if not line.startswith("smp,") or line.startswith("smp,header"):
            continue
        parts = line.split(",")
        if len(parts) != len(COLUMNS) + 1:
            continue
        try:
            rows.append([int(part) for part in parts[1:]])
        except ValueError:
            continue
    return rows, done


def capture(host, ms, duration, out=None):
    socket = websocket.create_connection(
        f"ws://{host}:81", subprotocols=["arduino"], timeout=5)
    socket.settimeout(1)
    if not queue(host, ms, duration):
        socket.close()
        raise SystemExit("could not queue the sampling log")

    lines = []
    deadline = time.time() + duration / 1000.0 + 25
    while time.time() < deadline:
        try:
            message = socket.recv()
        except Exception:
            continue
        for line in message.splitlines():
            if not line.strip().startswith("smp,"):
                continue
            lines.append(line.strip())
            if line.strip().startswith("smp,done"):
                deadline = 0
        if deadline == 0:
            break
    socket.close()
    if out:
        with open(out, "w") as handle:
            handle.write("\n".join(lines) + "\n")
    return lines


def events(rows, hold=EVENT_HOLD_MS):
    grouped = []
    for row in rows:
        if grouped and row[MS] - grouped[-1][-1][MS] <= hold:
            grouped[-1].append(row)
        else:
            grouped.append([row])
    return grouped


def report(lines, label=""):
    rows, done = rows_of(lines)
    if done is None:
        print("the run did not report its own sample count", file=sys.stderr)
        return 1
    samples, span = done
    secs = span / 1000.0

    collapses = events([r for r in rows if r[VTOTAL] < COLLAPSE_BELOW])
    dropouts = events([r for r in rows if r[HLOW] < DROPOUT_BELOW])

    print(f"{label}  rows {len(rows)}   samples {samples} over {secs:.0f} s"
          f"   -> {samples/secs:.0f}/s, one every {1000*secs/samples:.2f} ms")
    for name, found in (("count collapses", collapses),
                        ("pulse dropouts", dropouts)):
        rate = f", one every {secs/len(found):.0f} s" if found else ""
        print(f"  {name:16s} {len(found):4d}{rate}"
              f"   per 1000 samples {1000*len(found)/samples:.3f}")

    restarts = {}
    for event in collapses:
        restarts[event[0][VTOTAL]] = restarts.get(event[0][VTOTAL], 0) + 1
    if restarts:
        print("  where the count restarted: " + "  ".join(
            f"{where}:{count}" for where, count in sorted(restarts.items())))

    at = [e[0][MS] for e in collapses]
    from_ = [e[0][MS] for e in dropouts]
    for window in (20, 50):
        before = [c for c in at if any(0 < c - d <= window for d in from_)]
        after = [c for c in at if any(0 < d - c <= window for d in from_)]
        ambient = len(from_) * window / 1000.0 / secs if secs else 0
        print(f"  within {window:3d} ms   before {len(before)}/{len(at)}"
              f"   after {len(after)}/{len(at)}"
              f"   ambient {100*ambient:.2f}%")

    leads = sorted(min(c - d for d in from_ if 0 < c - d <= 100)
                   for c in at if any(0 < c - d <= 100 for d in from_))
    if leads:
        print(f"  lead times, ms: {leads}")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host")
    parser.add_argument("--read", help="report a saved capture instead")
    parser.add_argument("--ms", type=int, default=1,
                        help="sample interval floor; 1 means every loop pass")
    parser.add_argument("--for", dest="duration", type=int, default=900000)
    parser.add_argument("--out")
    parser.add_argument("--label", default="")
    args = parser.parse_args()

    if args.read:
        with open(args.read) as handle:
            return report(handle.readlines(), args.label or args.read)
    if not args.host:
        parser.error("--host or --read is required")
    return report(capture(args.host, args.ms, args.duration, args.out),
                  args.label)


if __name__ == "__main__":
    sys.exit(main())
