#!/usr/bin/env python3
"""Read and set the scaler's input sample clock, PLLAD_MD, the way the firmware
does it.

PLLAD_MD (seg 5, 0x12) sets how many times the ADC samples each input line. It
cannot be moved on its own: IF_HSYNC_RST and IF_LINE_SP are the input-side
coordinate space that the capture window lives in, and both are derived from it.
Writing the divider alone leaves the capture window addressing a line that no
longer exists, and leaves the PLL on its old setting until it is latched.

The firmware's own '/sc?n' handler is the reference for the whole sequence:

    pll_divider += 1
    PLLAD_MD     = pll_divider
    IF_HSYNC_RST = pll_divider / 2
    IF_LINE_SP   = pll_divider / 2 + 1 + 0x40
    latchPLLAD(); delay(1)
    updateClampPosition(); updateCoastPosition(0)

'/sc?n' only ever increments, so this reproduces the sequence to reach an
arbitrary value in either direction. Increments still go through '/sc?n' where
they can, because that also runs the clamp and coast updates, which are not
reachable over the register endpoints.

Nothing here persists. A mode change or a reset restores the preset's values;
only /uc?4 writes them to flash.

    python3 pllad.py --host 192.168.88.108
    python3 pllad.py --host 192.168.88.108 --set 2560
    python3 pllad.py --host 192.168.88.108 --set 2560 --raw    # skip /sc?n
"""

import argparse
import sys
import time

from gbs_unit import get, read_reg, read_word, write_reg

PLLAD_MD = (5, 0x12, 0x0FFF)
IF_HSYNC_RST = (1, 0x0E, 0x07FF)
IF_LINE_SP = (1, 0x22, 0x0FFF)
SP_RT_HS_SP = (5, 0x4B, 0x0FFF)
PLLAD_CONTROL = (5, 0x11)  # bit 7 is PLLAD_LAT
STATUS_MISC = (0, 0x09)  # bit 7 is PLLAD_LOCK

# '/sc?n' is one increment per request. Beyond this many, the register path is
# quicker and the clamp/coast updates are re-run by the sync watcher anyway.
MAX_NUDGES = 128


def read_state(host):
    """Everything that moves with the sample clock, plus what it is measured as."""
    status = read_reg(host, *STATUS_MISC)
    return {
        "pllad_md": read_word(host, *PLLAD_MD),
        "if_hsync_rst": read_word(host, *IF_HSYNC_RST),
        "if_line_sp": read_word(host, *IF_LINE_SP),
        "sp_rt_hs_sp": read_word(host, *SP_RT_HS_SP),
        "htotal": read_word(host, 0, 0x17, 0x0FFF),
        "vtotal": read_word(host, 0, 0x1B, 0x07FF),
        "preset_id": read_reg(host, 1, 0x2B),
        "pllad_lock": None if status is None else bool(status & 0x80),
    }


def describe(state):
    lock = {True: "locked", False: "UNLOCKED", None: "?"}[state["pllad_lock"]]
    # Any of these can read back as None when the unit is busy or mid-reset, and
    # that is exactly when this print matters most -- so it must not raise.
    drift = (
        f"{state['htotal'] - state['pllad_md']:+d}"
        if state["htotal"] is not None and state["pllad_md"] is not None
        else "?"
    )
    preset = "??" if state["preset_id"] is None else f"{state['preset_id']:02x}"
    return (
        f"PLLAD_MD {state['pllad_md']}  PLL {lock}\n"
        f"    IF_HSYNC_RST {state['if_hsync_rst']}  IF_LINE_SP {state['if_line_sp']}  "
        f"SP_RT_HS_SP {state['sp_rt_hs_sp']}\n"
        f"    HTOTAL {state['htotal']} (PLLAD_MD{drift})  VTOTAL {state['vtotal']}  "
        f"preset 0x{preset}"
    )


def write_word(host, segment, low_register, value, mask):
    """A 12- or 11-bit field spread over two bytes, preserving the bits above it.
    The high byte shares itself with unrelated fields, so it is read first."""
    value &= mask
    high_mask = mask >> 8
    high = read_reg(host, segment, low_register + 1)
    if high is None:
        return False
    high = (high & ~high_mask) | ((value >> 8) & high_mask)
    return (
        write_reg(host, segment, low_register, value & 0xFF) is not None
        and write_reg(host, segment, low_register + 1, high) is not None
    )


def latch(host):
    """PLLAD_LAT low, then high — the PLL takes the new divider on the edge."""
    control = read_reg(host, *PLLAD_CONTROL)
    if control is None:
        return False
    write_reg(host, *PLLAD_CONTROL, control & ~0x80)
    time.sleep(0.01)
    write_reg(host, *PLLAD_CONTROL, control | 0x80)
    return True


def set_by_registers(host, target):
    """The '/sc?n' sequence, written directly, so it can also go downwards."""
    ok = write_word(host, PLLAD_MD[0], PLLAD_MD[1], target, PLLAD_MD[2])
    ok &= write_word(host, IF_HSYNC_RST[0], IF_HSYNC_RST[1], target // 2, IF_HSYNC_RST[2])
    ok &= write_word(host, IF_LINE_SP[0], IF_LINE_SP[1], target // 2 + 1 + 0x40, IF_LINE_SP[2])
    # The firmware keeps the retime stop at 0.93 of the divider; left behind it
    # would sit inside the active line at the new clock.
    ok &= write_word(host, SP_RT_HS_SP[0], SP_RT_HS_SP[1], int(target * 0.93), SP_RT_HS_SP[2])
    ok &= latch(host)
    return ok


def set_by_steps(host, start, target, step, settle=1.0):
    """Walk to the target in small increments, checking the unit after each one.

    A large jump breaks input sync outright, and the firmware's framesync
    measurement then waits for a vsync pulse that never arrives -- with the
    watchdog disabled, so the board hangs until it is power cycled (see
    framesync.h vsyncInputSample). Small steps keep the PLL locked, so sync
    survives and that path is never entered.

    Stops at the first step the unit does not answer after, and reports where,
    so a hang bounds the problem instead of losing the whole run.
    """
    direction = 1 if target > start else -1
    current = start
    while current != target:
        remaining = abs(target - current)
        current += direction * min(step, remaining)
        if not set_by_registers(host, current):
            print(f"    write failed at PLLAD_MD {current}")
            return False, current
        time.sleep(settle)
        state = read_state(host)
        if state["pllad_md"] is None:
            print(f"    !! unit stopped answering after PLLAD_MD {current}")
            return False, current
        lock = {True: "locked", False: "UNLOCKED", None: "?"}[state["pllad_lock"]]
        print(f"    {current:5d}  PLL {lock}  HTOTAL {state['htotal']}  VTOTAL {state['vtotal']}")
    return True, current


def set_by_nudges(host, count):
    """'/sc?n' count times. Slower, but it also runs updateClampPosition() and
    updateCoastPosition(), which no register endpoint reaches."""
    for _ in range(count):
        status, _ = get(host, "/sc?n")
        if status != 200:
            return False
        time.sleep(0.05)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--set", type=int, help="target PLLAD_MD (256..4095)")
    ap.add_argument("--raw", action="store_true",
                    help="always use the register path, never '/sc?n'")
    ap.add_argument("--step", type=int, metavar="N",
                    help="walk to the target N at a time, checking the unit after "
                         "each step; keeps the PLL locked so a big jump cannot "
                         "break sync and hang the firmware")
    ap.add_argument("--settle", type=float, default=1.0,
                    help="seconds to wait after each step (default 1.0)")
    args = ap.parse_args()

    before = read_state(args.host)
    if before["pllad_md"] is None:
        sys.exit(f"{args.host}: cannot read PLLAD_MD")
    print("before:")
    print("   ", describe(before).replace("\n", "\n"))

    if args.set is None:
        return

    if not 256 <= args.set <= 4095:
        sys.exit(f"PLLAD_MD {args.set} is outside 256..4095")

    delta = args.set - before["pllad_md"]
    if delta == 0:
        print(f"\nalready {args.set}")
        return

    if args.step:
        print(f"\nstepping {before['pllad_md']} -> {args.set} by {args.step} ...")
        ok, reached = set_by_steps(args.host, before["pllad_md"], args.set,
                                   args.step, args.settle)
        if not ok:
            sys.exit(f"stopped at PLLAD_MD {reached} -- power cycle and retry "
                     f"with a smaller --step")
    elif not args.raw and 0 < delta <= MAX_NUDGES:
        print(f"\n{delta} x /sc?n ...")
        ok = set_by_nudges(args.host, delta)
    else:
        print(f"\nwriting registers for {before['pllad_md']} -> {args.set} ...")
        ok = set_by_registers(args.host, args.set)
    if not ok:
        sys.exit("a write did not complete — check the unit")

    time.sleep(0.5)
    after = read_state(args.host)
    print("\nafter:")
    print("   ", describe(after))

    if after["pllad_md"] != args.set:
        print(f"\n!! PLLAD_MD reads {after['pllad_md']}, asked for {args.set} — "
              "the firmware may have reloaded the preset")
    if after["pllad_lock"] is False:
        print("\n!! PLL is not locked at this divider — back it out")


if __name__ == "__main__":
    main()
