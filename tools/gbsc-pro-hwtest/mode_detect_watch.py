#!/usr/bin/env python3
"""Watch whether Mode Detect's period measurement re-latches across a mode change.

    python3 tools/gbsc-pro-hwtest/mode_detect_watch.py --host 192.168.88.108

Start it, change the source, wait for the picture to settle, Ctrl-C.

It watches the interrupt status with it. `s0_0F` bit 3 is a latched "input source
switch the mode", every generator is enabled, and nothing in the firmware reads
it -- so whether it fires is the second question one source change can answer.

`IF_VPERIOD` is Mode Detect's own line count and would be the one measurement the
ADC divider does not colour -- which is what the engine needs, because
`STATUS_SYNC_PROC_VTOTAL` is wrong exactly when the divider is wrong and a
refusal cannot then replace the divider that caused it.
docs/investigations/divider-latched-measurement.md.

Polled on a settled source it is useless: steady to the read but uncorrelated
with the line count across states. The open question is whether the mode change
itself latches it, so the only sample that matters is the one taken while the
change is happening.

Segment 0 is read as ONE burst, because the question is whether VPERIOD moves
WITH SP_VTOTAL. Sampled separately they are many video lines apart and their
order says nothing.

A read that fails is recorded rather than dropped. Register access is deferred to
loop(), so a gap here is the firmware inside detection's own searches -- which
locates the transition rather than hiding it.

Nothing is frozen and nothing is written: the firmware's own handling of the
change is the thing under observation.
"""
import argparse
import datetime
import json
import os
import sys
import time

from gbs_unit import field_from, field_spec, get_json, read_segment

STATUS_FIRST, STATUS_LAST = 0x00, 0x1F

# Everything the question needs, all inside segment 0 so one burst holds them.
STATUS_FIELDS = {
    "VPERIOD_IF": (0x07, 1, 11),
    "HPERIOD_IF": (0x06, 0, 9),
    "SP_VTOTAL": (0x1B, 0, 11),
    "SP_HTOTAL": (0x17, 0, 12),
    "SP_HLOW_LEN": (0x19, 0, 12),
    "STATUS_16": (0x16, 0, 8),
    # s0_0F bit 3 is a LATCHED "input source switch the mode". Nothing in the
    # firmware reads it and nothing writes INT_RST_3, so if it ever latches it
    # stays latched -- which makes a 1 here "a mode change has happened", not
    # "one is happening".
    "INT_MODE_SWITCH": (0x0F, 3, 1),
    "INT_NO_SYNC": (0x0F, 4, 1),
    "INT_SOG_BAD": (0x0F, 0, 1),
    "INT_STATUS": (0x0F, 0, 8),
}

# Read only when something above moves, so the fast loop stays one request.
CONTEXT_FIELDS = {name: field_spec(name) for name in (
    "PLLAD_MD", "IF_HSYNC_RST", "IF_VB_SP", "IF_VB_ST", "IF_PRGRSV_CNTRL",
    "IF_LD_RAM_BYPS"
)}

ORDER = ["SP_VTOTAL", "VPERIOD_IF", "HPERIOD_IF", "SP_HTOTAL", "SP_HLOW_LEN",
         "STATUS_16", "INT_STATUS", "INT_MODE_SWITCH"]


def sample_status(host):
    bank = read_segment(host, 0, STATUS_FIRST, STATUS_LAST)
    if bank is None:
        return None
    return {name: field_from(bank, reg, off, width)
            for name, (reg, off, width) in STATUS_FIELDS.items()}


def sample_context(host):
    banks = {}
    out = {}
    for name, (seg, reg, off, width) in CONTEXT_FIELDS.items():
        if seg not in banks:
            banks[seg] = read_segment(host, seg, 0x00, 0x3F)
        bank = banks[seg]
        out[name] = None if bank is None else field_from(bank, reg, off, width)
    return out


def line(state):
    return "  ".join(f"{name}={state[name]}" for name in ORDER)


def verdict(samples):
    """Whether VPERIOD_IF moved when the source's line count did.

    The whole point of the run. A VPERIOD that never moves across a change of
    SP_VTOTAL cannot be the independent measurement, no matter how it reads on a
    settled source.
    """
    seen = [s for s in samples if s["status"]]
    if len(seen) < 2:
        return "not enough readings to say anything"

    counts = [s["status"]["SP_VTOTAL"] for s in seen]
    periods = [s["status"]["VPERIOD_IF"] for s in seen]
    if len(set(counts)) < 2:
        return ("SP_VTOTAL never changed, so no mode change was observed -- "
                "this run says nothing about the latch")

    switched = any(s["status"]["INT_MODE_SWITCH"] for s in seen)
    interrupt = (f"INT_STATUS bit 3 (mode switch) DID latch"
                 if switched else
                 f"INT_STATUS bit 3 (mode switch) never latched")

    moved = len(set(periods)) > 1
    if not moved:
        return (f"SP_VTOTAL went {counts[0]} -> {counts[-1]} and VPERIOD_IF held "
                f"{periods[0]} throughout: it does NOT re-latch on a mode change.\n"
                f"{interrupt}.")
    return (f"SP_VTOTAL went {counts[0]} -> {counts[-1]} and VPERIOD_IF moved "
            f"{sorted(set(periods))}: check whether it landed on the new count.\n"
            f"{interrupt}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--interval", type=float, default=0.0,
                        help="seconds between samples; the default is as fast as "
                             "the unit answers")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    out = args.out
    if out is None:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "sessions", f"mode-detect-{stamp}.json")

    if read_segment(args.host, 0, 0x00, 0x01) is None:
        sys.exit("no /getregs on this firmware, so segment 0 cannot be read as "
                 "one burst -- and separate reads cannot answer this")

    samples = []
    started = time.monotonic()
    previous = None
    failing = 0

    print("change the source now; Ctrl-C when the picture has settled\n")
    try:
        while True:
            at = round(time.monotonic() - started, 3)
            state = sample_status(args.host)
            record = {"at": at, "status": state}

            if state is None:
                failing += 1
                if failing == 1:
                    print(f"{at:8.3f}  -- no answer (the loop is busy) --")
            else:
                if failing:
                    print(f"{at:8.3f}  -- answering again after {failing} silent reads --")
                    failing = 0
                if state != previous:
                    print(f"{at:8.3f}  {line(state)}")
                    record["context"] = sample_context(args.host)
                    for name, value in record["context"].items():
                        print(f"{'':8}    {name}={value}")
                    previous = state

            samples.append(record)
            if args.interval:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print()
    finally:
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, "w") as handle:
            json.dump({"tool": "mode_detect_watch", "host": args.host,
                       "samples": samples}, handle, indent=2)
        print(f"{len(samples)} samples -> {out}")
        print(f"\n{verdict(samples)}")


if __name__ == "__main__":
    main()
