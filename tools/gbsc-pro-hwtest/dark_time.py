#!/usr/bin/env python3
"""How long the panel is dark across a source mode change, off the dongle.

    python3 tools/gbsc-pro-hwtest/dark_time.py "X640 Y480 C256 F60" [--settle 18]

Streams downscaled grey frames from the capture device with a timestamp each,
sends the MODE command itself so the two share one clock, and reports every
dark run relative to the command. The longest run is the answer: the sink
flickers dark before the real span, and a profile that stops at the first run
reports a tenth of a second for a transition of several. The instrument is the
dongle's own receiver, so what it measures is the dongle's re-acquisition and
not the television's. docs/investigations/the-transition-is-mostly-the-encoder.md
"""

import argparse
import socket
import subprocess
import sys
import threading
import time

import numpy as np

import hdmi_capture

MODESERV = ("192.168.88.10", 6502)
WIDTH, HEIGHT = 192, 108
FRAME_BYTES = WIDTH * HEIGHT

# Dark is well below the lit level rather than near black: the dongle's black
# frames are limited-range 16, and a card's mean is several times that.
DARK_FRACTION = 0.45


def dark_runs(samples, threshold):
    """(start, stop) of every run of samples below `threshold`, from a list of
    (time, mean) pairs in time order. A run still dark at the end closes at the
    last sample."""
    runs, dark_from = [], None
    for t, mean in samples:
        if mean < threshold and dark_from is None:
            dark_from = t
        if mean >= threshold and dark_from is not None:
            runs.append((dark_from, t))
            dark_from = None
    if dark_from is not None and samples:
        runs.append((dark_from, samples[-1][0]))
    return runs


def longest(runs):
    return max(runs, key=lambda r: r[1] - r[0]) if runs else None


def stream(dev):
    return subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "v4l2", "-input_format", "yuyv422",
         "-video_size", "1920x1080", "-i", dev, "-vf", f"scale={WIDTH}:{HEIGHT}",
         "-pix_fmt", "gray", "-fps_mode", "passthrough", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def read_mean(proc):
    buf = b""
    while len(buf) < FRAME_BYTES:
        chunk = proc.stdout.read(FRAME_BYTES - len(buf))
        if not chunk:
            return None
        buf += chunk
    return float(np.frombuffer(buf, np.uint8).mean())


def send(command, sent_at):
    sock = socket.create_connection(MODESERV, timeout=20)
    sock.sendall((command + "\n").encode())
    sent_at.append(time.monotonic())
    try:
        while sock.recv(4096):
            pass
    except OSError:
        pass
    sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("mode", help="the ModeServ mode string, as in 'X640 Y480 C256 F60'")
    parser.add_argument("--settle", type=float, default=18.0,
                        help="seconds to keep watching after the command")
    args = parser.parse_args()

    proc = stream(hdmi_capture.device())
    samples = []
    started = time.monotonic()
    while time.monotonic() - started < 3.0:
        mean = read_mean(proc)
        if mean is None:
            sys.exit("no frames from the capture device")
        samples.append((time.monotonic(), mean))
    lit = float(np.median([mean for _, mean in samples[-30:]]))
    threshold = DARK_FRACTION * lit

    sent_at = []
    threading.Thread(target=send, args=(f"MODE {args.mode}", sent_at), daemon=True).start()
    while not sent_at:
        samples.append((time.monotonic(), read_mean(proc)))
    command = sent_at[0]
    while time.monotonic() - command < args.settle:
        mean = read_mean(proc)
        if mean is None:
            break
        samples.append((time.monotonic(), mean))
    proc.kill()

    after = [(t - command, mean) for t, mean in samples if t >= command - 1.0]
    runs = dark_runs(after, threshold)
    rate = len(after) / (after[-1][0] - after[0][0])
    print(f"MODE {args.mode}: lit level {lit:.1f}, threshold {threshold:.1f}, {rate:.1f} frames/s")
    for start, stop in runs:
        print(f"  dark {start:+6.2f} s .. {stop:+6.2f} s  ({stop - start:.2f} s)")
    span = longest(runs)
    if span is None:
        print("  never went dark")
    else:
        print(f"  longest dark run {span[1] - span[0]:.2f} s,"
              f" lit again at {span[1]:+.2f} s after the command")


if __name__ == "__main__":
    main()
