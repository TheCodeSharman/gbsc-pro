#!/usr/bin/env python3
"""A/B the frame time lock against the picture holding still, UNFROZEN.

    python3 tools/gbsc-pro-hwtest/ftl_legs.py --host <ip> on off on off
    python3 tools/gbsc-pro-hwtest/ftl_legs.py --host <ip> --seconds 120 90 180 270 0

The composite residual the post coast closed was a roll of a line or two. What
is left is a jump of 100 to 250 rows that appears only UNFROZEN -- frozen legs
at `SP_POST_COAST` 4 read 0 of 6406 twice -- so it belongs to something that
only runs when the engine does, and the frame time lock is the one of those
that writes the display clock every two seconds.
docs/investigations/the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md

**THE SHARE CANNOT ANSWER THIS, THE HISTOGRAM CAN.** The jump is a handful of
frames in six thousand, so two legs differing by 4 and 7 frames differ by
nothing -- what separates them is the OFFSETS, a sub-line population against
one past a hundred rows. Every leg prints both.

A leg named by a NUMBER is a target phase instead, through `/framesync`. The
lock converges on its target and leaves a steady-state offset of the rate bias
over the loop gain -- 11300 ticks of 3194880 on this bench state, four raster
lines -- and that offset scales with the bias, reaching about 147 lines at the
500 ppm the rate agreement allows. So a phase sweep asks whether the read
pointer crossing the write pointer away from its target is what displaces the
picture. docs/investigations/the-frame-time-lock-saturates.md

**A PERMANENT DISPLACEMENT READS AS STILL.** The judge correlates each frame
against the median of its OWN clip, so a picture that sat 150 rows down for a
whole leg scores zero. Every leg is therefore also correlated against the FIRST
leg's profile, which is what a phase that moves the picture and keeps it there
shows up in. picture_stability

**`/sc?W` TOGGLES AND REPORTS NOTHING.** The state is read off the status
frame the websocket broadcasts, byte 4 bit 1, because the gate's console line
is printed on change only and a steady state says nothing at all. The toggle
is unpersisted, so the run leaves the preference alone.
"""

import argparse
import sys
import time

import coast_legs
import gbs_unit
import hdmi_capture
import picture_stability

# Where the status frame carries the frame time lock's option bit.
STATUS_OPTIONS_BYTE = 4
STATUS_FRAME_TIME_LOCK = 1 << 1

# Seconds the console is given to broadcast a status frame. One arrives about
# every second.
STATUS_WAIT_S = 4.0

# FrameSync::DefaultTargetPhase, which the run puts back: the target is held in
# RAM rather than saved, so nothing else restores it.
DefaultTargetPhase = 90

# Seconds between the toggle and the clip. Off, the display clock keeps the
# rate it was last steered to; on, the lock re-arms and takes a few passes to
# reach a correction, and a clip across either carries the transition.
SETTLE_S = 10.0

# Seconds a target phase change is given. The loop steers to the +-600 ppm clamp
# to traverse, so the picture is moving for as long as it takes to converge --
# measured at eleven corrections of 1670 ms for a 90 degree move.
PHASE_SETTLE_S = 35.0


def running(console):
    """Whether the frame time lock's option is on, off the status frame."""
    console.status.clear()
    waited = time.monotonic()
    while time.monotonic() - waited < STATUS_WAIT_S:
        if console.status:
            frame = console.status[-1]
            if len(frame) > STATUS_OPTIONS_BYTE:
                return bool(ord(frame[STATUS_OPTIONS_BYTE])
                            & STATUS_FRAME_TIME_LOCK)
        time.sleep(0.25)
    return None


def want(host, console, on, tries=3):
    """Toggle until the option reads as asked, or say it never did."""
    for _ in range(tries):
        at = running(console)
        if at is None:
            return None
        if at == on:
            return True
        gbs_unit.get(host, "/sc?W")
        time.sleep(1.0)
    return False


def apply_phase(host, degrees):
    status, body = gbs_unit.get(host, f"/framesync?phase={degrees}")
    if status != 200:
        sys.exit(f"/framesync?phase={degrees} answered {status}: {body}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--seconds", type=float, default=120.0)
    parser.add_argument("leg", nargs="+", help="on, off, or a target phase")
    args = parser.parse_args()

    for name in args.leg:
        if name not in ("on", "off") and not name.lstrip("-").isdigit():
            sys.exit(f"a leg is 'on', 'off' or a target phase, not {name!r}")

    dev = hdmi_capture.device()
    gbs_unit.freeze(args.host, False)
    console = gbs_unit.Console(args.host)
    try:
        anchor = None
        for name in args.leg:
            if name in ("on", "off"):
                reached = want(args.host, console, name == "on")
                if reached is None:
                    sys.exit("the console broadcast no status frame")
                if not reached:
                    print(f"{name:10} never reached", flush=True)
                    continue
                time.sleep(SETTLE_S)
                label = f"lock {name}"
            else:
                apply_phase(args.host, int(name))
                time.sleep(PHASE_SETTLE_S)
                label = f"phase {name}"
            rows = coast_legs.leg(args.host, dev, label, args.seconds,
                                  reference=anchor)
            if rows is not None and anchor is None:
                anchor = picture_stability.reference(rows)
    finally:
        apply_phase(args.host, DefaultTargetPhase)
        want(args.host, console, True)
        console.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
