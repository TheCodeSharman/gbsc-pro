#!/usr/bin/env python3
"""Queue the on-device sampling log and report what each column DID.

    python3 tools/gbsc-pro-hwtest/sampling_stats.py --host <ip>
    python3 tools/gbsc-pro-hwtest/sampling_stats.py --host <ip> --ms 1 --for 180
    python3 tools/gbsc-pro-hwtest/sampling_stats.py --host <ip> --width 10

The log prints to the console, so the socket has to be attached before the run
starts; queuing from a separate shell races both ways and gives an empty
capture. This holds the socket, queues, and reads until the run ends.

**THE CADENCE DECIDES WHAT EXISTS.** The sync processor measures per LINE --
15625 a second on the bench source -- so 25 ms samples one line in 420 and a
disturbance confined to a few frames is invisible. `--ms 1` lands about 150
rows a second, which is what the loop and the console together will carry.

`--width` asks the device for the sync pulse alone, as fast as the bus allows,
reported one row per window. That is the only mode here that sees most LINES
rather than most frames, and the firmware reports a reading that crosses two
commoner ones separately, so a torn read does not reach the outlier counts.
The run's whole value census follows the last window as `cen,` lines.

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


def collect(host, interval_ms, seconds, query=None, pattern=None):
    ws = websocket.create_connection("ws://%s:81" % host,
                                     subprotocols=["arduino"], timeout=5)
    ws.settimeout(0.5)
    deadline = time.time() + 2
    while time.time() < deadline:
        try:
            ws.recv()
        except Exception:
            pass

    url = "http://%s/samplinglog?%s" % (
        host, query if query else "ms=%d&for=%d" % (interval_ms, seconds * 1000))
    for _ in range(40):
        reply = urllib.request.urlopen(url, timeout=10).read().decode()
        if "queued" in reply:
            break
        time.sleep(2)
    else:
        ws.close()
        raise SystemExit("sampling log never queued: %s" % reply)

    rows, other = [], []
    want = pattern if pattern else SAMPLE
    fields = 6 if pattern else len(COLUMNS)
    deadline = time.time() + seconds + 6
    while time.time() < deadline:
        try:
            line = ws.recv().strip()
        except Exception:
            continue
        match = want.match(line)
        if match:
            parts = match.group(2).split(",")
            if len(parts) >= fields:
                taken = [int(p) for p in parts[:fields]]
                if pattern:
                    taken.append(int(match.group(1)))
                rows.append(taken)
        elif line and "header" not in line:
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


WIDTH = re.compile(r"^wid,(\d+),(.*)$")


def widths(host, window_ms, seconds, low, high):
    query = "width=%d&lo=%d&hi=%d&for=%d" % (window_ms, low, high, seconds * 1000)
    rows, other = collect(host, None, seconds, query=query, pattern=WIDTH)
    if not rows:
        raise SystemExit("no width rows -- was the log already running?")
    n = [r[0] for r in rows]
    total = sum(n)
    print("%d windows of %d ms, %d readings (%.0f/s), %d crossings rejected"
          % (len(rows), window_ms, total, total / seconds, sum(r[5] for r in rows)))
    print("a 50 Hz source runs 15625 lines/s, so this samples one line in %.1f"
          % (15625.0 * seconds / total if total else 0))
    below = [r for r in rows if r[3]]
    above = [r for r in rows if r[4]]
    print("crossings rejected: %d" % sum(r[5] for r in rows))
    census = []
    for line in other:
        if line.startswith("cen,"):
            value, count = line.split(",")[1:3]
            census.append((int(value), int(count)))
    if census:
        print("the run's value census:")
        for value, count in sorted(census):
            print("  %5d = 0x%03X  high 0x%X low 0x%02X  x%d"
                  % (value, value, value >> 8, value & 0xFF, count))
    print("windows holding a reading below %d: %d of %d" % (low, len(below), len(rows)))
    print("windows holding a reading above %d: %d of %d" % (high, len(above), len(rows)))
    if below:
        print("  below-band windows (ms, readings, min, max, below):")
        for r in below[:40]:
            print("    %7d  n=%-4d min=%-5d max=%-5d below=%d" % (r[6], r[0], r[1], r[2], r[3]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--ms", type=int, default=1)
    ap.add_argument("--width", type=int, default=0,
                    help="window in ms for the per-line pulse-width mode")
    ap.add_argument("--lo", type=int, default=120)
    ap.add_argument("--hi", type=int, default=200)
    ap.add_argument("--for", dest="seconds", type=int, default=120)
    args = ap.parse_args()

    if args.width:
        widths(args.host, args.width, args.seconds, args.lo, args.hi)
        return

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
