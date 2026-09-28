#!/usr/bin/env python3
"""Capture Tv5725::SamplingLog and say what it measured.

    python3 tools/gbsc-pro-hwtest/sampling_log.py --host <ip> --ms 25 --for 30000
    python3 tools/gbsc-pro-hwtest/sampling_log.py --read /tmp/capture.csv

The endpoint is compiled in only at GBS_SAMPLING_LOG=1, and an unregistered
route answers 500 rather than 404 -- which reads as a crash in the handler
rather than as a build missing the flag:

    make -C build flash-ota HOST=<ip> GBS_SAMPLING_LOG=1

**The socket has to be open before the route is asked.** The log prints to the
console, so queuing it from one process while another starts listening races
both ways: the request answers already-running against a previous capture, or
the log finishes before the socket attaches. Either gives an empty capture,
which reads as a dead route rather than as a missed window.

The console carries three kinds of frame on one socket. Samples begin `smp,`, a
solve begins `sol,` and is emitted only when the engine MOVES, and a frame
beginning `#` is the web UI's status rather than terminal text.

What the summary is for: `STATUS_MISC_PLLAD_LOCK` is a duty cycle rather than a
state, so the percentage and the transition rate are the readings, and neither
survives an HTTP point read. The honest health metric beside them is
`STATUS_SYNC_PROC_HTOTAL` against the divider, which has a right answer of zero
-- the sync processor counts in ADC clocks, so a locked PLL reports its own
divider back.

**A high duty is not a good picture.** Measured 93.7% against a baseline 85.8%
on a state whose picture was visibly falling apart, so the duty cannot be used
to rank one configuration against another.
`docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md`.
"""

import argparse
import collections
import sys
import time

import numpy as np
import websocket

from gbs_unit import get

COLUMNS = ["ms", "divider", "lock", "vtotal", "htotal", "hperiod", "vperiod",
           "hsact", "ifbits", "intstatus"]

# SamplingLog.cpp packs these into the ifbits byte, lowest bit first.
IF_STATUS_BITS = ["STATUS_IF_HT_OK", "STATUS_IF_VT_OK", "STATUS_IF_HT_BAD",
                  "STATUS_IF_VT_BAD", "STATUS_IF_NO_SYNC",
                  "STATUS_IF_INP_NTSC_INT", "STATUS_IF_INP_PAL_INT"]


def parse(lines):
    """The sample rows, as {column: array}. Everything else on the socket is
    dropped rather than counted."""
    rows = []
    for line in lines:
        parts = line.strip().split(",")
        if len(parts) != len(COLUMNS) + 1 or parts[0] != "smp" or parts[1] == "header":
            continue
        try:
            rows.append([int(part) for part in parts[1:]])
        except ValueError:
            continue
    return {name: np.array([row[index] for row in rows], dtype=int)
            for index, name in enumerate(COLUMNS)}


def lock_statistics(samples):
    count = len(samples["ms"])
    if count == 0:
        return dict(count=0, duty=0.0, transitions=0, rate=0.0, seconds=0.0,
                    offset_min=0, offset_max=0, offset_zero=0.0)
    seconds = (samples["ms"][-1] - samples["ms"][0]) / 1000.0
    lock = samples["lock"]
    offset = samples["htotal"] - samples["divider"]
    transitions = int((np.diff(lock) != 0).sum())
    return dict(count=count, seconds=seconds,
                duty=float(lock.mean()), transitions=transitions,
                rate=transitions / seconds if seconds else 0.0,
                offset_min=int(offset.min()), offset_max=int(offset.max()),
                offset_zero=float((offset == 0).mean()))


def capture(host, query, out, limit=120.0):
    """Hold the socket, then queue, then read to the end of the log."""
    socket = websocket.create_connection(f"ws://{host}:81", subprotocols=["arduino"],
                                         timeout=5)
    socket.settimeout(1)
    answer = ""
    for _ in range(20):
        status, answer = get(host, "/samplinglog?" + query)
        if status == 200 and "queued" in answer:
            break
        time.sleep(2)
    else:
        socket.close()
        raise SystemExit(f"the route never queued: {answer} -- "
                         f"500 is a build without GBS_SAMPLING_LOG=1")

    lines, started = [], time.time()
    while time.time() - started < limit:
        try:
            line = socket.recv().strip()
        except Exception:
            continue
        lines.append(line)
        if line.endswith(",done"):
            break
    socket.close()
    if out:
        with open(out, "w") as handle:
            handle.write("\n".join(lines) + "\n")
    return lines


def report(lines):
    samples = parse(lines)
    stats = lock_statistics(samples)
    if stats["count"] == 0:
        print("no samples -- the socket was attached too late, or the route "
              "answered against a capture already running")
        return 1

    print(f"{stats['count']} samples over {stats['seconds']:.1f}s "
          f"({stats['count'] / stats['seconds']:.1f}/s), "
          f"{sum(1 for line in lines if line.startswith('sol,')) - 1} solves")
    print(f"  lock       {stats['duty'] * 100:5.1f}%   {stats['transitions']} "
          f"transitions ({stats['rate']:.1f}/s)   -- a duty, not a verdict")
    print(f"  htotal-md  {stats['offset_min']:+d}..{stats['offset_max']:+d}"
          f"   exactly 0 in {stats['offset_zero'] * 100:.1f}%"
          f"   -- this one has a right answer")
    for name in ("vtotal", "hperiod", "vperiod"):
        values = samples[name]
        common = "  ".join(f"{value}x{count}" for value, count
                           in collections.Counter(values.tolist()).most_common(3))
        print(f"  {name:9}  {values.min()}..{values.max()}"
              f"  sd {values.std():7.2f}   {common}")
    for value, count in collections.Counter(samples["ifbits"].tolist()).most_common(3):
        named = ", ".join(name for bit, name in enumerate(IF_STATUS_BITS)
                          if value & (1 << bit)) or "none"
        print(f"  ifbits {value:3} x{count:<5}  {named}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host")
    parser.add_argument("--ms", type=int, default=25)
    parser.add_argument("--for", dest="duration", type=int, default=30000)
    parser.add_argument("--out", default=None, help="write the raw capture here")
    parser.add_argument("--read", default=None, help="summarise a saved capture")
    parser.add_argument("--limit", type=float, default=120.0)
    args = parser.parse_args()

    if args.read:
        with open(args.read) as handle:
            return report(handle.read().splitlines())
    if not args.host:
        parser.error("--host, or --read a saved capture")
    return report(capture(args.host, f"ms={args.ms}&for={args.duration}",
                          args.out, args.limit))


if __name__ == "__main__":
    sys.exit(main())
