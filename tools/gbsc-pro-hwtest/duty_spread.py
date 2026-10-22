#!/usr/bin/env python3
"""How far the measured hsync duty spreads, which bounds SyncWidthIdentity.

    python3 tools/gbsc-pro-hwtest/duty_spread.py --host <ip>
    python3 tools/gbsc-pro-hwtest/duty_spread.py --host <ip> --samples 2000

`SourceKey::SyncWidthIdentity` is how far two duty readings may sit apart and
still be one source. It cannot be narrowed below what the measurement itself
moves, and the floor has two parts that this separates.

**THE QUANTISATION IS THE FLOOR AND IT IS NOT SMALL.** The duty is a count of
ADC samples over the divider, so one sample is one step -- 4.5 ten-thousandths
at a divider of 2200 and 8.3 at 1212. A tolerance admitting the plus or minus
one a healthy counter shows therefore costs twice that before any other noise,
so the step is reported beside the spread rather than left implicit.

**THE READING IS TAKEN ONCE PER SOLVE, so the within-window spread is a bound
and not the answer.** What the key varies by is the spread across solves, of
which this measures one. `SourceMeasurement::takeDuty()` requires an agreeing
pair, so a single outlier here is refused by the engine rather than keyed.

**A WIDE STATUS FIELD TEARS ACROSS ITS TWO BYTES.** STATUS_SYNC_PROC_HLOW_LEN
is 12 bits read in one transaction that is not atomic at the chip, so a value
whose high and low bytes are each another observed value's is reported
separately rather than counted. On composite the vertical interval's broad pulse
lands in the same register and is excluded by the pulse band, not by a guess.
docs/investigations/a-wide-status-field-tears-across-its-two-bytes.md
"""

import argparse
import collections
import time
import urllib.request

import websocket

# HsyncPulse::PulseFloorPerThousand and PulseCeilingPerThousand, which is what
# the firmware accepts as a sync pulse at all.
PULSE_FLOOR = 10
PULSE_CEILING = 152

# SourceKey::SyncWidthIdentity, as the key stores it.
IDENTITY = 20


def samples(host, ms, duration):
    """(divider, hlow_len) per on-device sample, read off the console.

    Queued from the process holding the socket: a queue sent before the socket
    is attached loses the window, and one sent during a previous run answers
    `already running`.
    """
    link = websocket.create_connection(
        f"ws://{host}:81", subprotocols=["arduino"], timeout=5)
    link.settimeout(0.5)
    for _ in range(20):
        reply = urllib.request.urlopen(
            f"http://{host}/samplinglog?ms={ms}&for={duration}", timeout=8).read().decode()
        if "queued" in reply:
            break
        time.sleep(1.0)
    else:
        raise SystemExit(f"the sampling log would not start: {reply.strip()}")

    out = []
    deadline = time.time() + duration / 1000.0 + 25
    while time.time() < deadline:
        try:
            line = link.recv().strip()
        except Exception:  # noqa: BLE001 - a timeout is just a quiet console
            continue
        if line.startswith("smp,done"):
            break
        if not line.startswith("smp,") or "header" in line:
            continue
        field = line.split(",")
        if len(field) < 7:
            continue
        try:
            out.append((int(field[2]), int(field[6])))
        except ValueError:
            continue
    link.close()
    return out


# How much rarer than its donors a crossing is. The byte test alone cannot
# answer it: if two genuine values cross, the crossings donate their bytes
# straight back, so each genuine value also has both its bytes present
# elsewhere and the rule fires on all four. What separates them is the rate --
# the pair is the measurement and the crossings are the few reads that landed
# inside an update.
TORN_RARER_THAN = 10


def torn(value, seen):
    """Whether `value` looks like a read that crossed a mid-update byte.

    True where its high byte comes from one other observed value, its low byte
    from another, and both of those are at least TORN_RARER_THAN times as
    common as this one.
    """
    count = seen[value]
    donors_high = [v for v in seen if v != value and (v & 0xFF00) == (value & 0xFF00)]
    donors_low = [v for v in seen if v != value and (v & 0x00FF) == (value & 0x00FF)]
    if not donors_high or not donors_low:
        return False
    strongest = min(max(seen[v] for v in donors_high),
                    max(seen[v] for v in donors_low))
    return count * TORN_RARER_THAN <= strongest


def source_samples(low, divider):
    """SyncProcessor::hsyncSourceSamples() -- the narrower of the pulse and its
    complement, so a high-active source counted through the inversion reports
    the pulse rather than the line minus it."""
    if divider <= low:
        return low
    complement = divider - low
    return low if low < complement else complement


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--ms", type=int, default=25)
    parser.add_argument("--samples", type=int, default=1200)
    args = parser.parse_args()

    duration = args.ms * args.samples
    rows = samples(args.host, args.ms, duration)
    if not rows:
        raise SystemExit("no samples -- is another capture holding the console?")

    raw = collections.Counter(low for _, low in rows)
    dividers = collections.Counter(divider for divider, _ in rows)
    divider = dividers.most_common(1)[0][0]

    duties = []
    crossings = collections.Counter()
    outside = 0
    for row_divider, low in rows:
        if torn(low, raw):
            crossings[low] += 1
            continue
        pulse = source_samples(low, row_divider)
        per_ten_thousand = 10000.0 * pulse / row_divider if row_divider else 0.0
        if not PULSE_FLOOR * 10 <= per_ten_thousand <= PULSE_CEILING * 10:
            outside += 1
            continue
        duties.append(per_ten_thousand)

    print(f"{len(rows)} samples, divider {divider}"
          f"{' (NOT STEADY: ' + str(dict(dividers)) + ')' if len(dividers) > 1 else ''}")
    print(f"  hlow_len      {dict(raw.most_common(6))}")
    if crossings:
        print(f"  torn reads    {dict(crossings)} -- excluded, both bytes seen elsewhere")
    if outside:
        print(f"  outside band  {outside} of {len(rows)} -- not a pulse, excluded")
    if not duties:
        raise SystemExit("every reading was excluded; nothing to report")

    low, high = min(duties), max(duties)
    step = 10000.0 / divider
    mean = sum(duties) / len(duties)
    spread = sum((d - mean) ** 2 for d in duties) / len(duties)
    print(f"  duty          {len(duties)} readings, "
          f"{low:.1f}..{high:.1f} ten-thousandths, mean {mean:.1f}, "
          f"sd {spread ** 0.5:.2f}")
    print(f"  p2p           {high - low:.1f}")
    print(f"  one sample    {step:.1f}  ({step / (high - low) * 100:.0f}% of the "
          f"p2p)" if high > low else f"  one sample    {step:.1f}")
    print(f"  SyncWidthIdentity {IDENTITY} admits "
          f"{IDENTITY / step:.1f} samples of dither")
    print(f"  the floor this leg supports is {high - low:.1f}, "
          f"against {IDENTITY} in force")


if __name__ == "__main__":
    main()
