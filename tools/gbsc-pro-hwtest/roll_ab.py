#!/usr/bin/env python3
"""A/B/A one register, or a group, against the picture holding still.

    python3 tools/gbsc-pro-hwtest/roll_ab.py --host <ip> SP_PRE_COAST=2 SP_DLT_REG=1024
    python3 tools/gbsc-pro-hwtest/roll_ab.py --host <ip> IF_PRGRSV_CNTRL=1,IF_LD_SEL_PROV=1

Automation is frozen for the run. Each treatment is written by name, read
back, scored with `picture_stability.judge` on a clip, and restored, and a
CONTROL clip is taken between every treatment, because the share of displaced
frames drifts between 4% and 29% inside one acquisition on the composite
bench state: a treatment is judged against its neighbours, and only a share
near zero is a result.

**A SHARE OF ZERO IS NOT A VERDICT ON ITS OWN.** The judge correlates row
profiles, so a band of unwritten memory a hundred rows high reads as a still
picture -- measured with the H coast stop past the line, 0 of 514 displaced
beside a frame nobody would call a picture. Look at a frame before believing
a treatment, and read the count and the sync width beside it.
docs/investigations/the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md
"""

import argparse
import sys
import time

import gbs_unit
import hdmi_capture
import picture_stability

WITNESSES = ["STATUS_IF_INP_PAL_INT", "STATUS_SYNC_PROC_VTOTAL",
             "STATUS_SYNC_PROC_HLOW_LEN"]


def treatments(args):
    """Each argument as a list of (name, value), a comma joining a group."""
    out = []
    for arg in args:
        pairs = []
        for item in arg.split(","):
            name, value = item.split("=")
            pairs.append((name, int(value, 0)))
        out.append(pairs)
    return out


def clip(host, dev, seconds, label):
    rows, span = picture_stability.clip_over(dev, seconds)
    if rows is None:
        print(f"{label:48} no frames", flush=True)
        return None
    judged = picture_stability.judge(rows, span)
    seen = gbs_unit.read_fields(host, WITNESSES) or {}
    witness = " ".join(f"{k.split('_')[-1].lower()} {v}" for k, v in seen.items())
    print(f"{label:48} share {judged.share * 100:5.1f}%  jumps/s "
          f"{judged.per_second:5.2f}  worst {judged.worst:4d}  lost "
          f"{judged.lost:3d}/{judged.frames}  {witness}", flush=True)
    return judged


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--settle", type=float, default=2.0,
                        help="seconds between a write and its clip")
    parser.add_argument("treatment", nargs="+", help="NAME=VALUE[,NAME=VALUE]")
    args = parser.parse_args()

    dev = hdmi_capture.device()
    if not gbs_unit.freeze(args.host, True):
        sys.exit("the engine did not freeze")
    try:
        clip(args.host, dev, args.seconds, "control")
        for pairs in treatments(args.treatment):
            was = {name: gbs_unit.read_named(args.host, name) for name, _ in pairs}
            for name, value in pairs:
                gbs_unit.write_named(args.host, name, value)
            time.sleep(args.settle)
            now = {name: gbs_unit.read_named(args.host, name) for name, _ in pairs}
            label = ",".join(f"{n}={v}" for n, v in pairs)
            clip(args.host, dev, args.seconds,
                 f"{label} (was {list(was.values())} reads {list(now.values())})")
            for name, _ in pairs:
                gbs_unit.write_named(args.host, name, was[name])
            time.sleep(args.settle)
            clip(args.host, dev, args.seconds, "control")
    finally:
        gbs_unit.freeze(args.host, False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
