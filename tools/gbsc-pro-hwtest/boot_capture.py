#!/usr/bin/env python3
"""Capture the register state of a boot, from as early as the unit will answer.

    python3 tools/gbsc-pro-hwtest/boot_capture.py --label good-1

Power cycle the unit, then run this -- or run it first and power cycle while it
waits; it polls until the unit answers rather than assuming it is up.

Four power-ups on 2026-08-03 produced four different outcomes: the custom
preset, the PAL 1080p built-in, the right input with the wrong preset, and the
NTSC 1080p built-in with RGBHV scaling off. Working out why needs the state of
several boots, captured *before* anything is restored over the top -- which is
exactly what was missing that night.

Two captures per boot. The first is taken the instant HTTP answers, the second
after the sync watcher has had time to settle, because some of what goes wrong
happens during that settling rather than at reset.

All six segments, 1536 registers. dump_registers.py only covers 496 across five
segments -- fine for restoring, but it omits segment 2 entirely, so it cannot
answer questions about what a boot did.
"""

import argparse
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SNAPSHOTS = os.path.join(HERE, "snapshots")
SETTLE_SECONDS = 45


def answering(host, timeout=2.0):
    try:
        with urllib.request.urlopen(f"http://{host}/wifi/status", timeout=timeout) as r:
            return r.status == 200
    except (urllib.error.URLError, OSError):
        return False


def wait_for_boot(host, patience):
    """Block until the unit answers. Returns seconds waited, or None on timeout."""
    started = time.monotonic()
    was_up = answering(host)
    if was_up:
        print("unit is answering now -- power cycle it, waiting for it to go away...")
        while answering(host) and time.monotonic() - started < patience:
            time.sleep(0.5)
        if answering(host):
            print("it never went away; capturing the current state instead")
            return 0.0
        print("gone. waiting for it to come back...")

    while time.monotonic() - started < patience:
        if answering(host):
            return time.monotonic() - started
        time.sleep(0.5)
    return None


def capture(host, path, note):
    subprocess.run(
        [sys.executable, os.path.join(HERE, "snapdiff.py"),
         "--host", host, "--save", path, "--note", note],
        check=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--label", required=True,
                        help="what this boot was, e.g. good-1 or bad-nopicture-2")
    parser.add_argument("--patience", type=float, default=180.0,
                        help="seconds to wait for the unit to come back")
    args = parser.parse_args()

    waited = wait_for_boot(args.host, args.patience)
    if waited is None:
        print(f"the unit never answered within {args.patience:.0f}s", file=sys.stderr)
        return 1
    print(f"answering after {waited:.1f}s -- capturing immediately")

    early = os.path.join(SNAPSHOTS, f"boot-{args.label}-early.json")
    capture(args.host, early, f"boot '{args.label}': first answer, {waited:.1f}s after power")

    print(f"waiting {SETTLE_SECONDS}s for the sync watcher to settle...")
    time.sleep(SETTLE_SECONDS)

    settled = os.path.join(SNAPSHOTS, f"boot-{args.label}-settled.json")
    capture(args.host, settled, f"boot '{args.label}': {SETTLE_SECONDS}s after first answer")

    print(f"\ncompare two boots with:\n"
          f"  python3 {os.path.join('tools/gbsc-pro-hwtest', 'snapdiff.py')} --diff \\\n"
          f"      snapshots/boot-<a>-settled.json snapshots/boot-<b>-settled.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
