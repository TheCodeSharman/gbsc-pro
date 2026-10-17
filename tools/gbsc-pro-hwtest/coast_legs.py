#!/usr/bin/env python3
"""A/B the vertical-interval coast against the picture holding still, UNFROZEN.

    python3 tools/gbsc-pro-hwtest/coast_legs.py --host <ip> 0/0 default 0/0 default
    python3 tools/gbsc-pro-hwtest/coast_legs.py --host <ip> --seconds 100 2/6 7/6

**NOT `roll_ab.py`, WHICH FREEZES AND POKES THE REGISTER.** The coast changes
the measured line count, a changed count arms a solve, and the solve writes the
coast again -- so a frozen leg measures the pair with the consequence it has for
the count held out of the reading. `/coast?pre=&post=` holds the override in the
engine, re-applies the sync type in force and re-solves, which is the pair
reaching the chip the way a firmware change would deliver it.

**AND THE WII CANNOT BE JUDGED FROZEN AT ALL.** Freezing on sync on green takes
`STATUS_SYNC_PROC_VTOTAL` to 97..135 and times nothing on either test bus, so
the one source that can say whether a coast suits a serrated interval is out of
a frozen instrument's reach. docs/known-issues.md

**THE RESIDUAL IS RARE, SO A SHORT LEG CANNOT SEPARATE ZERO FROM THE FLOOR.**
The events come every 5 to 12 s and last about four frames, so a 20 s leg holds
four of them and a run of them reads 0.0% by chance -- measured, with controls
reading 0.0% on two legs of their own. A leg is 100 s.

**AND A SHARE OF ZERO IS NOT A PICTURE.** The judge correlates row profiles and
reads a band of unwritten memory as a still frame, so each leg carries a frame's
mean luma and black margins beside its share. `picture_stability`.

Alternate the treatments, because the share drifts inside one acquisition.
docs/known-issues.md
"""

import argparse
import sys
import time

import gbs_unit
import hdmi_capture
import picture_stability
import roll_ab

# The coast pair as it reached the chip, the count, the sync width, and the
# divider -- because the POST coast gates the input formatter's vertical, which
# is the signal the field rate is timed on, so a post coast moves the
# measurement and the solve that follows it.
WITNESSES = ["SP_PRE_COAST", "SP_POST_COAST", "STATUS_SYNC_PROC_VTOTAL",
             "STATUS_SYNC_PROC_HLOW_LEN", "PLLAD_MD"]

# Seconds between the re-solve the route asks for and the clip. The engine
# re-applies the sync type and re-measures, and a clip taken across that carries
# the transition rather than the state.
SETTLE_S = 10.0

# How many consecutive reads of an unmoved capture say the solve has settled,
# and how long to wait for them.
SETTLED_READS = 3
SETTLE_LIMIT_S = 60.0


def settled(host, limit_s=SETTLE_LIMIT_S, holds=SETTLED_READS):
    """Wait for the engine to be acquired with the capture it solved holding.

    **NOT `gbs_unit.acquired_and_settled`, WHICH ALSO KEYS ON `lineRateHz`.**
    That is a MEASUREMENT of the source and it dithers on sync on green, so the
    Wii never repeats it and the wait expires on a source that has been
    acquired throughout. What says a solve has finished is the capture it
    produced standing still.
    """
    started, last, held, seen = time.monotonic(), None, 0, []
    while time.monotonic() - started < limit_s:
        got = gbs_unit.get_json(host, "/geometry")[1]
        if got and got.get("state") == "acquired":
            now = (got.get("ch"), got.get("cv"))
            held = held + 1 if now == last else 0
            last = now
            if held >= holds:
                return True
        else:
            held, last = 0, None
        seen.append(f"{(got or {}).get('state')} {(got or {}).get('ch')}x"
                    f"{(got or {}).get('cv')}")
        time.sleep(1.0)
    print(f"{'':10} gave up on: {' | '.join(seen[-8:])}", flush=True)
    return False


def apply_coast(host, leg):
    """Put the engine on this coast, or back on the one it computes."""
    query = "clear=1" if leg == "default" else \
        "pre={}&post={}".format(*leg.split("/"))
    status, body = gbs_unit.get(host, f"/coast?{query}")
    if status != 200:
        sys.exit(f"/coast?{query} answered {status}: {body}")


def leg(host, dev, name, seconds, reference=None):
    """One clip, judged and reported. The row profiles, or None.

    `reference` is another leg's profile, so a picture that moved and STAYED
    moved is reported too -- the judge correlates each frame against the median
    of its own clip, where a whole-leg displacement is the median.
    """
    if not settled(host):
        print(f"{name:10} never settled", flush=True)
        return None
    rows, span = picture_stability.clip_over(dev, seconds)
    if rows is None:
        print(f"{name:10} no frames", flush=True)
        return None
    judged = picture_stability.judge(rows, span)
    at = gbs_unit.get_json(host, "/geometry")[1] or {}
    solved = f"{at.get('ch')}x{at.get('cv')} {at.get('lineRateHz')}Hz"
    seen = gbs_unit.read_fields(host, WITNESSES) or {}
    witness = " ".join(f"{k.lower().replace('status_sync_proc_', '')} {v}"
                       for k, v in seen.items())
    print(f"{name:10} share {judged.share * 100:5.2f}%  displaced "
          f"{judged.displaced:3d}+{judged.lost} lost of {judged.frames:4d}  "
          f"{judged.per_second:5.2f} jumps/s  worst {judged.worst:4d}  "
          f"{judged.seconds:5.1f} s  {solved}  {witness}  "
          f"{roll_ab.look(dev)}", flush=True)
    moved = picture_stability.displacement(
        picture_stability.reference(rows), reference) if reference is not None \
        else 0
    print(f"{'':10} offsets {picture_stability.histogram(rows)}  "
          f"against the first leg {moved}", flush=True)
    return rows


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--seconds", type=float, default=100.0)
    parser.add_argument("leg", nargs="+", help="PRE/POST, or 'default'")
    args = parser.parse_args()

    dev = hdmi_capture.device()
    gbs_unit.freeze(args.host, False)
    try:
        for name in args.leg:
            apply_coast(args.host, name)
            time.sleep(SETTLE_S)
            leg(args.host, dev, name, args.seconds)
    finally:
        apply_coast(args.host, "default")
    return 0


if __name__ == "__main__":
    sys.exit(main())
