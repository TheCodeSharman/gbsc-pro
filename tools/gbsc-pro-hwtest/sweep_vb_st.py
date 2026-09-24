#!/usr/bin/env python3
"""Score `IF_VB_ST` on whether the input formatter's vertical reaches the pin.

Composite sync -- `SP_SOG_MODE` 1, which is `SYNC 1` on the RISC PC and sync on
green on the Wii -- refuses a small set of values for the vertical blanking
start. At one of them the blanking is never asserted, the picture wraps, and
FrameSync loses the input period it arms on.

    python3 tools/gbsc-pro-hwtest/sweep_vb_st.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/sweep_vb_st.py --host … --from 420 --to 524
    python3 tools/gbsc-pro-hwtest/sweep_vb_st.py --host … --values 511,512,513 --trials 5

`/testbus?if=3` puts that signal on `DEBUG_IN_PIN` and counts transitions on the
device, which is the only instrument that sees this: `STATUS_IF_VT_OK` reads 1
while the signal is dead, `VPERIOD_IF` reads the correct period, and
`/framesync`'s `ready` latches on first arming and stays true afterwards.

Selector 2 -- the VDS's output vsync, swept in the same pass -- is the control.
It says the pin and the pad are working, so a zero on selector 0 beside a live
selector 2 is the source signal and not the instrument.

The sweep restores `IF_VB_ST` on the way out, but the engine owns the field and
re-solves it on the next mode change, so nothing here is a lasting change.
../../docs/investigations/the-vertical-origin-follows-the-sync-type.md
"""
import argparse
import sys
import time

import setfield
from gbs_unit import Console, get, read_named

FIELD = "IF_VB_ST"

# 32 selectors at this window is about three seconds, and a 60 Hz signal gives
# ten transitions against a dead bus's zero. Shorter windows resolve the two
# just as well and leave less room to tell a partial reading from a dead one.
WINDOW_MS = 150

# Away and back between trials, so each reading is a fresh write of the value
# rather than a re-read of whatever the last one left latched.
PARKED = 400

SETTLE_S = 1.2


def probe(host, console, window_ms):
    """Transitions and duty for the input formatter's vertical and the control."""
    console.drain()
    status, body = get(host, f"/testbus?ms={window_ms}&if=3")
    if status == 404 or (status == 500 and not body.strip()):
        sys.exit("no /testbus on this firmware; it is behind GBS_DEBUG")

    rows, done = {}, False
    deadline = time.time() + 30
    while time.time() < deadline and not done:
        for line in console.collect(0.2):
            if line == "tb,done":
                done = True
            elif line.startswith("tb,") and not line.startswith("tb,header"):
                parts = line.split(",")
                if len(parts) >= 5:
                    rows[int(parts[1])] = (int(parts[2]), int(parts[3]),
                                           int(parts[4]))

    def one(selector):
        if selector not in rows:
            return (None, None)
        transitions, high, spins = rows[selector]
        return (transitions, 100.0 * high / spins if spins else 0.0)

    return one(0), one(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--from", dest="low", type=int, default=498)
    parser.add_argument("--to", dest="high", type=int, default=520)
    parser.add_argument("--values", help="comma-separated, instead of a range")
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--window-ms", type=int, default=WINDOW_MS)
    args = parser.parse_args()

    if args.values:
        values = [int(v) for v in args.values.split(",")]
    else:
        values = list(range(args.low, args.high + 1))

    fields = setfield.load_map()
    spec = fields[FIELD]
    console = Console(args.host)
    time.sleep(0.5)

    was = read_named(args.host, FIELD)
    print(f"# {FIELD} is {was}; restored on the way out")
    print(f"# {'value':>6} {'trial':>5} {'ifvert':>7} {'duty':>8} "
          f"{'vds':>5} {'duty':>8}")

    dead = {}
    try:
        for trial in range(args.trials):
            for value in values:
                if args.trials > 1:
                    setfield.apply(args.host, FIELD, spec, PARKED, False)
                    time.sleep(0.6)
                setfield.apply(args.host, FIELD, spec, value, False)
                time.sleep(SETTLE_S)

                (transitions, duty), (ctl, ctl_duty) = probe(
                    args.host, console, args.window_ms)
                flag = ""
                if transitions == 0:
                    dead.setdefault(value, 0)
                    dead[value] += 1
                    flag = "   <<< no vertical"
                print(f"  {value:>6} {trial + 1:>5} {transitions:>7} "
                      f"{duty:>7.2f}% {ctl:>5} {ctl_duty:>7.2f}%{flag}",
                      flush=True)
    finally:
        setfield.apply(args.host, FIELD, spec, was, False)
        console.close()

    if not dead:
        print("\nno value left the vertical dead")
        return
    print("\nvalues with no vertical (of "
          f"{args.trials} trial{'s' if args.trials != 1 else ''} each):")
    for value in sorted(dead):
        print(f"  {value}: {dead[value]}/{args.trials}")


if __name__ == "__main__":
    main()
