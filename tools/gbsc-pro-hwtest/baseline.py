#!/usr/bin/env python3
"""Is the unit in a known-good state, and if so, save it.

    ./baseline.py --host 192.168.88.108
    ./baseline.py --host 192.168.88.108 --snapshot snapshots/known-good.json

**"NO SIGNAL" IS SIX DIFFERENT FAULTS**, and a correct register dump does not
distinguish them -- see CLAUDE.md, "No HDMI with every register perfect". This
asks each precondition separately and names the first that is blocking.

The ordering is deliberate: every check depends on the ones above it, so the FIRST
failure is the one to work on and the rest are consequences. A source that never
asserts VSync makes VDS_ENABLE 0 and no amount of geometry work changes that.

The last two checks distinguish the geometry engine's own output from a preset
table's, which is the tell for docs/preset-load-clobber.md -- VDS_HB_SP below 8 and
PB_FETCH_NUM at exactly 256 both mean the engine has not run, because nothing else
writes either.
"""

import argparse
import json
import sys
import urllib.request


def read_reg(host, segment, register):
    with urllib.request.urlopen(
        f"http://{host}/getreg?s={segment}&r={hex(register)}", timeout=5
    ) as response:
        return int(json.load(response)["value"], 16)


def read_field(host, segment, register, offset, width):
    span = (offset + width + 7) // 8
    raw = 0
    for index in range(span):
        raw |= read_reg(host, segment, register + index) << (8 * index)
    return (raw >> offset) & ((1 << width) - 1)


# STATUS_16, from tv5725.h rather than from memory:
#
#     bit 0  HSPOL   H-sync polarity
#     bit 1  HSACT   H-sync active      <- the lock signal
#     bit 2  VSPOL   V-sync polarity
#     bit 3  VSACT   V-sync active
#
# **THE LOCK TEST IS HSACT ALONE**, which is what the firmware itself uses --
# getStatus16SpHsStable() checks `status16 & 0x02`. 0x06 is HSACT | VSPOL, a
# polarity bit, so a working unit passes for the wrong reason and fails with a
# picture on screen once the polarity flips.
#
# VSACT is NOT usable here: on the csync path it reads 0 while
# STATUS_SYNC_PROC_VTOTAL counts a steady 308 lines, and on the separate-sync
# path (2026-08-15) the same source reads it high with VTOTAL 311. Both values
# occur with a perfect picture. Reported below as information, never asserted
# on.
HS_ACTIVE = 1 << 1
VS_ACTIVE = 1 << 3
VS_POLARITY = 1 << 2

# The sync processor's line count is garbage the instant a preset lands -- a 97/98
# reading is normal and settles. Anything the VDS scales has far more lines.
SOURCE_VTOTAL_MIN = 200
SOURCE_VTOTAL_MAX = 1300

# Only the geometry engine ever writes these, so upstream's values identify a
# preset that has not been solved over.
ENGINE_HB_SP_FLOOR = 8
PRESET_DEFAULT_FETCH = 256


def gather(host):
    return {
        "STATUS_16": read_reg(host, 0, 0x16),
        "source_vtotal": read_field(host, 0, 0x1B, 0, 11),
        "hperiod_if": read_field(host, 0, 0x07, 0, 9),
        "hscale_byps": read_field(host, 3, 0x00, 4, 1),
        "vscale_byps": read_field(host, 3, 0x00, 5, 1),
        "htotal": read_field(host, 3, 0x01, 0, 12) + 1,
        "vtotal": read_field(host, 3, 0x02, 4, 11) + 1,
        "adc_input_sel": read_field(host, 5, 0x02, 6, 2),
        "pb_fetch_num": read_field(host, 4, 0x39, 0, 10),
        "vds_hb_sp": read_field(host, 3, 0x05, 4, 12),
        "display_clock_reg": read_reg(host, 0, 0x41),
    }


def checks_for(state):
    """Ordered so the FIRST failure is the cause and the rest are consequences."""
    return [
        (
            "H-sync active",
            bool(state["STATUS_16"] & HS_ACTIVE),
            f"STATUS_16 {state['STATUS_16']:#04x}"
            f" (HSACT {bool(state['STATUS_16'] & HS_ACTIVE):d},"
            f" VSACT {bool(state['STATUS_16'] & VS_ACTIVE):d},"
            f" VSPOL {bool(state['STATUS_16'] & VS_POLARITY):d})",
            "The same bit the firmware's own getStatus16SpHsStable() tests. Clear "
            "means no usable H-sync: for RGBHV that is the cable or the analog "
            "switch, since VGA is the only input raising asw_01 AND asw_04. Not a "
            "geometry fault. VSACT and VSPOL are shown but NOT asserted on -- "
            "this bench source gives a perfect picture with VSACT 0 on the csync "
            "path and VSACT 1 on the separate-sync path, so neither value means "
            "anything on its own.",
        ),
        (
            "source line count settled",
            SOURCE_VTOTAL_MIN < state["source_vtotal"] < SOURCE_VTOTAL_MAX,
            f"srcVTOTAL {state['source_vtotal']}",
            "This is the real proof vertical sync is being measured, whatever "
            "VSACT says. 0 is no measurement at all; under 200 is a reading in "
            "progress, so wait ~6 s after any mode change before believing it.",
        ),
        (
            "an explicit scale on both axes",
            state["hscale_byps"] == 0 and state["vscale_byps"] == 0,
            f"HSCALE_BYPS {state['hscale_byps']}, VSCALE_BYPS {state['vscale_byps']}",
            "1 means that axis is passing through 1:1, so the geometry engine's "
            "scale is not in the path. Expected in RGBHV bypass, which still "
            "produces a picture -- docs/rgbhv-bypass-trap.md.",
        ),
        (
            "output raster programmed",
            state["htotal"] > 64 and state["vtotal"] > 64,
            f"raster {state['htotal']} x {state['vtotal']}",
            "A raster under 64 is bypass, where there is no VDS geometry to solve.",
        ),
        (
            "geometry engine wrote the windows",
            state["vds_hb_sp"] >= ENGINE_HB_SP_FLOOR,
            f"VDS_HB_SP {state['vds_hb_sp']}, engine floor is {ENGINE_HB_SP_FLOOR}",
            "Below the floor means a preset table wrote this and the engine has "
            "not solved over it. docs/preset-load-clobber.md.",
        ),
        (
            "geometry engine wrote the burst",
            state["pb_fetch_num"] != PRESET_DEFAULT_FETCH,
            f"PB_FETCH_NUM {state['pb_fetch_num']}",
            f"Exactly {PRESET_DEFAULT_FETCH} is upstream's default, tuned for a "
            "raster this is not, and it can sit BELOW the floor the capture needs "
            "-- which is tearing.",
        ),
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--host", required=True)
    parser.add_argument("--snapshot", metavar="PATH",
                        help="write the register state here, but only if every "
                             "check passes -- a baseline is not a baseline if it "
                             "was captured from a broken unit")
    args = parser.parse_args(argv)

    state = gather(args.host)
    results = checks_for(state)

    print("\n  BASELINE CHECK\n")
    for name, ok, detail, _ in results:
        print(f"    [{'PASS' if ok else 'FAIL'}]  {name:32s} {detail}")

    failures = [(name, hint) for name, ok, _, hint in results if not ok]
    print(f"\n  {len(results) - len(failures)}/{len(results)} pass")

    if failures:
        name, hint = failures[0]
        print(f"\n  BLOCKING: {name}")
        for line in hint.split(". "):
            if line.strip():
                print(f"    {line.strip().rstrip('.')}.")
        if len(failures) > 1:
            print(f"\n  {len(failures) - 1} later check(s) also fail; expect those "
                  "to clear once this one does.")
        print("\n  Also worth ruling out before anything else:")
        print("    - stray tooling:  ps -eo pid,etime,cmd | grep -E 'soak_watch|regpanel'")
        print("    - a cold boot:    pull mains AND USB, since USB backfeeds the rails")
        return 1

    print(f"\n  raster {state['htotal']} x {state['vtotal']}, "
          f"display clock register {state['display_clock_reg']:#04x}, "
          f"ADC input {state['adc_input_sel']}")

    if args.snapshot:
        with open(args.snapshot, "w") as handle:
            json.dump(state, handle, indent=2, sort_keys=True)
        print(f"  wrote {args.snapshot}")
        print("  NOTE: this is the summary, not a restorable dump. For that use")
        print("        dump_registers.py --out, which writes all 496 config registers.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
