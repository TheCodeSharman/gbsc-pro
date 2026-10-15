#!/usr/bin/env python3
"""Does the measured hsync polarity follow the SYNC TYPE, and the origin with it?

    python3 tools/gbsc-pro-hwtest/sync_polarity.py --host 192.168.88.108
    ... --modes "X800 Y600 C256 F60"          just these

One source mode, both sync types, every field read by NAME in ONE pass so the
readings are comparable to each other -- a reading taken two source excursions
later is not a second instrument.

**THE POLARITY IS NOT A PROPERTY OF THE MODE.** The RISC PC's composite sync is
a NOR, so the same mode presents high-active on its own hsync line and
low-active through the separator. `SyncProcessor::hsyncPulseSamples()` takes
`InvertedPulseWidthSamples` off a high-active reading, so one source mode is
corrected on one sync type and not on the other -- and that constant was
calibrated on DIRECT-path sources, where the polarity describes the source. On
the composite path it describes the separator's output.

`VideoSourceLine::SeparatorPulseEndLeadSamples` is the other composite-specific
term, and the two are told apart by size: the separator's is the sync width less
66 and the polarity's is the constant itself.
docs/known-issues.md, docs/sync-type-selection.md
"""

import argparse
import sys
import time

import gbs_unit

MODESERV = "192.168.88.10"

PATH = ("STATUS_SYNC_PROC_HSPOL", "STATUS_SYNC_PROC_VSPOL",
        "STATUS_SYNC_PROC_HLOW_LEN", "STATUS_SYNC_PROC_HTOTAL",
        "STATUS_SYNC_PROC_VTOTAL", "SP_RT_HS_SP", "PLLAD_MD", "IF_HSYNC_RST",
        "IF_HB_SP2", "IF_HB_ST2", "SP_SOG_MODE", "SP_SOG_P_INV",
        "SP_HS_INV_REG", "SP_HS2PLL_INV_REG", "SP_H_CST_SP")

# A sync-type change leaves the engine on the path it held, so a leg pays for
# the re-probe before anything it reads means the type it asked for.
SETTLE_SECONDS = 14.0

# SyncProcessor's own, so the derived origin beside the register is the
# firmware's arithmetic rather than a second statement of it.
RETIME_ORIGIN_SAMPLES = 63
INVERTED_PULSE_WIDTH_SAMPLES = 5


def leg(host, where, mode, csync):
    gbs_unit.set_sync_type(where, csync)
    reply = gbs_unit.mode_serv(where, f"MODE {mode}")
    if not gbs_unit.modeserv_ok(reply):
        return None, f"the source refused the mode: {reply!r}"
    gbs_unit.select_input(host, "vga")
    gbs_unit.get(host, "/sc?~")
    time.sleep(SETTLE_SECONDS)
    if not gbs_unit.acquired_and_settled(host, limit_s=90.0):
        return None, "never acquired, or never stopped re-solving"
    read = gbs_unit.read_fields(host, list(PATH))
    _, geometry = gbs_unit.get_json(host, "/geometry")
    return (read, geometry), None


def report(read, geometry):
    low = read["STATUS_SYNC_PROC_HLOW_LEN"]
    line = read["STATUS_SYNC_PROC_HTOTAL"]
    pulse = min(low, line - low) if line > low else low
    positive = read["STATUS_SYNC_PROC_HSPOL"]
    folded = (pulse - INVERTED_PULSE_WIDTH_SAMPLES
              if positive and pulse > INVERTED_PULSE_WIDTH_SAMPLES else pulse)
    derived = read["PLLAD_MD"] - folded + RETIME_ORIGIN_SAMPLES
    pad = " " * 33
    print(f"HSPOL {positive} VSPOL {read['STATUS_SYNC_PROC_VSPOL']}  "
          f"low {low:5} line {line:5} pulse {pulse:4} "
          f"duty {pulse / line:.4f}  folded {folded:4}")
    print(f"{pad}MD {read['PLLAD_MD']:5} RT_HS_SP {read['SP_RT_HS_SP']:5} "
          f"(MD - folded + {RETIME_ORIGIN_SAMPLES} = {derived:5})  "
          f"IF_HSYNC_RST {read['IF_HSYNC_RST']:5}")
    print(f"{pad}IF_HB {read['IF_HB_SP2']:5}..{read['IF_HB_ST2']:5}  "
          f"SOG_MODE {read['SP_SOG_MODE']} SOG_P_INV {read['SP_SOG_P_INV']} "
          f"HS_INV {read['SP_HS_INV_REG']} "
          f"HS2PLL_INV {read['SP_HS2PLL_INV_REG']}  "
          f"H_CST_SP {read['SP_H_CST_SP']:5}")
    print(f"{pad}cv {geometry.get('cv')} ch {geometry.get('ch')} "
          f"rate {geometry.get('lineRateHz')} state {geometry.get('state')}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", default=MODESERV)
    parser.add_argument("--modes", default="X800 Y600 C256 F60",
                        help="comma separated source modes")
    args = parser.parse_args()

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    print(f"host {args.host}  source {args.modeserv}  {len(modes)} modes\n")
    for mode in modes:
        for name, csync in (("separate", False), ("composite", True)):
            found, why = leg(args.host, args.modeserv, mode, csync)
            print(f"{mode:22} {name:10} ", end="")
            if found is None:
                print(f"SKIP {why}")
                continue
            report(*found)
    return 0


if __name__ == "__main__":
    sys.exit(main())
