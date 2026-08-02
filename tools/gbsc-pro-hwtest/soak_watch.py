#!/usr/bin/env python3
"""Log what the scaler sees, for unattended soak tests.

    python3 tools/gbsc-pro-hwtest/soak_watch.py \
        --host 192.168.88.108 --log ~/soak-nevryon.log

The RISC PC's aborts are the thing under test, but the scaler is a usable witness:
a crash, a wedge or a mode change shows up here as sync dropping or VTOTAL moving,
with a timestamp. That turns "it aborted at some point overnight" into "it aborted
at 03:14", which is the difference between an anecdote and evidence.

Logs every change, plus a heartbeat so a silent log can be told from a dead
script. Read-only — it never writes a register.
"""

import argparse
import time

from gbs_unit import read_reg, read_word


def sample(host):
    status = read_reg(host, 0, 0x16)
    if status is None:
        return None
    return {
        "status16": status,
        "vtotal": read_word(host, 0, 0x1B, 0x07FF),
        "htotal": read_word(host, 0, 0x17, 0x0FFF),
        "preset": read_reg(host, 1, 0x2B),
        "lock": bool((read_reg(host, 0, 0x09) or 0) & 0x80),
    }


def describe(s):
    if s is None:
        return "NO ANSWER from the unit"
    st = s["status16"]
    return (f"status16 0x{st:02x} "
            f"H={'act' if st & 2 else '---'}({'+' if st & 1 else '-'}) "
            f"V={'act' if st & 8 else '---'}({'+' if st & 4 else '-'})  "
            f"VTOTAL {s['vtotal']}  HTOTAL {s['htotal']}  "
            f"preset {s['preset'] if s['preset'] is None else hex(s['preset'])}  "
            f"PLL {'locked' if s['lock'] else 'UNLOCKED'}")


def significant(a, b):
    """A change worth logging, as opposed to the last digit wobbling."""
    if (a is None) != (b is None):
        return True
    if a is None:
        return False
    if a["status16"] != b["status16"] or a["preset"] != b["preset"]:
        return True
    if a["lock"] != b["lock"]:
        return True
    for key in ("vtotal", "htotal"):
        x, y = a[key], b[key]
        if (x is None) != (y is None):
            return True
        if x is not None and abs(x - y) > 3:
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--interval", type=float, default=5.0)
    ap.add_argument("--heartbeat", type=float, default=900.0,
                    help="seconds between 'still here' lines (default 15 min)")
    args = ap.parse_args()

    last = None
    last_beat = 0.0
    changes = 0

    with open(args.log, "a", buffering=1, encoding="utf-8") as log:
        def write(tag, text):
            log.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')}  {tag:9s} {text}\n")

        write("START", f"host={args.host} interval={args.interval}s")
        first = sample(args.host)
        write("BASELINE", describe(first))
        last = first

        while True:
            time.sleep(args.interval)
            now = sample(args.host)
            if significant(last, now):
                changes += 1
                write("CHANGE", describe(now))
                last = now
                last_beat = time.monotonic()
                continue
            if time.monotonic() - last_beat >= args.heartbeat:
                write("ok", f"{describe(now)}   ({changes} changes so far)")
                last_beat = time.monotonic()


if __name__ == "__main__":
    main()
