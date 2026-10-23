#!/usr/bin/env python3
"""Record everything the console says, with timestamps, reconnecting on drop.

    python3 tools/gbsc-pro-hwtest/console_record.py --host 192.168.88.108 \
        --out /tmp/console.log --for 7200

**NOTHING ON THE BOARD RETAINS THE CONSOLE, WHICH IS WHY AN UNREPEATABLE EVENT
IS LOST.** `SerialMirror` broadcasts live and keeps nothing, and `/bootlog` is
compiled out at `BOOTLOG_BYTES=0` -- so a fault someone provokes at the bench
while no capture is attached cannot be read afterwards, however cheap the
console is to watch. A session that means to diagnose a bench event starts this
first and asks questions second.

It reconnects, because a restart or a dropped link otherwise ends the recording
silently part way through the thing being recorded.
"""

import argparse
import sys
import time

try:
    import websocket
except ImportError:
    websocket = None


class Quiet(Exception):
    """A recv that had nothing to say, as against one that lost the link."""


def _connect(host):
    if websocket is None:
        sys.exit("websocket-client is not installed -- enter the dev shell")
    ws = websocket.create_connection(f"ws://{host}:81",
                                     subprotocols=["arduino"], timeout=5)
    ws.settimeout(1)

    def recv():
        try:
            return ws.recv()
        except websocket.WebSocketTimeoutException:
            raise Quiet()

    return recv, ws.close


def record(host, out, run_s, now=time.time, connect=_connect, pause=time.sleep):
    """Append timestamped console lines to `out` until `run_s` has passed.

    `connect` returns a (recv, close) pair and is injected so a test can drive
    this without a unit. A recv raising Quiet had nothing to say; anything else
    means the link went, and the outer loop takes a new one.
    """
    started = now()
    kept = 0
    out.write(f"=== recording {host} ===\n")
    while now() - started < run_s:
        try:
            recv, close = connect(host)
        except OSError:
            pause(2)
            continue
        out.write(f"{now() - started:9.2f}  --- console attached ---\n")
        while now() - started < run_s:
            try:
                line = recv().strip()
            except Quiet:
                continue
            except Exception:
                break
            if line:
                out.write(f"{now() - started:9.2f}  {line}\n")
                out.flush()
                kept += 1
        close()
    return kept


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--for", dest="run_s", type=float, default=7200.0)
    args = parser.parse_args()

    with open(args.out, "a", buffering=1, encoding="utf-8") as out:
        kept = record(args.host, out, args.run_s)
    print(f"{kept} lines to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
