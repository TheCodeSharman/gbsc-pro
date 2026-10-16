#!/usr/bin/env python3
"""Whether the emitted picture HOLDS STILL over time, or jumps down the frame.

    python3 tools/gbsc-pro-hwtest/picture_stability.py
    python3 tools/gbsc-pro-hwtest/picture_stability.py --control --host <ip>

The composite picture jumps vertically several times a second. Every other
instrument here is blind to it by construction: `card_edges` collapses a clip
with `np.maximum` ACROSS FRAMES so the card's flashing ring cannot hide a
border, which erases a transient before anything judges it, and
`hdmi_capture.borders` reads one frame. This is the first thing here that
measures the picture over time.

**IT IS A ROLL AND NOT A BAR.** Horizontal black bars are what the fault looks
like, and counting dropped rows finds them -- but the rows are black because
the picture MOVED and left them behind, so a band count scores the few jumps
big enough to black out a mid-picture row and misses the rest. Measured on
`vga` at 320x256@50 into 1080p, one clip of 60 frames: 5 frames carry a band
and 21 are displaced. The displacement is the fault; the band is its shadow.

**THE JUMPS ARE WHOLE SOURCE LINES.** 1080 output rows over 256 active source
lines is 4.22 rows a line, and the measured offsets are 2, 4 and 8 rows -- half
a line, one line, two -- with occasional jumps of 107 and 214, which are 25 and
51 lines. The picture rolls as a WHOLE: correlating the top, middle and bottom
thirds separately gives one offset, so it is not a tear.

**A SUB-ROW FIGURE IS `picture_jitter.py`'s QUESTION, NOT THIS ONE.** That one
resolves thousandths of a column to catch a shimmer, with a lag range of 3. A
214-row roll is off the end of its search and reads there as unbracketed.

The reference each frame is judged against is the MEDIAN profile over the clip,
so no separate clean capture is needed -- a jump a few times a second cannot
move a median. **WHICH IS ALSO THE LIMIT: A PICTURE DISPLACED IN MOST FRAMES
READS STILL**, the displaced position becoming the median. That one is a
framing fault and `hdmi_capture.borders` reads it off a single frame.

**NEVER A MEAN.** A few-times-a-second event averages into nothing, so what is
reported is the share of frames displaced, the rate the offset CHANGES, and the
worst offset.

**A SHARE OF ZERO IS NOT A PICTURE.** The correlation reads a band of
unwritten memory a hundred rows high as a still frame, because the rest of
the frame correlates at lag 0 -- measured with the H coast stop past the
line, 0 of 514 displaced beside a frame that was mostly magenta noise. A
treatment that reads zero is looked at before it is believed. `roll_ab.py`.
"""

import argparse
import collections
import subprocess
import sys
import time

import numpy as np

import gbs_unit
import hdmi_capture

# How far down the frame a roll is looked for. The measured jumps reach 214
# rows, so a search sized for a shimmer finds the wrong peak and reports a
# large roll as a small one.
MAXLAG = 260

# A frame's profile has to vary by this much to carry a verdict. The dongle is
# itself a sink: a frame it drops or re-sends black has no feature to correlate
# and the lag read off one is noise, which counted as a displacement makes the
# capture's own losses read as an unsteady board.
CONTRAST = 4.0

# What share of frames may be displaced before the picture is not holding
# still. **THE FLOOR IS MEASURED, NOT ASSUMED**: on `vga` at 320x256@50 into
# 1080p, separate sync reads lag 0 on every frame of every run -- 0.00 over
# four runs of 60 -- where composite reads 0.17 to 0.53. The allowance is for
# the dongle, not for the board.
STEADY_SHARE = 0.05

# `displaced` and `lost` are frame counts, `share` is the displaced fraction,
# `events` is how many times the offset CHANGED and `per_second` that against
# the measured span, `worst` is the largest offset either way, and `why` is
# None where the picture held still enough.
Stability = collections.namedtuple(
    "Stability",
    "frames displaced lost share events per_second worst seconds why")


def row_luma(clip):
    """A clip's mean luma per row, as (frames, rows).

    A roll moves every column alike, so a row's mean carries it at a
    thousandth of the data and anything localised averages away.
    """
    if clip.ndim == 4:
        return np.array([_luma(frame).mean(axis=1) for frame in clip], np.float32)
    return clip.astype(np.float32).mean(axis=2)


def _luma(rgb):
    return rgb.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)


def reference(rows):
    """The profile the picture holds, as the median over frames."""
    return np.median(np.asarray(rows, np.float32), axis=0)


def displacement(profile, ref, maxlag=MAXLAG, contrast=CONTRAST):
    """How far down the frame `profile` sits against `ref`, in rows.

    Positive is the picture moved DOWN. None is a frame with no feature to
    correlate, which is the capture's loss rather than the board's movement.

    Normalised by the OVERLAP at each lag, because a raw correlation is longest
    at lag 0 and so prefers it -- which reads a rolled frame as a still one.
    """
    profile = np.asarray(profile, np.float32)
    ref = np.asarray(ref, np.float32)
    if profile.std() < contrast or ref.std() < contrast:
        return None
    a, b = profile - profile.mean(), ref - ref.mean()
    count = len(b)
    lags = np.arange(-(count - 1), count)
    scores = np.correlate(a, b, "full") / (count - np.abs(lags))
    within = np.abs(lags) <= maxlag
    return int(lags[within][int(np.argmax(scores[within]))])


def displacements(rows, maxlag=MAXLAG, contrast=CONTRAST):
    """Each frame's offset against the profile the clip holds."""
    rows = np.asarray(rows, np.float32)
    ref = reference(rows)
    return [displacement(profile, ref, maxlag, contrast) for profile in rows]


def judge(rows, seconds, maxlag=MAXLAG, contrast=CONTRAST, limit=STEADY_SHARE):
    """What a clip's row profiles say about the picture holding still.

    `seconds` is the clip's own measured span, which the frame count cannot
    give: the capture carries no timestamps of its own, so a rate has to be
    timed around the read.
    """
    rows = np.asarray(rows, np.float32)
    offsets = displacements(rows, maxlag, contrast)
    lost = sum(1 for lag in offsets if lag is None)
    moved = [lag for lag in offsets if lag]
    # **A FRAME WITH NO PICTURE IN IT IS A FRAME THE PICTURE WAS ABSENT FOR**,
    # which is unsteady by any reading -- so it is counted WITH the displaced
    # ones rather than merely left out of the denominator. Subtracted instead,
    # a run that blanked for a tenth of its frames passed as steady, and a run
    # that was black throughout passed as the steadiest there has ever been.
    share = (len(moved) + lost) / len(offsets) if offsets else 0.0
    events, held = 0, 0
    for lag in offsets:
        if lag is None:
            continue
        if lag and lag != held:
            events += 1
        held = lag
    span = max(seconds, 1e-6)
    worst = max(moved, key=abs) if moved else 0
    why = None
    if not offsets or lost == len(offsets):
        why = (f"no picture to judge: all {len(offsets)} frames carry nothing "
               f"to correlate. ASK THE BOARD FIRST -- a block left held in "
               f"reset emits nothing while every register reads correct")
    elif share > limit:
        absent = f" and was absent in {lost}" if lost else ""
        why = (f"the picture moved in {len(moved)} of {len(offsets)} frames"
               f"{absent} ({share * 100:.0f}%, against {limit * 100:.0f}%), "
               f"{events / span:.2f} jumps a second, worst {worst} rows")
    return Stability(len(offsets), len(moved), lost, round(share, 3), events,
                     round(events / span, 2), worst, round(seconds, 2), why)


# Columns the frame is area-averaged to before it is read. 1920/32 is 60
# exactly, so the mean of the strip's columns IS the mean of the frame's. A
# roll moves every column alike and nothing about it needs the full stride,
# which keeps a span long enough to RATE an event affordable in memory.
STRIP = 32

# Frames discarded before the clip starts. The dongle emits black while its
# receiver settles and every one of those would score as a lost frame.
# `hdmi_capture.WARMUP` is the same figure for the same reason.
WARMUP = 40


def stream(dev, width=STRIP, height=1080):
    """ffmpeg streaming area-averaged rows, for reading frame by frame.

    **NOT `hdmi_capture.frames()`, WHICH CARRIES NO TIMESTAMPS.** A rate needs
    the span the frames actually arrived over, and a capture read in one block
    cannot give it -- nor can the frame count, the dongle's delivered rate not
    being its nominal one. `dark_time.py` reads the capture the same way.
    """
    return subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "v4l2",
         "-input_format", "yuyv422", "-video_size", "1920x1080", "-i", dev,
         "-vf", f"scale={width}:{height}:flags=area", "-pix_fmt", "gray",
         "-fps_mode", "passthrough", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def read_rows(proc, width=STRIP, height=1080):
    """One frame's mean per row, or None where the stream ended."""
    want, buf = width * height, b""
    while len(buf) < want:
        chunk = proc.stdout.read(want - len(buf))
        if not chunk:
            return None
        buf += chunk
    return np.frombuffer(buf, np.uint8).reshape(height, width).mean(axis=1)


def clip_over(dev, seconds, warmup=WARMUP):
    """Row profiles for `seconds` of consecutive frames, and the span taken.

    The span is MEASURED rather than derived from the frame count, so a dongle
    delivering fewer frames than it claims cannot inflate a rate.
    """
    proc = stream(dev)
    try:
        for _ in range(warmup):
            if read_rows(proc) is None:
                return None, 0.0
        rows, started = [], None
        while True:
            frame = read_rows(proc)
            if frame is None:
                break
            at = time.monotonic()
            if started is None:
                started = at
            rows.append(frame)
            if at - started >= seconds:
                return np.array(rows, np.float32), at - started
        return (np.array(rows, np.float32), 0.0) if rows else (None, 0.0)
    finally:
        proc.kill()


def reading(judged):
    """One leg's verdict as a report prints it."""
    return (f"{judged.displaced} of {judged.frames} frames displaced "
            f"({judged.share * 100:.0f}%), {judged.per_second:.2f} jumps/s, "
            f"worst {judged.worst} rows, {judged.lost} lost, "
            f"{judged.seconds:.1f} s")


# The one mode both sync types carry, and what the source is left showing. A
# leg is measured on ONE source mode: a rate scored across two is two pictures
# averaged.
BENCH_MODE = "X320 Y256 C256 F50"
MODESERV = "192.168.88.10"

# A sync-type change leaves the engine on the path it held, so a leg pays for
# the re-probe and for the source leaving and returning before it measures
# anything. docs/sync-type-selection.md
SETTLE_SECONDS = 14.0


def on_sync_type(host, where, csync):
    """Put the source on this sync arrangement and the engine on the same one.

    `SP_SOG_MODE` read back is the only witness that the path in force is the
    one the source is sending, and `/sc?~` is what makes the engine re-probe at
    all. A leg measured through the other path is a leg measured on the other
    sync type. docs/sync-type-selection.md
    """
    gbs_unit.set_sync_type(where, csync)
    gbs_unit.get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)
    if not gbs_unit.acquired_and_settled(host, limit_s=90.0):
        return None
    gbs_unit.show_card(where)
    return gbs_unit.read_named(host, "SP_SOG_MODE")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=8.0,
                        help="span per leg; long enough to RATE a "
                             "few-per-second event, not only to see one")
    parser.add_argument("--host", default=None,
                        help="needed by --control, which re-probes the engine")
    parser.add_argument("--modeserv", default=MODESERV)
    parser.add_argument("--control", action="store_true",
                        help="measure composite AND separate sync in one run, "
                             "separate being the floor composite is read against")
    parser.add_argument("--limit", type=float, default=STEADY_SHARE)
    args = parser.parse_args()

    if args.control and not args.host:
        sys.exit("--control changes the sync type, so it needs --host")
    dev = hdmi_capture.device()
    legs = ((True, "composite"), (False, "separate")) if args.control \
        else ((None, "as it stands"),)

    print(f"{dev}  {args.seconds:.0f} s per leg  "
          f"limit {args.limit * 100:.0f}% of frames")
    found = {}
    for csync, name in legs:
        if csync is not None:
            sog = on_sync_type(args.host, args.modeserv, csync)
            if sog is None:
                print(f"  {name:12} SKIP  never acquired, or never settled")
                continue
            print(f"  {name:12} SP_SOG_MODE {sog}")
        rows, span = clip_over(dev, args.seconds)
        if rows is None:
            print(f"  {name:12} SKIP  no frames from the capture device")
            continue
        judged = judge(rows, span, limit=args.limit)
        found[name] = judged
        print(f"  {name:12} {'FAIL' if judged.why else 'ok  '}  {reading(judged)}")
        if judged.why:
            print(f"               {judged.why}")

    # THE FLOOR IS THE POINT OF THE CONTROL. A rate on its own carries the
    # dongle's own contribution and says nothing about the board.
    if "separate" in found and "composite" in found:
        print(f"\ncomposite {found['composite'].share * 100:.0f}% of frames "
              f"against a separate-sync floor of "
              f"{found['separate'].share * 100:.0f}%")
    return 1 if any(judged.why for judged in found.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
