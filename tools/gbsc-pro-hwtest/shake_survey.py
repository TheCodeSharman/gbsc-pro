"""Reboot the unit N times and score, per boot, the rate it matched and how
steady the picture is.

**THE SHAKE IS A PROPERTY OF THE BOOT, NOT OF THE UNIT.** The output field rate
is set once per solve by externalClockGenSyncInOutRate(), from a source rate two
samples agreed on -- and Clock::RateAgreement admits a pair 0.5 Hz apart, which
is 0.83% at 60 Hz. A boot whose one-shot lands wrong beats against the source
for as long as it runs; a boot whose one-shot lands right is steady. Measured on
the RISC PC at 800x600@60: three boots of six shook, and one of them logged
`rate match: source 60801 mHz` against a source running 60317.

So a single observation says nothing, and "it looked fine when I checked" is not
evidence. Survey a handful of boots and read the spread.

**THE HORIZONTAL COLUMN IS THE CONTROL AND THE MEASUREMENT IS WORTHLESS WITHOUT
IT.** A 30 fps camera against a 60 Hz panel beats, and rolling shutter adds its
own wobble, so an absolute figure says nothing. Horizontal edge position shares
every one of those errors. A boot where only the vertical figure rises is the
finding; both rising together is the camera or the room.

**--lock NEEDS A LONG SETTLE.** FrameSync corrects at most 0.06% per 1.67 s, so
walking back a one-shot that landed 0.8% out takes minutes -- and while it walks,
the output rate is moving, which is itself a shake. Measured: clipped 34 s after
arming, every boot scored WORSE than with the lock off; clipped at 180 s, 0.004.

Needs tv-snap (the bench camera), ffmpeg and numpy.

    python3 shake_survey.py --host <ip> --boots 6
    python3 shake_survey.py --host <ip> --boots 6 --lock --settle 180
"""
import argparse
import re
import subprocess
import tempfile
import threading
import time

import numpy as np
import websocket

from gbs_unit import get, get_json

# What externalClockGenSyncInOutRate() matched, in milli-hertz. Whole hertz
# cannot see a match that is 0.8% out at 60 Hz, which is the whole fault.
RATE_MATCH = re.compile(
    r"rate match: source (\d+) mHz, output (\d+) mHz, clock (\d+) -> (\d+)")

# The clip is scored at this size. Small enough that one row is several panel
# rows, which is what averages the sensor noise out of the edge position.
WIDTH, HEIGHT = 480, 270


def edge_positions(path):
    """Per frame, where the strongest luma gradient sits along each axis.

    Collapsing to one profile per axis first is what makes this a POSITION and
    not a frame difference: a difference measures displacement multiplied by
    local contrast, reads zero across a flat block, and puts the whole score on
    the test card's frequency wedge.
    """
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-vf", f"scale={WIDTH}:{HEIGHT}",
         "-pix_fmt", "gray", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE, check=True).stdout
    frames = len(raw) // (WIDTH * HEIGHT)
    if frames == 0:
        raise SystemExit(f"{path} decoded to no frames")
    pixels = np.frombuffer(raw, np.uint8)[: frames * WIDTH * HEIGHT]
    pixels = pixels.reshape(frames, HEIGHT, WIDTH).astype(np.float32)

    def centroid(profiles):
        out = []
        for profile in profiles:
            gradient = np.abs(np.diff(profile))
            peak = int(np.argmax(gradient))
            low, high = max(0, peak - 3), min(len(gradient), peak + 4)
            window = gradient[low:high]
            out.append(low + float((window * np.arange(len(window))).sum() / window.sum()))
        return np.array(out)

    return centroid(pixels.mean(axis=2)), centroid(pixels.mean(axis=1)), frames


class ConsoleTail:
    """The console, reconnecting: a restart drops the socket and the line worth
    having is printed seconds after it comes back."""

    def __init__(self, host):
        self.host = host
        self.lines = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self):
        link = None
        while not self._stop.is_set():
            if link is None:
                try:
                    link = websocket.create_connection(
                        f"ws://{self.host}:81", subprotocols=["arduino"], timeout=2)
                    link.settimeout(0.5)
                except Exception:  # noqa: BLE001 - the unit is rebooting
                    time.sleep(0.3)
                    continue
            try:
                frame = link.recv().strip()
                if not frame.startswith("#"):
                    self.lines.append(frame)
            except websocket.WebSocketTimeoutException:
                pass
            except Exception:  # noqa: BLE001 - dropped, reconnect
                link = None

    def close(self):
        self._stop.set()
        self._thread.join(timeout=2)


def acquired(host):
    status, payload = get_json(host, "/geometry", timeout=3)
    return status == 200 and payload is not None and payload.get("state") == "acquired"


def survey_one(host, settle, arm_lock, clip_seconds, directory):
    get(host, "/restart", timeout=5)
    time.sleep(6)

    console = ConsoleTail(host)
    try:
        deadline = time.time() + 90
        while time.time() < deadline and not acquired(host):
            time.sleep(1)

        if arm_lock:
            time.sleep(2)
            get(host, "/sc?W", timeout=5)  # RAM only, so the flash is left alone
        time.sleep(settle)
        matches = [RATE_MATCH.search(line) for line in console.lines]
        matches = [m for m in matches if m]
    finally:
        console.close()

    clip = f"{directory}/shake-{int(time.time())}.mp4"
    subprocess.run(["tv-snap", "--clip", str(clip_seconds), "-o", clip, "-q"],
                   stdout=subprocess.DEVNULL, check=True)
    vertical, horizontal, frames = edge_positions(clip)
    return matches[-1] if matches else None, vertical, horizontal, frames


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--boots", type=int, default=6)
    parser.add_argument("--settle", type=float, default=30.0,
                        help="seconds after acquisition before the clip. With "
                             "--lock this wants 180: the lock is still walking "
                             "the rate in before then, which is itself a shake")
    parser.add_argument("--lock", action="store_true",
                        help="arm the frame time lock once acquired, with /sc?W, "
                             "which writes no flash")
    parser.add_argument("--clip", type=float, default=6.0)
    parser.add_argument("--dir", default=tempfile.gettempdir())
    args = parser.parse_args()

    print(f"{'boot':>4}  {'source mHz':>10}  {'output mHz':>10}  {'clock':>10}  "
          f"{'vert sd':>8}  {'horiz sd':>8}  {'vert span':>9}")
    for boot in range(1, args.boots + 1):
        match, vertical, horizontal, _ = survey_one(
            args.host, args.settle, args.lock, args.clip, args.dir)
        source = match.group(1) if match else "-"
        output = match.group(2) if match else "-"
        clock = match.group(4) if match else "-"
        print(f"{boot:>4}  {source:>10}  {output:>10}  {clock:>10}  "
              f"{vertical.std():>8.3f}  {horizontal.std():>8.3f}  "
              f"{vertical.max() - vertical.min():>9.2f}", flush=True)


if __name__ == "__main__":
    main()
