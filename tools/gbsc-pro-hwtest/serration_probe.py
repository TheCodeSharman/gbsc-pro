#!/usr/bin/env python3
"""Whether the source's vertical interval is SERRATED, read with no coast held.

    python3 tools/gbsc-pro-hwtest/serration_probe.py --host <ip>
    python3 tools/gbsc-pro-hwtest/serration_probe.py --host <ip> --samples 120

No register reports whether a composite interval carries serrations, and the
coast that covers them is one value wide because the sync type stands in for
the question. What CAN be asked is what the sync processor counts when nothing
is held over the interval: with the coast at 0/0 a serrated source's count
collapses and dithers while the input formatter's own vertical period -- which
counts half-lines, and so counts the serrations as lines either way -- stays
put. An unserrated source reads the same count at 0/0 as it does held.

So the measurement is the PAIR, read adjacently, with the coast at 0/0:
`STATUS_SYNC_PROC_VTOTAL` against `VPERIOD_IF`, and how steady each is.

**THE COAST IS RESTORED ON THE WAY OUT.** At 0/0 sync on green drops to
`absent` with the sync pad away and the output black, so this is a probe and
not a state to leave a unit in.
"""

import argparse
import statistics
import sys
import time

import gbs_unit

PAIR = ["STATUS_SYNC_PROC_VTOTAL", "VPERIOD_IF", "STATUS_IF_VT_OK", "PLLAD_MD"]

# Seconds between applying a coast and the first sample. /coast re-applies the
# sync type and re-solves, so a sample taken across that carries the transition.
SETTLE_S = 10.0


def coast(host, pre, post):
    query = "clear=1" if pre is None else f"pre={pre}&post={post}"
    status, body = gbs_unit.get(host, f"/coast?{query}")
    if status != 200:
        sys.exit(f"/coast?{query} answered {status}: {body}")


def samples(host, count, interval):
    out = []
    for _ in range(count):
        seen = gbs_unit.read_fields(host, PAIR)
        if seen:
            out.append((seen["STATUS_SYNC_PROC_VTOTAL"], seen["VPERIOD_IF"],
                        seen["STATUS_IF_VT_OK"], seen["PLLAD_MD"]))
        time.sleep(interval)
    return out


def report(name, taken):
    if not taken:
        print(f"{name:12} no samples", flush=True)
        return
    counts = [a for a, _, _, _ in taken]
    periods = [b for _, b, _, _ in taken]
    ok = sum(1 for _, _, c, _ in taken if c)
    dividers = sorted({d for _, _, _, d in taken})
    spread = max(counts) - min(counts)
    median = statistics.median(counts)
    period = statistics.median(periods)
    ratio = period / median if median else 0.0
    halves = period / 2.0
    print(f"{name:12} count {median:6.1f} spread {spread:3d} over "
          f"{len(set(counts))} values   period {period:6.1f} spread "
          f"{max(periods) - min(periods):3d}   vt_ok {ok}/{len(taken)}   "
          f"period/count {ratio:5.3f}   count-period/2 "
          f"{median - halves:+6.1f}", flush=True)
    print(f"{'':12} counts  {sorted(set(counts))}", flush=True)
    print(f"{'':12} periods {sorted(set(periods))}  dividers {dividers}",
          flush=True)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--interval", type=float, default=0.1)
    args = parser.parse_args()

    at = gbs_unit.read_fields(args.host, ["SP_PRE_COAST", "SP_POST_COAST"]) or {}
    print(f"held coast {at.get('SP_PRE_COAST')}/{at.get('SP_POST_COAST')}",
          flush=True)
    try:
        report("held", samples(args.host, args.samples, args.interval))
        coast(args.host, 0, 0)
        time.sleep(SETTLE_S)
        report("no coast", samples(args.host, args.samples, args.interval))
    finally:
        coast(args.host, None, None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
