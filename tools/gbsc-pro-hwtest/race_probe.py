#!/usr/bin/env python3
"""Reproduce a register write landing in the wrong segment.

    python3 tools/gbsc-pro-hwtest/race_probe.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/race_probe.py --host <ip> --frozen

The TV5725 has one 256-register window and a pointer at 0xF0 selecting which of
six banks it shows, so every access is two steps: aim, then read or write. On
2026-08-09 a `pytest --source` run left segment 1 holding segment 3's bytes at
the same offsets -- `IF_HB_ST` read 1348, which is `VDS_DIS_HB_ST` -- and
segment 3 holding segment 1's back. `IF_HB_ST` 71 units past the end of a 1277
unit line is the zigzag on every vertical edge that took a nine round bisect.

That fault took a 134 second suite run to hit once, which is not a tool. This
reproduces it in under a minute, and its value is that it ISOLATES: the three
ingredients can be removed one at a time, and doing so is what ruled out the two
explanations that looked right.

Measured on 2026-08-09, each for 60 s:

    --frozen, no pad presses                        clean
    running, no pad presses                         clean
    running, pad presses                            IF_HB_ST -> 1282

So it is not the async HTTP handlers colliding with each other, and not the
sync watcher: it needs the geometry engine's burst of writes from loop() while
a whole-segment read is in flight. It also survived making the segment pointer
re-aim on every single access, which is how we know the defect is not a stale
cache of where the pointer last pointed -- something moves it BETWEEN the aim
and the access, and re-aiming more often cannot close that.

Ingredients, all three needed:
  * pad presses, so the geometry engine writes segments 1 and 3 in bursts
  * whole-segment reads, which hold the handler in I2C for 256 registers
  * a watcher, because the damage is silent until something reads it back
"""

import argparse
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import get, read_reg, read_segment, write_reg  # noqa: E402


# IF_HB_ST, set 0 horizontal blanking start: s1 0x10 plus bits 2-0 of 0x11. The
# register the zigzag was bisected to, and the one the stray writes landed in.
WATCHED = (1, 0x10, 0x11, 0x07)

# The segment the readers hammer. 3 is the VDS, which the engine writes on every
# press alongside segment 1 -- the pair the bench fault actually swapped.
NOISY_SEGMENT = 3

# A value that is inside the 1277 unit line, so anything else is the finding.
SANE = 2


def watched_value(host):
    segment, low, high, mask = WATCHED
    lo = read_reg(host, segment, low)
    hi = read_reg(host, segment, high)
    if lo is None or hi is None:
        return None
    return lo | ((hi & mask) << 8)


def run(host, frozen, seconds, presses, readers):
    get(host, f"/freeze?on={1 if frozen else 0}")
    time.sleep(0.5)

    segment, low, high, _ = WATCHED
    write_reg(host, segment, low, SANE)
    write_reg(host, segment, high, 0xF0)

    stop = threading.Event()
    found = []

    def hammer():
        while not stop.is_set():
            read_segment(host, NOISY_SEGMENT)

    def press():
        keys = ["z", "h", "+", "-"]
        i = 0
        while not stop.is_set():
            get(host, f"/sc?{keys[i % len(keys)]}")
            i += 1
            time.sleep(0.25)

    def watch():
        while not stop.is_set():
            value = watched_value(host)
            if value is not None and value != SANE:
                found.append(value)
                stop.set()

    threads = [threading.Thread(target=hammer, daemon=True) for _ in range(readers)]
    if presses:
        threads.append(threading.Thread(target=press, daemon=True))
    threads.append(threading.Thread(target=watch, daemon=True))

    started = time.monotonic()
    for thread in threads:
        thread.start()
    while time.monotonic() - started < seconds and not stop.is_set():
        time.sleep(0.25)
    stop.set()
    for thread in threads:
        thread.join(timeout=10.0)

    elapsed = time.monotonic() - started
    if frozen:
        get(host, "/freeze?on=0")

    if found:
        print(f"\n  IF_HB_ST was written to {found[0]} after {elapsed:.0f}s -- "
              f"a write aimed at segment {segment} landed elsewhere, or one "
              f"aimed elsewhere landed here\n")
        return 1
    print(f"\n  no corruption in {elapsed:.0f}s\n")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True, help="the unit, e.g. 192.168.88.108")
    parser.add_argument("--frozen", action="store_true",
                        help="freeze the firmware first, so loop() writes nothing")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--no-presses", dest="presses", action="store_false",
                        help="leave the geometry engine idle")
    parser.add_argument("--readers", type=int, default=3,
                        help="threads hammering whole-segment reads")
    args = parser.parse_args(argv)
    return run(args.host, args.frozen, args.seconds, args.presses, args.readers)


if __name__ == "__main__":
    sys.exit(main())
