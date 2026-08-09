#!/usr/bin/env python3
"""The memory bus arbitration knobs, which nothing in the firmware ever sets.

The HSCALE tearing is a beat between the capture write bursts and the playback
read bursts over one SDRAM bus.
docs/investigations/hscale-tearing-characterisation.md. PB_FETCH_NUM is the
playback burst SIZE; these are the settings deciding how the bus is shared in the
first place -- the clock it runs at, who wins an arbitration, and when each side
asks -- and every one is sitting at whatever an upstream preset table left.

**MORE ZOOM MAKING IT WORSE AND A SMALLER PB_FETCH_NUM HELPING ARE DIFFERENT
QUANTITIES.** More produced pixels means more playback reads per output line,
which is BANDWIDTH. A smaller PB_FETCH_NUM moves the same total bytes in more,
finer requests, which is LATENCY -- a big burst that loses arbitration leaves the
playback FIFO draining for the whole of a capture transfer before it is refilled.
So the fault is worst-case service latency under contention, not a bandwidth
ceiling, and these knobs address it directly where PB_FETCH_NUM only reaches it
sideways.

    python3 membus.py --host 192.168.88.108                 # show, change nothing
    python3 membus.py --host 192.168.88.108 --clock 162
    python3 membus.py --host 192.168.88.108 --pb-request high
    python3 membus.py --host 192.168.88.108 --cap-request half
    python3 membus.py --host 192.168.88.108 --restore

**ONE KNOB AT A TIME.** The quantities interact: the clock changes both request
rates at once, the priority changes which one wins, and a picture that comes good
after two changes says nothing about either. The first write of a session saves what it found to
snapshots/membus-before.json, so --restore always has somewhere to go back to.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_probe
from gbs_unit import read_reg, write_reg

HERE = os.path.dirname(os.path.abspath(__file__))
BEFORE = os.path.join(HERE, "snapshots", "membus-before.json")

# name, segment, register, offset, width -- bench_probe's field spec.
PLL_MS = ("PLL_MS", 0, 0x40, 4, 3)
PB_REQ_SEL = ("PB_REQ_SEL", 4, 0x2B, 1, 2)
CAP_FF_HALF_REQ = ("CAP_FF_HALF_REQ", 4, 0x21, 1, 1)
CAP_STATUS_SEL = ("CAP_STATUS_SEL", 4, 0x22, 1, 1)
CAP_REQ_OVER = ("CAP_REQ_OVER", 4, 0x22, 0, 1)
MEM_REQ_PBH_RFFH = ("MEM_REQ_PBH_RFFH", 4, 0x15, 0, 1)
MEM_REQ_PB_RFF_CAP = ("MEM_REQ_PB_RFF_CAP", 4, 0x15, 1, 1)

# The ADC sampling divider, and the ONE register that must move with it.
#
# Tv5725::Sampling maintains IF_HSYNC_RST = PLLAD_MD / 2 in the firmware, but
# PLLAD_MD is written from other places too. Halving the divider by hand without
# it turns the display solid green, and sync stays "stable" throughout so nothing
# warns. This tool writes the pair or neither.
PLLAD_MD = ("PLLAD_MD", 5, 0x12, 0, 12)
IF_HSYNC_RST = ("IF_HSYNC_RST", 1, 0x0E, 0, 11)

# EVERY register that counts in ADC SAMPLES, and therefore every register that
# must move when PLLAD_MD does.
#
# Writing PLLAD_MD and IF_HSYNC_RST alone -- all the firmware's one maintained
# invariant covers -- gives a green, doubled, shredded picture. The sync processor
# counts in ADC SAMPLES, so every position and width below is a fraction of the
# line expressed in the old sample rate; change the rate and they all point
# somewhere else.
#
# The green specifically is the CLAMP. SP_CS_CLP_ST/SP landing on active video
# instead of back porch sets the black level from picture content.
#
# They are all lengths on one line, so they all scale by the same k = new / old.
# None is a number to re-derive: they are whatever this unit is tuned to, moved
# onto the new ruler.
ADC_UNIT_FIELDS = [
    ("SP_DLT_REG", 5, 0x35, 0, 12),        # sync width difference threshold
    ("SP_H_PULSE_IGNOR", 5, 0x37, 0, 8),   # ignore pulses shorter than this
    ("SP_CS_CLP_ST", 5, 0x41, 0, 12),      # SOG clamp start  <- the green
    ("SP_CS_CLP_SP", 5, 0x43, 0, 12),      # SOG clamp stop   <- the green
    ("SP_CS_HS_ST", 5, 0x45, 0, 12),
    ("SP_CS_HS_SP", 5, 0x47, 0, 12),
    ("SP_RT_HS_ST", 5, 0x49, 0, 12),
    ("SP_RT_HS_SP", 5, 0x4B, 0, 12),       # geometry.py: must stay under PLLAD_MD
    ("SP_H_CST_ST", 5, 0x4D, 0, 12),       # H coast start
    ("SP_H_CST_SP", 5, 0x4F, 0, 12),       # H coast stop
]

# The SECOND ruler. IF units are ADC samples / 2 -- IF_HSYNC_RST is PLLAD_MD/2
# -- so everything here scales by exactly the same k, and forgetting it is how
# IF_HB_ST1 ended up at 1096 on a line 851 units long: a window whose stop is
# past the end of the line, which wraps.
#
# IF_HB_ST2/SP2 and IF_LINE_ST/SP are deliberately ABSENT. The geometry engine
# recomputes those from IF_HSYNC_RST on every solve, which is the pattern all of
# these should have had -- writing them here would fight it.
IF_UNIT_FIELDS = [
    ("IF_HB_ST", 1, 0x10, 0, 11),
    ("IF_HB_SP", 1, 0x12, 0, 11),
    ("IF_HB_ST1", 1, 0x14, 0, 11),     # 1096 on an 851-unit line
    ("IF_HB_SP1", 1, 0x16, 0, 11),
    ("IF_HBIN_ST", 1, 0x24, 0, 12),
    ("IF_HBIN_SP", 1, 0x26, 0, 12),
    ("IF_INI_ST", 1, 0x0C, 5, 11),     # per-line pixel shift, so a length too
]

# **PLLAD_MD DOES NOT TAKE EFFECT UNTIL IT IS LATCHED.** tv5725.h: "This bit's
# rising edge is..." -- so 0, settle, 1. latchPLLAD() waits 128us between.
#
# Without it the register holds the new divider while the PLL keeps running at
# the old one, and IF_HSYNC_RST -- which IS believed immediately -- tells the
# whole downstream the line is a length it is not: a green, combed, doubled
# picture.
PLLAD_LAT = ("PLLAD_LAT", 5, 0x11, 7, 1)
LATCH_SETTLE_S = 0.002          # 128us in the firmware; generous over HTTP


def latch_pllad(probe, sleep):
    """Rising edge on PLLAD_LAT, which is what makes a written divider real."""
    probe.write_field(PLLAD_LAT, 0)
    sleep(LATCH_SETTLE_S)
    return probe.write_field(PLLAD_LAT, 1)


SP_RT_HS_SP_CHECK = ("SP_RT_HS_SP", 5, 0x4B, 0, 12)

# Read only, for context: the burst size three sessions went into.
PB_FETCH_NUM = ("PB_FETCH_NUM", 4, 0x39, 0, 10)
PB_CAP_OFFSET = ("PB_CAP_OFFSET", 4, 0x37, 0, 10)

# The quantity the fault is actually a function of, read for context: the beat
# is a band in VDS_HSCALE / PB_FETCH_NUM.
VDS_HSCALE = ("VDS_HSCALE", 3, 0x16, 0, 10)

WRITABLE = [PLL_MS, PB_REQ_SEL, CAP_FF_HALF_REQ, CAP_STATUS_SEL, CAP_REQ_OVER,
            MEM_REQ_PBH_RFFH, MEM_REQ_PB_RFF_CAP, PLLAD_MD, IF_HSYNC_RST]
SCALED_FIELDS = ADC_UNIT_FIELDS + IF_UNIT_FIELDS
WRITABLE = WRITABLE + SCALED_FIELDS
READABLE = WRITABLE + [PB_FETCH_NUM, PB_CAP_OFFSET, VDS_HSCALE]

# DS-5725-3.2 front page: "Maximum analog sampling rate up to 162MSPS".
MAX_SAMPLE_RATE_HZ = 162_000_000

# tv5725.h PLL_MS, s0 0x40 bits 4-6. 010 is not a frequency -- it takes the
# memory clock from FBCLK on pin 110 -- so it is named rather than numbered, and
# it is what the firmware writes at three call sites while the bench unit reads 7.
CLOCKS = {0: 108.0, 1: 81.0, 2: None, 3: 162.0, 4: 144.0, 5: 185.0, 6: 216.0,
          7: 129.6}

# What /sc?l steps through (gbs-control.ino:9276). 216 MHz is NOT in it, which is
# the whole reason this tool writes the register directly.
SERIAL_CYCLE = [0, 2, 7, 4, 3, 5]


def sample_rate_hz(divider, line_rate_hz, oversample=4):
    return divider * line_rate_hz * oversample


def divider_is_safe(divider, line_rate_hz, oversample=4):
    """Whether this divider keeps the ADC inside its rated 162 MSPS.

    Refused rather than clamped: a silently reduced divider changes the capture
    units, which changes HSCALE, which is the quantity under investigation. A
    substituted value would be recorded as a measurement of the one asked for.
    """
    return sample_rate_hz(divider, line_rate_hz, oversample) <= MAX_SAMPLE_RATE_HZ


def scaled_adc_units(baseline, old_divider, new_divider):
    """Every ADC-unit register moved onto the new sample rate.

    Scaled from the BASELINE rather than from what the registers hold now, so
    stepping 2553 -> 1840 -> 1700 does not compound rounding and running the
    same command twice is a no-op rather than a second shrink.

    Clamped to each field's own width. A 12-bit position scaled past 4095 would
    WRAP to a small number, which is a clamp landing inside the hsync pulse --
    the green picture again, arriving by arithmetic instead of by omission.
    """
    if not old_divider or not new_divider:
        return {}
    k = new_divider / old_divider
    out = {}
    for name, _seg, _reg, _off, width in SCALED_FIELDS:
        was = baseline.get(name)
        if was is None:
            continue
        out[name] = min(int(round(was * k)), (1 << width) - 1)
    return out


def clock_name(value):
    mhz = CLOCKS.get(value)
    return "FBCLK (pin 110)" if mhz is None else f"{mhz:g} MHz"


def clock_value(mhz):
    """The PLL_MS setting for a clock in MHz, or None if the chip has no such
    setting. Refuses to pick the nearest: a silently substituted clock would be
    recorded as a measurement of the one that was asked for."""
    for value, rate in CLOCKS.items():
        if rate is not None and abs(rate - mhz) < 0.05:
            return value
    return None


REQUEST = {0: "low", 1: "high", 2: "low", 3: "high"}


def describe(probe):
    """Every knob, decoded. None where a register did not read -- a missing read
    is reported, never defaulted."""
    state = {}
    for spec in READABLE:
        state[spec[0]] = probe.read_field(spec)
    return state


def render(state):
    lines = []
    pll = state.get("PLL_MS")
    lines.append(f"  SDRAM clock      PLL_MS {pll}   {clock_name(pll)}"
                 if pll is not None else "  SDRAM clock      <unread>")

    pb = state.get("PB_REQ_SEL")
    lines.append(f"  playback request PB_REQ_SEL {pb} = {REQUEST.get(pb, '?')} "
                 f"priority against capture"
                 if pb is not None else "  playback request <unread>")

    lines.append(f"  capture asks     CAP_FF_HALF_REQ {state.get('CAP_FF_HALF_REQ')}"
                 f"  (1 = at half full, 0 = at one entry)")
    lines.append(f"                   CAP_STATUS_SEL  {state.get('CAP_STATUS_SEL')}"
                 f"   CAP_REQ_OVER {state.get('CAP_REQ_OVER')}")
    lines.append(f"  arbiter          MEM_REQ_PBH_RFFH {state.get('MEM_REQ_PBH_RFFH')}"
                 f"   MEM_REQ_PB_RFF_CAP {state.get('MEM_REQ_PB_RFF_CAP')}")
    lines.append(f"  burst (context)  PB_FETCH_NUM {state.get('PB_FETCH_NUM')}"
                 f"   PB_CAP_OFFSET {state.get('PB_CAP_OFFSET')}")
    md = state.get("PLLAD_MD")
    hs = state.get("VDS_HSCALE")
    lines.append(f"  sampling         PLLAD_MD {md}   IF_HSYNC_RST "
                 f"{state.get('IF_HSYNC_RST')}"
                 + (f"   ({md/2:.0f} expected)" if md else ""))
    if md and hs and state.get("PB_FETCH_NUM"):
        lines.append(f"  the beat         VDS_HSCALE {hs}  / fetch "
                     f"{state['PB_FETCH_NUM']} = ratio "
                     f"{hs/state['PB_FETCH_NUM']:.2f}"
                     f"   (band measured 2.34..2.75)")
    return "\n".join(lines)


def save_before(state):
    """Record the starting state ONCE per session, so --restore goes back to what
    was there before any of this ran rather than to the last thing tried."""
    if os.path.exists(BEFORE):
        return False
    os.makedirs(os.path.dirname(BEFORE), exist_ok=True)
    with open(BEFORE, "w") as handle:
        json.dump(state, handle, indent=2, sort_keys=True)
    return True


def apply_field(probe, spec, value, state):
    save_before(state)
    if not probe.write_field(spec, value):
        return False
    return probe.read_field(spec) == value


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--clock", type=float,
                        help=f"SDRAM clock in MHz. One of: "
                             f"{', '.join(f'{v:g}' for v in sorted(x for x in CLOCKS.values() if x))}")
    parser.add_argument("--pb-request", choices=("low", "high"),
                        help="playback's arbitration priority against capture")
    parser.add_argument("--cap-request", choices=("half", "one"),
                        help="when the capture FIFO raises a memory request")
    parser.add_argument("--pllad", type=int,
                        help="ADC sampling divider. Writes IF_HSYNC_RST = "
                             "PLLAD_MD/2 with it, never on its own")
    parser.add_argument("--no-scale", action="store_true",
                        help="move ONLY the divider, its latch and "
                             "IF_HSYNC_RST. Scaling the sync-processor block "
                             "was solving a problem an unlatched write created; "
                             "this isolates the divider change itself")
    parser.add_argument("--line-rate", type=int, default=15550,
                        help="input line rate in Hz, for the 162 MSPS check "
                             "(default 15550 = VTOTAL 311 at 50 Hz)")
    parser.add_argument("--restore", action="store_true",
                        help="put every knob back to what the first run found")
    args = parser.parse_args()

    probe = bench_probe.Probe(
        lambda segment, register: read_reg(args.host, segment, register),
        lambda segment, register, value: write_reg(args.host, segment, register, value))

    state = describe(probe)
    print("before:")
    print(render(state))

    changes = 0

    if args.restore:
        if not os.path.exists(BEFORE):
            print(f"\nnothing to restore: {BEFORE} does not exist")
            return 1
        with open(BEFORE) as handle:
            saved = json.load(handle)
        for spec in WRITABLE:
            want = saved.get(spec[0])
            if want is not None and state.get(spec[0]) != want:
                probe.write_field(spec, want)
                changes += 1
        print(f"\nrestored {changes} field(s) from {BEFORE}")

    if args.clock is not None:
        value = clock_value(args.clock)
        if value is None:
            print(f"\nthe chip has no {args.clock:g} MHz setting. PLL_MS offers "
                  f"{', '.join(f'{v:g}' for v in sorted(x for x in CLOCKS.values() if x))}")
            return 1
        ok = apply_field(probe, PLL_MS, value, state)
        print(f"\nSDRAM clock -> {clock_name(value)} (PLL_MS {value}): "
              f"{'ok' if ok else 'DID NOT TAKE'}")
        changes += 1

    if args.pllad is not None:
        rate = sample_rate_hz(args.pllad, args.line_rate)
        if not divider_is_safe(args.pllad, args.line_rate):
            print(f"\nPLLAD_MD {args.pllad} at {args.line_rate} Hz is "
                  f"{rate/1e6:.1f} MSPS, over the rated 162. Refused.")
            return 1
        save_before(state)
        with open(BEFORE) as handle:
            baseline = json.load(handle)
        old = baseline.get("PLLAD_MD") or state.get("PLLAD_MD")
        line = args.pllad // 2
        ok_md = probe.write_field(PLLAD_MD, args.pllad)
        # LATCH BEFORE anything downstream is told the line has changed, so the
        # sample rate and IF_HSYNC_RST never disagree even briefly.
        latch_pllad(probe, time.sleep)
        ok_if = probe.write_field(IF_HSYNC_RST, line)
        got_md = probe.read_field(PLLAD_MD)
        got_if = probe.read_field(IF_HSYNC_RST)
        print(f"\nPLLAD_MD -> {args.pllad} ({rate/1e6:.1f} MSPS, "
              f"{100*rate/MAX_SAMPLE_RATE_HZ:.0f}% of rated), "
              f"IF_HSYNC_RST -> {line}: "
              f"{'ok' if (ok_md and ok_if and got_md == args.pllad and got_if == line) else 'DID NOT TAKE'}")
        wanted = {} if args.no_scale else scaled_adc_units(baseline, old,
                                                           args.pllad)
        for spec in SCALED_FIELDS:
            if spec[0] in wanted:
                probe.write_field(spec, wanted[spec[0]])
        print(f"  scaled {len(wanted)} sample-unit registers by "
              f"{args.pllad/old:.4f} from the baseline at PLLAD_MD {old}:")
        for spec in SCALED_FIELDS:
            name = spec[0]
            if name in wanted:
                got = probe.read_field(spec)
                flag = "" if got == wanted[name] else "   <- DID NOT TAKE"
                print(f"      {name:18s} {baseline.get(name):5} -> "
                      f"{wanted[name]:5}   (read {got}){flag}")
        rt = probe.read_field(SP_RT_HS_SP_CHECK)
        if rt is not None and rt >= args.pllad:
            print(f"  WARNING: SP_RT_HS_SP {rt} is not under PLLAD_MD {args.pllad}")
        print("  the geometry needs one pad press to be recomputed against the "
              "new line length: curl 'http://<host>/sc?z'")
        changes += 1

    if args.pb_request is not None:
        ok = apply_field(probe, PB_REQ_SEL, 1 if args.pb_request == "high" else 0,
                         state)
        print(f"\nplayback request -> {args.pb_request}: "
              f"{'ok' if ok else 'DID NOT TAKE'}")
        changes += 1

    if args.cap_request is not None:
        ok = apply_field(probe, CAP_FF_HALF_REQ,
                         1 if args.cap_request == "half" else 0, state)
        print(f"\ncapture request -> at {args.cap_request}: "
              f"{'ok' if ok else 'DID NOT TAKE'}")
        changes += 1

    if changes > 1:
        print("\nWARNING: more than one knob moved in this run. A picture that "
              "comes good now says nothing about which change earned it.")

    if changes:
        print("\nafter:")
        print(render(describe(probe)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
