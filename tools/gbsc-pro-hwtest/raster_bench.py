#!/usr/bin/env python3
"""Drive output rasters onto a live unit, to find out what the TV will lock to.

The model is in output_raster.py; this only applies it and reads back, so nothing
about the arithmetic lives here.

    # what would be written, no unit needed
    ./raster_bench.py --mode 1080p --rate 50 --dry-run

    # apply it, then put the unit back
    ./raster_bench.py --host 192.168.88.108 --mode 1080p --rate 50
    ./raster_bench.py --host 192.168.88.108 --restore

    # sweep raster SHAPES at a fixed frame time -- see --iso below
    ./raster_bench.py --host 192.168.88.108 --iso --pause 6

**TWO KINDS OF EXPERIMENT, AND ONLY ONE IS FREE.**

`--mode` changes htotal, which changes the output frame rate, which the firmware
then fights: runFrequency() corrects 0.06% per pass, so a 33% move needs hundreds
of iterations with the picture wrong throughout. The clock cannot be changed over
HTTP at all -- PLL648_CONTROL_01 is parked at the 0x75 sentinel and only
externalClockGenResetClock() reprograms the Si5351, which nothing reachable calls.
So `--mode` is honest only for small moves, or after a flash that changes the seed.

`--iso` holds htotal x vtotal constant, so the frame time and the pixel clock do
not move at all and the ONLY thing varying is the raster shape the encoder has to
lock to. That needs no firmware change and is the experiment to run first.

The unit is frozen throughout: otherwise runAutoBestHTotal() retimes the output
underneath the experiment. Freezing is never persisted, so a power cycle always
recovers.
"""

import argparse
import json
import sys
import time
import urllib.parse
import urllib.request

import output_raster as our

# Read back after every apply. VDS_HB_SP and PB_FETCH_NUM are the tells that the
# geometry engine really re-solved rather than the preset being left in place --
# only the engine writes either. See docs/preset-load-clobber.md.
READBACK = [
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12),
    ("VDS_VSYNC_RST", 3, 0x02, 4, 11),
    ("VDS_HS_ST", 3, 0x0A, 0, 12),
    ("VDS_HS_SP", 3, 0x0B, 4, 12),
    ("VDS_HB_SP", 3, 0x05, 4, 12),
    ("VDS_HB_ST", 3, 0x04, 0, 12),
    ("VDS_DIS_HB_SP", 3, 0x11, 4, 12),
    ("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
    ("VDS_HSCALE", 3, 0x16, 0, 10),
    ("PB_FETCH_NUM", 4, 0x39, 0, 10),
    ("STATUS_SYNC_PROC_VTOTAL", 0, 0x1B, 0, 11),
]


def _get(host, path):
    with urllib.request.urlopen(f"http://{host}{path}", timeout=8) as response:
        return response.read().decode(errors="replace")


def read_reg(host, segment, register):
    body = _get(host, f"/getreg?s={segment}&r={hex(register)}")
    return int(json.loads(body)["value"], 16)


def read_field(host, segment, register, offset, width):
    span = (offset + width + 7) // 8
    raw = 0
    for index in range(span):
        raw |= read_reg(host, segment, register + index) << (8 * index)
    return (raw >> offset) & ((1 << width) - 1)


def write_field(host, segment, register, offset, width, value):
    """Read-modify-write, because these fields share bytes with their neighbours.

    VDS_HSYNC_RST's top nibble lives in the same byte as VDS_VSYNC_RST's bottom
    one, so a whole-byte write to either corrupts the other. That is not a
    hypothetical: it is how a raster ends up with a rolled frame.
    """
    span = (offset + width + 7) // 8
    raw = 0
    for index in range(span):
        raw |= read_reg(host, segment, register + index) << (8 * index)

    mask = ((1 << width) - 1) << offset
    raw = (raw & ~mask) | ((value << offset) & mask)

    for index in range(span):
        byte = (raw >> (8 * index)) & 0xFF
        _get(host, f"/setreg?s={segment}&r={hex(register + index)}&v={hex(byte)}")


def freeze(host, on):
    return _get(host, f"/freeze?on={1 if on else 0}").strip()


PLL648_CONTROL_01 = (0, 0x41)
EXTERNAL_SENTINEL = 0x75


def set_display_clock(host, divider, settle=1.5):
    """Reprogram the Si5351 to a different display clock, live, over HTTP.

    **THIS IS THE ONLY REACHABLE ROUTE AND THE SEQUENCE MATTERS.** Nothing exposed
    over HTTP reprograms the Si5351 -- except the ';' serial command, which calls
    externalClockGenResetClock() directly with no stash-restore in its path, so it
    reads whatever divider we just wrote. /sc?%3B is that command.

    Four steps, because two guards fight each other:

    1. Write the real divider to PLL648_CONTROL_01.
    2. ';' -- resetClock() reads it and programs the Si5351. Side effect: it
       toggles extClockGenDetected to 0 and prints "ext clock gen bypass".
    3. ';' again -- detected back to 1. resetClock() early-returns this time,
       which is why the divider had to be in place for the FIRST press.
    4. Re-park the sentinel. Both runFrequency() and
       externalClockGenSyncInOutRate() bail out unless PLL648_CONTROL_01 reads
       0x75, so leaving a real divider there silently disables frame-time lock
       altogether -- the clock would be right and nothing would hold it to the
       source. freqExtClockGen already holds the new value by now, so parking the
       sentinel costs nothing and restores the steering.

    Returns the frequency the firmware should now be driving.
    """
    write_field(host, *PLL648_CONTROL_01, 0, 8, divider)

    _get(host, "/sc?%3B")
    time.sleep(settle)
    _get(host, "/sc?%3B")
    time.sleep(settle)

    write_field(host, *PLL648_CONTROL_01, 0, 8, EXTERNAL_SENTINEL)
    time.sleep(settle)

    # This leaves the Si5351 at the divider's NOMINAL frequency, which a raster
    # almost never demands exactly -- htotal_for() floors, so 2301 x 1126 at 50 Hz
    # wants 129.546 MHz against the 129.600 programmed. That residual is 50.021 Hz
    # out against 50 Hz in, and a 0.021 Hz beat shows as a rolling bar every
    # 30 seconds or so. steer_rate() closes it, and apply_raster() calls it at the
    # only point in the sequence where it can measure the right thing.
    return our.DIVIDER_HZ.get(divider)


def steer_rate(host, settle=3.0):
    """Pull the Si5351 to exactly what the CURRENT raster demands.

    **THE ORDERING IS THE WHOLE POINT, AND GETTING IT WRONG BLACKED THE SCREEN.**
    externalClockGenSyncInOutRate() measures getOutputFrameRate() and scales the
    clock by sourceRate/outputRate. That measurement is taken against whatever
    raster is programmed right now, so calling it before the new raster is written
    corrects the new clock against the OLD raster:

        clock set to 129.6 MHz with the raster still 1445 x 1126
          -> ofr reads 129.6e6/(1445 x 1126) = 79.7 Hz
          -> inside the 47..86 guard, so it proceeds
          -> clock set to (50/79.7) x 129.6 = 81.3 MHz, undoing the change
        then the 2301 raster lands on 81.3 MHz -> 31.4 Hz, and nothing locks

    It also has to come after the sentinel is parked, because it bails out on the
    same `!= 0x75` guard that makes a second ';' press a no-op.

    So: raster, then clock, then re-solve, then this. Which is exactly the order
    applyPresets() uses -- preset table, externalClockGenResetClock(),
    externalClockGenSyncInOutRate(). apply_raster() owns the sequence so a caller
    cannot reorder it.
    """
    _get(host, "/sc?%3A")
    time.sleep(settle)


def framing(host):
    return json.loads(_get(host, "/geometry"))


def resolve(host):
    """Force the engine to re-derive every window against the raster as it stands.

    /geometry sets `changed` whenever a parameter is PRESENT, so re-requesting the
    current framing still queues a full solve. It is drained from loop() and works
    while frozen, which is exactly the seam this needs -- the raster registers are
    written here, and the engine recomputes the eight blanking registers from them.
    """
    current = framing(host)
    query = urllib.parse.urlencode(current)
    return json.loads(_get(host, f"/geometry?{query}"))


def snapshot(host):
    return {name: read_field(host, *spec) for name, *spec in
            [(n, s, r, o, w) for n, s, r, o, w in READBACK]}


def show(label, state):
    print(f"  {label}")
    htotal = state["VDS_HSYNC_RST"] + 1
    vtotal = state["VDS_VSYNC_RST"] + 1
    print(f"    raster        {htotal} x {vtotal}")
    print(f"    hsync         {state['VDS_HS_ST']}..{state['VDS_HS_SP']}")
    print(f"    memory  win   {state['VDS_HB_SP']}..{state['VDS_HB_ST']}")
    print(f"    display win   {state['VDS_DIS_HB_SP']}..{state['VDS_DIS_HB_ST']}")
    print(f"    HSCALE        {state['VDS_HSCALE']}   PB_FETCH_NUM {state['PB_FETCH_NUM']}")
    print(f"    source VTOTAL {state['STATUS_SYNC_PROC_VTOTAL']}")


def apply_raster(host, raster, pause, divider=None):
    """Apply a whole output mode: raster, clock, windows, rate. In that order.

    **THE ORDER IS THE CONTRACT, and this function owns it precisely because the
    caller got it wrong and blacked the screen.** It is the order applyPresets()
    uses, for the same reasons:

      1. the raster registers, totals first so a sync position is never briefly
         outside the raster. Output timing moves as a set or not at all.
      2. the display clock, chosen for THIS raster.
      3. the engine re-solves every window against it.
      4. the rate steer LAST, because it measures the output frame rate off the
         raster that is programmed at the time -- see steer_rate().

    `divider` defaults to the raster's own. Pass it explicitly only to test a
    deliberate mismatch between clock and raster.
    """
    writes = [
        (3, 0x01, 0, 12, raster.htotal - 1),      # VDS_HSYNC_RST
        (3, 0x02, 4, 11, raster.vtotal - 1),      # VDS_VSYNC_RST
        (3, 0x0A, 0, 12, raster.hsync_start),     # VDS_HS_ST
        (3, 0x0B, 4, 12, raster.hsync_stop),      # VDS_HS_SP
        (3, 0x0D, 0, 11, raster.vsync_start),     # VDS_VS_ST
        (3, 0x0E, 4, 11, raster.vsync_stop),      # VDS_VS_SP
    ]
    for segment, register, offset, width, value in writes:
        write_field(host, segment, register, offset, width, value)

    actual = set_display_clock(host, raster.divider if divider is None else divider)
    resolve(host)
    steer_rate(host)

    if pause:
        time.sleep(pause)
    return actual


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host")
    parser.add_argument("--mode", default="1080p", choices=sorted(our.MODE_VTOTAL))
    parser.add_argument("--rate", type=float, default=50.0)
    parser.add_argument("--ceiling", type=float, default=our.WORKING_CEILING_HZ / 1e6,
                        help="display clock ceiling in MHz (default %(default)s)")
    parser.add_argument("--hsync", help="override the standard pulse, as START:STOP")
    parser.add_argument("--iso", action="store_true",
                        help="sweep raster shapes at a fixed frame time")
    parser.add_argument("--clock-steps", metavar="MHZ,MHZ,...",
                        help="step the display clock, recomputing the raster as a "
                             "set at each one, to find where the picture stops. "
                             "e.g. --clock-steps 108,129.6,162")
    parser.add_argument("--pause", type=float, default=6.0,
                        help="seconds to hold each state; the settling rule is ~6 s")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--restore", action="store_true",
                        help="unfreeze and force a preset reload, recovering the unit")
    args = parser.parse_args(argv)

    if args.restore:
        if not args.host:
            parser.error("--restore needs --host")
        print(freeze(args.host, False))
        print("unfrozen. The next mode change or /uc?h reloads a preset.")
        return 0

    hsync = None
    if args.hsync:
        start, stop = args.hsync.split(":")
        hsync = (int(start), int(stop))

    target = our.raster_for(args.mode, args.rate,
                            ceiling_hz=int(args.ceiling * 1e6), hsync=hsync)
    if target is None:
        print(f"no raster fits {args.mode} at {args.rate} Hz under "
              f"{args.ceiling} MHz", file=sys.stderr)
        return 1

    print(target.describe())

    if args.dry_run or not args.host:
        if args.iso:
            shapes = our.iso_frame_rasters(target.htotal, target.vtotal)
            print(f"\n  {len(shapes)} iso-frame shapes (frame time and clock held):")
            for htotal, vtotal in shapes:
                drift = htotal * vtotal / (target.htotal * target.vtotal) - 1.0
                print(f"    {htotal:5d} x {vtotal:<5d}  {drift:+.3%}")
        if not args.host:
            print("\n  (no --host: nothing was written)")
        return 0

    before = snapshot(args.host)
    print()
    show("before", before)

    print(f"\n  {freeze(args.host, True)}")
    try:
        if args.clock_steps:
            steps = [float(mhz) for mhz in args.clock_steps.split(",")]
            print(f"  stepping the display clock: {steps} MHz\n")
            print("  **THE PICTURE IS THE INSTRUMENT.** Nothing readable here says")
            print("  whether the TV locked -- VDS registers stay perfect through a")
            print("  blank screen. Watch the display at each step and say what it did.\n")
            for mhz in steps:
                divider = min(our.DIVIDER_HZ,
                              key=lambda d: abs(our.DIVIDER_HZ[d] - mhz * 1e6))
                step = our.raster_for(args.mode, args.rate,
                                      ceiling_hz=our.DIVIDER_HZ[divider], hsync=hsync)
                if step is None:
                    print(f"  {mhz} MHz: no raster fits {args.mode} at {args.rate} Hz")
                    continue
                actual = apply_raster(args.host, step, args.pause)
                print(f"  --- {actual / 1e6:g} MHz seed {divider:#04x}: "
                      f"{step.htotal} x {step.vtotal}, active {step.active_width()} px")
                show("readback", snapshot(args.host))
                print()
        elif args.iso:
            shapes = our.iso_frame_rasters(target.htotal, target.vtotal)
            print(f"  sweeping {len(shapes)} iso-frame shapes, {args.pause}s each\n")
            for htotal, vtotal in shapes:
                from dataclasses import replace
                apply_raster(args.host, replace(target, htotal=htotal, vtotal=vtotal),
                             args.pause)
                show(f"{htotal} x {vtotal}", snapshot(args.host))
        else:
            # THE CLOCK COMES FIRST, and this path used not to set it at all.
            # The model chooses a divider, so applying its raster without the
            # matching clock leaves the old one driving a new line length: the
            # 2301-px raster on the 81 MHz the 1445 preset was using is a 31 Hz
            # frame, which no display locks to. It read as "the model produces a
            # broken raster" when the tool simply had not finished the set.
            actual = apply_raster(args.host, target, args.pause)
            print(f"  display clock -> {actual / 1e6:g} MHz "
                  f"(seed {target.divider:#04x}), rate steered after the raster")
            print()
            show("after", snapshot(args.host))
    finally:
        print(f"\n  still frozen: {freeze(args.host, True)}")
        print("  run with --restore to unfreeze when you are done looking.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
