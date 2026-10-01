#!/usr/bin/env python3
"""Walk the menu the remote drives and photograph each step.

    python3 tools/gbsc-pro-hwtest/osd_walk.py --host <ip> --out /tmp/new \
        menu down ok down

Keys go in through /ir, which injects a frame at the receiver, so they reach
the menu by exactly the path a real press takes, and each step is reported
with the page the menu says it is on.

Each step writes <out>/NN-<key>.png off the USB HDMI capture, which is the
emitted frame rather than a photograph of the panel. The same sequence run
before and after a change is what makes two states comparable.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

KEYS = ("menu", "up", "down", "left", "right", "ok", "exit",
        "info", "save", "mute", "volup", "voldown")


def get(host, path, timeout=5):
    url = f"http://{host}{path}"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as reply:
            body = reply.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        raise SystemExit(f"{url}: {error.code} {error.read().decode()!r}")
    except OSError as error:
        raise SystemExit(f"{url}: {error}")
    try:
        return json.loads(body)
    except ValueError:
        return body


def capture(png, frames, settle):
    time.sleep(settle)
    result = subprocess.run(
        [sys.executable, os.path.join(HERE, "hdmi_capture.py"),
         "--frames", str(frames), "--png", png],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"capture failed: {result.stderr.strip()}")
    for line in result.stdout.splitlines():
        if "frame 0" in line:
            return line.strip()
    return result.stdout.strip().splitlines()[-1] if result.stdout else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", required=True,
                        help="directory for the frames; created if absent")
    parser.add_argument("--frames", type=int, default=2)
    parser.add_argument("--settle", type=float, default=1.5,
                        help="seconds between a key and its frame")
    parser.add_argument("keys", nargs="+", help=" ".join(KEYS))
    args = parser.parse_args()

    for key in args.keys:
        if key not in KEYS:
            raise SystemExit(f"unknown key {key!r}; one of {' '.join(KEYS)}")

    os.makedirs(args.out, exist_ok=True)

    # The menu holds its own state, so a walk starts from closed.
    get(args.host, "/ir?key=exit")
    time.sleep(0.5)

    try:
        for step, key in enumerate(args.keys):
            get(args.host, f"/ir?key={key}")
            png = os.path.join(args.out, f"{step:02d}-{key}.png")
            margins = capture(png, args.frames, args.settle)
            page = get(args.host, "/menu")
            rows = [r["label"] + (f" = {r['value']}" if r["value"] else "")
                    for r in page["rows"]]
            said = f"  depth {page['depth']}  {rows}"
            print(f"{step:02d} {key:8s} {margins}{said}", flush=True)
    finally:
        get(args.host, "/ir?key=exit")
        time.sleep(0.5)

    print(f"\n{len(args.keys)} frames in {args.out}")


if __name__ == "__main__":
    main()
