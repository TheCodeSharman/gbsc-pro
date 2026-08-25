#!/usr/bin/env python3
"""Watch HPERIOD_IF across real source mode changes, with the firmware frozen.

    python3 tools/gbsc-pro-hwtest/hperiod_sweep.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/hperiod_sweep.py --host <ip> --mdf <path-to-AKF50>

Run `ModeSweep` on the RISC PC at the same time -- RiscPc repo,
`tools/video-source/`. That cycles the source through the stock AKF50 modes,
15.6-37.9 kHz; this samples the scaler and reports, per mode, whether
HPERIOD_IF is usable.

Pass --mdf a **stock Acorn mode file** (AKF50 and friends, under
!Boot.Resources.Configure.Monitors.Acorn). A hand-authored file predicts only the
machine it was written for -- see the provenance note in mdf_modes.py.

**Freezes the firmware first** (`/freeze?on=1`), and this is the point of the
tool rather than a convenience. Unfrozen, a source mode change makes the ESP load
a preset, which moves PLLAD_MD and rewrites most of the chip -- so a reading that
moved could be the source, the preset, or the ESP racing the sampler. Frozen,
nothing writes a TV5725 register but this script, so whatever HPERIOD_IF does is
the chip responding to the source. The freeze is released on the way out,
including on Ctrl-C.

Modes are identified by STATUS_SYNC_PROC_VTOTAL, not by agreement with the RISC
PC. The mode list in ModeSweep has a distinct VTOTAL per mode precisely so
that works: no clock sync, no shared ordering, and a skipped mode on one side
cannot silently mislabel samples on the other.

Deliberately imports nothing from gbs_unit, so it runs outside the nix dev shell
-- gbs_unit pulls in websocket-client at import time, and a sweep that runs for
several minutes is exactly the thing you want to start without ceremony.
"""

import argparse
import json
import re
import sys
import time
import urllib.request
from collections import OrderedDict

# HPERIOD_IF is 9-bit: 0 and 511 are the rails, and tv5725-chip.md records that
# both are perfectly stable, so "it did not move" is not a validity test.
RAILS = (0, 511)

# The value is source H total / 4 counted at 27 MHz (docs/tv5725-chip.md), so one
# count is 4 ticks = 148 ns.
COUNT_NS = 4 / 27.0 * 1000


def read_segment(host, segment, timeout=8):
    url = f"http://{host}/getregs?s={segment}"
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return bytes.fromhex(json.load(response)["values"])


def sample(host):
    """One observation: (VTOTAL, HPERIOD_IF, VPERIOD_IF, PLLAD_MD)."""
    s0 = read_segment(host, 0)
    s5 = read_segment(host, 5)
    return (
        s0[0x1B] | ((s0[0x1C] & 0x07) << 8),
        s0[0x06] | ((s0[0x07] & 0x01) << 8),
        (s0[0x07] >> 1) | (s0[0x08] << 7),
        s5[0x12] | ((s5[0x13] & 0x0F) << 8),
    )


def freeze(host, on):
    """Returns the flag the unit reports back, or None if it has no /freeze."""
    url = f"http://{host}/freeze?on={1 if on else 0}"
    try:
        with urllib.request.urlopen(url, timeout=8) as response:
            return json.load(response).get("frozen")
    except Exception:
        return None


def line_period_us(hperiod):
    return hperiod * COUNT_NS / 1000.0


def mdf_expectations(path):
    """{VTOTAL: (label, line period us)} from a RISC OS mode file.

    Only for labelling the report. A prediction that disagrees with a healthy
    reading is interesting; one that disagrees with a railed reading is not.
    """
    text = open(path, encoding="latin-1").read()
    out = {}
    for block in re.findall(r"startmode(.*?)endmode", text, re.S):
        fields = dict(re.findall(r"^\s*(\w+):(.*)", block, re.M))
        if "h_timings" not in fields or "v_timings" not in fields:
            continue
        try:
            h = [int(n) for n in fields["h_timings"].split(",")]
            v = [int(n) for n in fields["v_timings"].split(",")]
            rate_khz = float(fields["pixel_rate"])
        except ValueError:
            continue
        htotal, vtotal = sum(h), sum(v)
        if not htotal or not vtotal:
            continue
        period = htotal / rate_khz * 1000.0
        # The scaler reads one line less than the mode file, consistently.
        out.setdefault(vtotal - 1, (f"{fields.get('x_res')}x{fields.get('y_res')}", period))
    return out


def summarise(runs, expected):
    print()
    print(f"{'VTOTAL':>7} {'mode':>12} {'n':>4} {'HPERIOD':>16} {'railed':>7} "
          f"{'implied us':>11} {'mdf us':>8} {'verdict':>9}")
    print("-" * 86)
    for vtotal, values in runs.items():
        railed = sum(1 for v in values if v in RAILS)
        healthy = [v for v in values if v not in RAILS]
        label, mdf_us = expected.get(vtotal, ("?", None))
        distinct = sorted(set(healthy))
        shown = ",".join(str(d) for d in distinct[:3]) + ("…" if len(distinct) > 3 else "")
        if not healthy:
            verdict = "RAILED"
            implied = ""
        else:
            spread = max(healthy) - min(healthy)
            verdict = "ok" if railed == 0 and spread <= 1 else "unstable"
            implied = f"{line_period_us(sum(healthy) / len(healthy)):.2f}"
        print(f"{vtotal:>7} {label:>12} {len(values):>4} {shown:>16} "
              f"{railed:>3}/{len(values):<3} {implied:>11} "
              f"{(f'{mdf_us:.2f}' if mdf_us else ''):>8} {verdict:>9}")
    print()
    print("  'ok' means zero railed samples and at most one count of spread.")
    print("  One count is 148 ns, so a healthy reading is good to about +/-0.15 us.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True)
    ap.add_argument("--seconds", type=float, default=300.0,
                    help="how long to watch (default 300)")
    ap.add_argument("--interval", type=float, default=0.4,
                    help="seconds between samples (default 0.4)")
    ap.add_argument("--mdf", help="stock Acorn mode file (AKF50 etc), to label "
                                  "modes and predict the line period. Do not pass "
                                  "a hand-authored file: it predicts only the "
                                  "machine it was written for.")
    ap.add_argument("--no-freeze", action="store_true",
                    help="do not freeze the firmware. Leaves preset loads in the "
                         "measurement, which is the thing this tool exists to remove.")
    args = ap.parse_args()

    expected = mdf_expectations(args.mdf) if args.mdf else {}

    if not args.no_freeze:
        state = freeze(args.host, True)
        if state is not True:
            print(f"error: could not freeze {args.host} (/freeze said {state!r}). "
                  "Needs a build with /freeze support; pass --no-freeze to run "
                  "anyway, knowing preset loads will be in the data.", file=sys.stderr)
            return 2
        print(f"frozen. Watching for {args.seconds:.0f}s -- start ModeSweep now.")
    else:
        print(f"NOT frozen. Watching for {args.seconds:.0f}s.")

    runs = OrderedDict()
    previous = None
    deadline = time.time() + args.seconds
    try:
        while time.time() < deadline:
            try:
                vtotal, hperiod, vperiod, pllad = sample(args.host)
            except Exception:
                time.sleep(args.interval)
                continue
            runs.setdefault(vtotal, []).append(hperiod)
            if vtotal != previous:
                label = expected.get(vtotal, ("?", None))[0]
                print(f"  [{time.strftime('%H:%M:%S')}] VTOTAL {vtotal:>5} ({label})"
                      f"  HPERIOD {hperiod:>4}  VPERIOD {vperiod:>5}  PLLAD {pllad:>5}")
                previous = vtotal
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        if not args.no_freeze:
            freeze(args.host, False)
            print("unfrozen.")

    summarise(runs, expected)
    return 0


if __name__ == "__main__":
    sys.exit(main())
