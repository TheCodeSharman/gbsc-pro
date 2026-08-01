#!/usr/bin/env python3
"""Watch what the scaler makes of the source, and catch the moment it changes.

    python3 tools/gbsc-pro-hwtest/mode_watch.py --host 192.168.88.108

Built for the ADFFS game-mode work. A RISC OS game programs VIDC directly rather
than going through the mode file, so switching from the desktop into a game
changes the input timings underneath the scaler. This records that transition:
what the timings were, what they became, and what the firmware did about it.

Polls a small register set and prints a block whenever the mode changes. On each
change it also fires /sc?, so printVideoTimings() dumps the full timing set to
the console — cheap polling, rich capture at the interesting moment.

Leave it running, start the game, quit back to the desktop, then Ctrl-C for a
summary of every distinct mode seen and how long each lasted.

Needs a GBS_DEBUG=1 build for the console half. Close the web UI first: each
client costs heap, though the server accepts five.
"""

import argparse
import sys
import time

from gbs_unit import Console, get, read_reg, read_word

# Timings wobble by a count or two while the sync processor tracks; anything
# inside this is the same mode, not a new one.
TOLERANCE = 3


def sample(host):
    """One reading of what the chip currently thinks it has. None if unreadable."""
    status = read_reg(host, 0, 0x16)
    if status is None:
        return None
    return {
        "status16": status,
        "htotal": read_word(host, 0, 0x17, 0x0FFF),
        "vtotal": read_word(host, 0, 0x1B, 0x07FF),
        "status00": read_reg(host, 0, 0x00),
        "preset": read_reg(host, 1, 0x2B),
    }


def changed(a, b):
    """Has the mode actually changed, as opposed to drifted?"""
    if a is None or b is None:
        return a is not b
    for key in ("status16", "status00", "preset"):
        if a[key] != b[key]:
            return True
    for key in ("htotal", "vtotal"):
        if a[key] is None or b[key] is None:
            if a[key] is not b[key]:
                return True
        elif abs(a[key] - b[key]) > TOLERANCE:
            return True
    return False


def describe(reading):
    status = reading["status16"]
    sync = (
        f"H={'act' if status & 0x02 else '---'}({'+' if status & 0x01 else '-'}) "
        f"V={'act' if status & 0x08 else '---'}({'+' if status & 0x04 else '-'})"
    )
    locked = "LOCKED" if (status & 0x0A) == 0x0A else "no lock"
    return (
        f"HTOTAL {reading['htotal']}  VTOTAL {reading['vtotal']}  "
        f"{sync}  {locked}\n"
        f"    STATUS_16 0x{status:02x}  STATUS_00 0x{reading['status00']:02x}  "
        f"preset ID 0x{reading['preset']:02x}"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--interval", type=float, default=0.5,
                    help="seconds between readings (default 0.5)")
    ap.add_argument("--log", help="also append every change to this file")
    ap.add_argument("--no-console", action="store_true",
                    help="skip the WebSocket, poll registers only")
    args = ap.parse_args()

    status, _ = get(args.host, "/wifi/status")
    if status != 200:
        print(f"error: {args.host} did not answer (status {status})", file=sys.stderr)
        return 2

    console = None
    if not args.no_console:
        try:
            console = Console(args.host)
        except Exception as e:  # noqa: BLE001 - no console is a degraded mode, not a failure
            print(f"note: no WebSocket console ({e}); registers only\n")

    log = open(args.log, "a", buffering=1) if args.log else None

    def emit(text):
        print(text, flush=True)
        if log:
            log.write(text + "\n")

    emit(f"# mode_watch {time.strftime('%Y-%m-%d %H:%M:%S')} host={args.host}")
    emit("watching for mode changes — start the game, quit back, then Ctrl-C\n")

    previous = None
    history = []  # (timestamp, reading)
    started = time.time()

    try:
        while True:
            reading = sample(args.host)
            if changed(previous, reading):
                now = time.time()
                if reading is None:
                    emit(f"{now - started:7.1f}s  -- unreadable (unit busy or resetting) --")
                else:
                    emit(f"{now - started:7.1f}s  {describe(reading)}")
                    if console:
                        console.drain()
                        get(args.host, "/sc?,")
                        for line in console.collect(2.0):
                            emit(f"           | {line}")
                history.append((now, reading))
                previous = reading
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass

    emit(f"\n\n=== {len(history)} mode change(s) over {time.time() - started:.0f}s ===")
    for index, (when, reading) in enumerate(history):
        until = history[index + 1][0] if index + 1 < len(history) else time.time()
        held = until - when
        if reading is None:
            emit(f"  {when - started:7.1f}s  +{held:6.1f}s  unreadable")
        else:
            emit(f"  {when - started:7.1f}s  +{held:6.1f}s  "
                 f"ht={reading['htotal']:<5} vt={reading['vtotal']:<5} "
                 f"status16=0x{reading['status16']:02x} preset=0x{reading['preset']:02x}")

    if console:
        console.close()
    if log:
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
