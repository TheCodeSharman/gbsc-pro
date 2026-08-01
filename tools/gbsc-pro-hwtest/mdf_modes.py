#!/usr/bin/env python3
"""Read a RISC OS mode file (MDF) and say what the scaler should see for each mode.

    python3 tools/gbsc-pro-hwtest/mdf_modes.py path/to/RetroScale
    python3 tools/gbsc-pro-hwtest/mdf_modes.py path/to/RetroScale --match 320x256

An MDF states a mode as a pixel rate plus pixel/line counts. The scaler reports
STATUS_SYNC_PROC_VTOTAL in *lines*, so the vtotal computed here is directly
comparable to what mode_watch.py reads back — which is what makes it a usable
prediction rather than a curiosity.

HTOTAL is not comparable in the same way. The scaler samples on its own PLL
locked to HSync, so its HTOTAL is in *its* clock units, not the source's pixel
clock. Compare line and field rates instead, or the ratio.

Timing fields are !MakeModes order:
    h_timings: sync, back porch, left border, display, right border, front porch
    v_timings: sync, back porch, top border, display, bottom border, front porch
sync_pol is a bitfield: bit 0 inverts HSync, bit 1 inverts VSync.
"""

import argparse
import re
import sys


def parse(path):
    """Every startmode/endmode block in the file, as a list of dicts."""
    modes, current = [], None
    with open(path, encoding="latin-1") as handle:
        for line in handle:
            line = line.strip()
            if line == "startmode":
                current = {}
                continue
            if line == "endmode":
                if current is not None:
                    modes.append(current)
                current = None
                continue
            if current is None or ":" not in line or line.startswith("#"):
                continue
            key, _, value = line.partition(":")
            current[key.strip()] = value.strip()
    return modes


def totals(mode):
    """(htotal in pixels, vtotal in lines), or None if the block is incomplete."""
    try:
        h = [int(n) for n in mode["h_timings"].split(",")]
        v = [int(n) for n in mode["v_timings"].split(",")]
    except (KeyError, ValueError):
        return None
    if len(h) != 6 or len(v) != 6:
        return None
    return sum(h), sum(v)


def polarity(mode):
    try:
        bits = int(mode.get("sync_pol", 0))
    except ValueError:
        return "?"
    return f"H{'-' if bits & 1 else '+'} V{'-' if bits & 2 else '+'}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mdf")
    ap.add_argument("--match", help="only modes whose resolution contains this, e.g. 320x256")
    args = ap.parse_args()

    try:
        modes = parse(args.mdf)
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    if not modes:
        print(f"no mode blocks found in {args.mdf}", file=sys.stderr)
        return 1

    print(f"{args.mdf}: {len(modes)} modes\n")
    print(f"{'resolution':>12}  {'pixel clk':>10}  {'htotal':>6}  {'VTOTAL':>6}  "
          f"{'line rate':>10}  {'field':>7}  sync")
    print(f"{'':>12}  {'':>10}  {'(px)':>6}  {'(lines)':>6}  {'':>10}  {'':>7}")
    print("-" * 78)

    shown = 0
    for mode in modes:
        resolution = f"{mode.get('x_res', '?')}x{mode.get('y_res', '?')}"
        if args.match and args.match.replace(" ", "").lower() not in resolution.lower():
            continue
        pair = totals(mode)
        if pair is None:
            continue
        htotal, vtotal = pair
        try:
            rate_khz = float(mode["pixel_rate"])
        except (KeyError, ValueError):
            continue

        line_hz = rate_khz * 1000 / htotal if htotal else 0
        field_hz = line_hz / vtotal if vtotal else 0
        shown += 1
        print(f"{resolution:>12}  {rate_khz / 1000:>7.3f}MHz  {htotal:>6}  {vtotal:>6}  "
              f"{line_hz / 1000:>7.2f}kHz  {field_hz:>6.2f}Hz  {polarity(mode)}")

    if not shown:
        print("(nothing matched)")
    else:
        print(f"\nVTOTAL is in lines — compare it directly against mode_watch.py.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
