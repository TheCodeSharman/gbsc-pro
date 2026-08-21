#!/usr/bin/env python3
"""What clears a latched HPERIOD_IF railing, tried hardest-last.

    python3 reset_ladder.py --host <ip>

**UNTESTED AGAINST THE STATE IT IS FOR.** The railing has been produced once and
not reproduced since, so this has never been fired at a railed unit. It refuses
to run unless it finds the railing, because every rung disturbs the picture and
there is nothing to learn from clearing a state that is not there.

The ladder exists because the state survives everything cheap: ten detection
passes, four block resets pulsed one at a time, and a full ESP reset with chip
re-initialisation. Only a cold boot has cleared it. So the rungs here are the
ones that come closest to what removing power does -- the analog power-downs --
and they are last, because a script dying between the down and the up leaves the
chip off.

The RSTZ bits are active low, so 0 holds a block in reset. Only the twelve
documented bits are touched: s0 0x46 bit 7 and s0 0x47 bits 5-7 are named
nowhere in RD-5725-1.1, and holding an unnamed bit low is a guess with the
picture on the other end of it.

docs/investigations/hperiod-if-railing.md
"""

import argparse
import collections
import json
import os
import sys
import time

import gbs_unit
from bench_probe import Probe, WriteFailed

CATALOGUE = json.load(open(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "tv5725_registers.json")))

# Every documented block reset, so they can be held together. Pulsing one at a
# time leaves its neighbours running, and a block that is still clocked can
# re-establish what the reset cleared.
RESET_BITS = ["SFTRST_IF_RSTZ", "SFTRST_DEINT_RSTZ", "SFTRST_MEM_FF_RSTZ",
              "SFTRST_MEM_RSTZ", "SFTRST_FIFO_RSTZ", "SFTRST_OSD_RSTZ",
              "SFTRST_VDS_RSTZ", "SFTRST_DEC_RSTZ", "SFTRST_MODE_RSTZ",
              "SFTRST_SYNC_RSTZ", "SFTRST_HDBYPS_RSTZ", "SFTRST_INT_RSTZ"]

EXPECTED_HPERIOD = 431      # the bench source, 320x256@50
TOLERANCE = 3


def reset_masks():
    """register -> the bits holding a documented block in reset."""
    masks = collections.defaultdict(int)
    for name in RESET_BITS:
        field = CATALOGUE[name]
        assert field["seg"] == 0 and field["width"] == 1, name
        masks[field["reg"]] |= 1 << field["off"]
    return dict(masks)


def held_in_reset(byte, mask):
    """`byte` with the masked resets asserted and every other bit preserved."""
    return byte & ~mask & 0xFF


def spec(name):
    field = CATALOGUE[name]
    return (name, field["seg"], field["reg"], field["off"], field["width"])


class Ladder:
    def __init__(self, host):
        self.host = host
        self.probe = Probe(self._read, self._write)

    def _read(self, segment, register, attempts=12):
        for _ in range(attempts):
            value = gbs_unit.read_reg(self.host, segment, register)
            if value is not None:
                return value
            time.sleep(0.3)
        return None

    def _write(self, segment, register, value):
        return gbs_unit.write_reg(self.host, segment, register, value)

    def measure(self, seconds=4.0):
        counts = collections.Counter()
        deadline = time.time() + seconds
        while time.time() < deadline:
            registers = gbs_unit.read_segment(self.host, 0)
            if registers:
                field = CATALOGUE["HPERIOD_IF"]
                counts[gbs_unit.field_from(registers, field["reg"],
                                           field["off"], field["width"])] += 1
            time.sleep(0.08)
        total = sum(counts.values())
        good = sum(n for v, n in counts.items()
                   if abs(v - EXPECTED_HPERIOD) <= TOLERANCE)
        if not total:
            return "NO READS", counts
        if good == total:
            return "HEALTHY", counts
        return ("RAILED" if good == 0 else "mixed"), counts

    def latch(self):
        self.probe.write_field(spec("PLLAD_LAT"), 0)
        self.probe.write_field(spec("PLLAD_LAT"), 1)

    def all_blocks_in_reset(self, hold):
        masks = reset_masks()
        saved = {}
        for register in masks:
            saved[register] = self._read(0, register)
            if saved[register] is None:
                raise WriteFailed(f"s0 0x{register:02x} did not read")
        try:
            for register, mask in masks.items():
                self._write(0, register, held_in_reset(saved[register], mask))
            time.sleep(hold)
        finally:
            for register, value in saved.items():
                for _ in range(10):
                    if self._write(0, register, value):
                        break
                    time.sleep(0.3)
        time.sleep(2.0)

    def pulse_low(self, name, hold):
        field = spec(name)
        before = self.probe.read_field(field)
        if before is None:
            raise WriteFailed(f"{name} did not read")
        try:
            self.probe.write_field(field, 0)
            time.sleep(hold)
        finally:
            for _ in range(10):
                try:
                    self.probe.write_field(field, before)
                    break
                except WriteFailed:
                    time.sleep(0.4)
        self.latch()
        time.sleep(2.0)

    def rungs(self):
        return [
            ("all 12 blocks in reset together", lambda: self.all_blocks_in_reset(0.5)),
            ("all 12 blocks, 3 s", lambda: self.all_blocks_in_reset(3.0)),
            ("PLLAD_VCORST pulsed", lambda: self.pulse_low("PLLAD_VCORST", 0.3)),
            ("PLLAD_PDZ, PLL powered down", lambda: self.pulse_low("PLLAD_PDZ", 1.0)),
            ("ADC_POWDZ, ADC powered down", lambda: self.pulse_low("ADC_POWDZ", 1.0)),
        ]

    def recover(self):
        """Unfreeze FIRST, so the firmware re-solves even if a write failed."""
        for _ in range(15):
            try:
                if gbs_unit.get(self.host, "/freeze?on=0", timeout=3)[0] == 200:
                    break
            except Exception:  # noqa: BLE001 - a restore must not raise
                pass
            time.sleep(1)
        for _ in range(3):
            gbs_unit.get(self.host, "/sc?~", timeout=20)
            time.sleep(9)
            if self.measure(3.0)[0] == "HEALTHY":
                return True
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--anyway", action="store_true",
                        help="run even if HPERIOD_IF is not railed. Every rung "
                             "disturbs the picture, so this is not the default.")
    args = parser.parse_args()

    ladder = Ladder(args.host)
    verdict, counts = ladder.measure()
    print(f"as found: {verdict}  {dict(counts.most_common(4))}", flush=True)
    if verdict != "RAILED" and not args.anyway:
        print("not railed, so there is nothing to clear. --anyway to force.")
        return 0

    for name, rung in ladder.rungs():
        try:
            rung()
        except WriteFailed as e:
            print(f"  {name}: WRITE FAILED, rung void -- {e}", flush=True)
            continue
        verdict, counts = ladder.measure()
        print(f"  {name:34} {verdict:8} {dict(counts.most_common(4))}", flush=True)
        if verdict == "HEALTHY":
            print(f"\n{name} cleared it.", flush=True)
            break
    else:
        print("\nnothing in the ladder cleared it.", flush=True)

    print("recovered" if ladder.recover() else "COULD NOT RECOVER", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
