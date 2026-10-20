#!/usr/bin/env python3
"""Queue the on-device sampling log and report what each column DID.

    python3 tools/gbsc-pro-hwtest/sampling_stats.py --host <ip>
    python3 tools/gbsc-pro-hwtest/sampling_stats.py --host <ip> --ms 1 --for 180

The log prints to the console, so the socket has to be attached before the run
starts; queuing from a separate shell races both ways and gives an empty
capture. This holds the socket, queues, and reads until the run ends.

**THE CADENCE DECIDES WHAT EXISTS.** The sync processor measures per LINE --
15625 a second on the bench source -- so 25 ms samples one line in 420 and a
disturbance confined to a few frames is invisible. `--ms 1` lands about 150
rows a second, which is what the loop and the console together will carry.

**A WIDE FIELD TEARS ACROSS ITS TWO BYTES**, so an outlier is checked before it
is believed: a value whose high byte and low byte are each another observed
value's is reported as a crossing rather than as a reading.
`docs/investigations/a-wide-status-field-tears-across-its-two-bytes.md`.

`STATUS_MISC_PLLAD_LOCK` is one bit and cannot tear, so its duty and flip rate
are the columns that carry across a comparison of two states.
`docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md`.
"""

import argparse
import collections
import re
import time
import urllib.request

import websocket

COLUMNS = ["divider", "pllad_lock", "sp_vtotal", "sp_htotal", "hlow_len",
           "hperiod_if", "vperiod_if", "hsact", "ifbits", "intstatus"]
SAMPLE = re.compile(r"^smp,(\d+),(.*)$")
LOCK = COLUMNS.index("pllad_lock")


def collect(host, interval_ms, seconds):
    ws = websocket.create_connection("ws://%s:81" % host,
                                     subprotocols=["arduino"], timeout=5)
    ws.settimeout(0.5)
    deadline = time.time() + 2
    while time.time() < deadline:
        try:
            ws.recv()
        except Exception:
            pass

    url = "http://%s/samplinglog?ms=%d&for=%d" % (host, interval_ms,
                                                  seconds * 1000)
    for _ in range(40):
        reply = urllib.request.urlopen(url, timeout=10).read().decode()
        if "queued" in reply:
            break
        time.sleep(2)
    else:
        ws.close()
        raise SystemExit("sampling log never queued: %s" % reply)

    rows, other = [], []
    deadline = time.time() + seconds + 6
    while time.time() < deadline:
        try:
            line = ws.recv().strip()
        except Exception:
            continue
        match = SAMPLE.match(line)
        if match:
            parts = match.group(2).split(",")
            if len(parts) >= len(COLUMNS):
                rows.append([int(p) for p in parts[:len(COLUMNS)]])
        elif line and not line.startswith("smp,header"):
            other.append(line)
    ws.close()
    return rows, other


def crossing(value, counts):
    """Whether `value` is one observed value's high byte over another's low one.

    A torn read is RARER than both values it crosses, which is what keeps the
    genuine value from matching its own crossings.
    """
    mine = counts[value]
    high = max((counts[v] for v in counts
                if v != value and v >> 8 == value >> 8), default=0)
    low = max((counts[v] for v in counts
               if v != value and v & 0xFF == value & 0xFF), default=0)
    return mine < high and mine < low


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--ms", type=int, default=1)
    ap.add_argument("--for", dest="seconds", type=int, default=120)
    args = ap.parse_args()

    rows, other = collect(args.host, args.ms, args.seconds)
    if not rows:
        raise SystemExit("no sample rows -- was the log already running?")
    n = len(rows)
    print("%d rows over %d s (%.0f/s), %d other console lines"
          % (n, args.seconds, n / args.seconds, len(other)))

    lock = [r[LOCK] for r in rows]
    flips = sum(1 for i in range(1, n) if lock[i] != lock[i - 1])
    print("PLLAD lock duty %.1f%%, %.1f flips/s"
          % (100.0 * sum(lock) / n, flips / args.seconds))

    for i, name in enumerate(COLUMNS):
        counts = collections.Counter(r[i] for r in rows)
        if len(counts) == 1:
            print("  %-11s %d throughout" % (name, next(iter(counts))))
            continue
        parts = []
        for value, count in sorted(counts.items()):
            tag = " (crossing)" if crossing(value, counts) else ""
            parts.append("%d x%d%s" % (value, count, tag))
        print("  %-11s %d distinct: %s" % (name, len(counts), ", ".join(parts)))


if __name__ == "__main__":
    main()
