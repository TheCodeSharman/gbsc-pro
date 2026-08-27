#!/usr/bin/env python3
"""Capture register-write traces from a GBS_TRACE_WRITES build, over USB.

The trace is the equivalence oracle for branches no bench source can reach.
`trace_oracle.py` turns runs into an oracle and compares two; this is what gets
the runs off the unit.

**IT HAS TO BE USB.** The trace goes to hardware Serial and never to SerialM,
because broadcastTXT() allocates per frame and the build runs on ~21 KB of free
heap. Nothing about it is reachable over the network.

**WHICH ENTRY IS TRACED IS THE WHOLE POINT.** `--via post` runs
doPostPresetLoadSteps(), which is where the scaled standards do their work.
`--via apply` runs applyPresets(), and it is the only one that reaches either
bypass switch: standard 15 branches to bypassModeSwitch_RGBHV() and 5/6/7/13 to
setOutModeHdBypass(), neither of which doPostPresetLoadSteps() calls. A trace
taken through the wrong entry does not fail -- it silently contains none of the
writes being asked about.

    make -C build flash GBS_TRACE_WRITES=1 PORT=/dev/ttyUSB0
    python3 capture_traces.py --host <ip> --out traces/ --via post
    python3 capture_traces.py --host <ip> --out traces/ --via apply --standards 15,5,6,7,13

Opening the port resets the board, so the first load after connecting is given
time to boot.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial  # pyserial, from the repo's nix dev shell

from gbs_unit import get
from trace_oracle import oracle

BEGIN = "=== TRACE BEGIN"
END = "=== TRACE END"

# Every standard the forcing route accepts.
ALL_STANDARDS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15]


def read_one_trace(port, timeout):
    """The lines between one BEGIN and its END, or None if either never came.

    A run that was not delimited is discarded rather than truncated: a short
    trace and a complete one are indistinguishable once the marker is lost, and
    the oracle would take the short one as evidence of fewer writes.
    """
    lines, inside = [], False
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").strip()
        if line.startswith(BEGIN):
            lines, inside = [], True
            continue
        if line.startswith(END):
            return lines if inside else None
        if inside:
            lines.append(line)
    return None


def capture(host, port, standard, yuv, via, runs, timeout):
    out = []
    for _ in range(runs):
        port.reset_input_buffer()
        get(host, f"/trace/standard?std={standard}&yuv={yuv}&via={via}", timeout=8)
        trace = read_one_trace(port, timeout)
        if trace is None:
            return None
        out.append(trace)
        time.sleep(1.0)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True, help="directory to write fixtures into")
    ap.add_argument("--via", choices=("post", "apply"), default="post")
    ap.add_argument("--standards", default=None,
                    help="comma-separated; default is every one the route accepts")
    ap.add_argument("--yuv", default="0,1", help="comma-separated 0 and/or 1")
    ap.add_argument("--runs", type=int, default=3,
                    help="runs per branch. Two is the minimum that can tell a "
                         "stable write from a variable one")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="seconds to wait for one delimited trace")
    ap.add_argument("--captured", required=True,
                    help="the date to stamp fixtures with, e.g. 2026-08-27. "
                         "Passed in rather than taken from the clock so a "
                         "re-run of the same session does not re-date them")
    args = ap.parse_args()

    standards = ([int(s) for s in args.standards.split(",")]
                 if args.standards else ALL_STANDARDS)
    yuvs = [int(y) for y in args.yuv.split(",")]

    os.makedirs(args.out, exist_ok=True)
    with serial.Serial(args.port, args.baud, timeout=1.0) as port:
        print(f"{args.port} open -- the board resets on this, waiting for boot",
              flush=True)
        time.sleep(12.0)

        missed = []
        for standard in standards:
            for yuv in yuvs:
                runs = capture(args.host, port, standard, yuv, args.via,
                               args.runs, args.timeout)
                label = f"std{standard:02d}_yuv{yuv}"
                if args.via != "post":
                    label += f"_{args.via}"
                if runs is None:
                    missed.append(label)
                    print(f"{label}: NO DELIMITED TRACE -- skipped", flush=True)
                    continue

                built = oracle(runs)
                built.update(standard=standard, yuv=yuv, pal60=0,
                             via=args.via, captured=args.captured)
                path = os.path.join(args.out, f"{label}.json")
                with open(path, "w") as f:
                    json.dump(built, f, indent=1)
                print(f"{label}: {built['runLengths']} writes, "
                      f"{len(built['stable'])} stable, "
                      f"{built['variable']} variable -> {path}", flush=True)

    # Named, never silently dropped: a capture set that is short in a way nobody
    # is told about reads as covering branches it does not.
    if missed:
        print(f"\nNO TRACE FOR: {', '.join(missed)}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
