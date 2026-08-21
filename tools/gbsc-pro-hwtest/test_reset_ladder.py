"""reset_ladder's bit masks, against the register catalogue. No hardware."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reset_ladder


def test_the_masks_cover_every_documented_block_reset():
    masks = reset_ladder.reset_masks()
    covered = sum(bin(m).count("1") for m in masks.values())
    assert covered == len(reset_ladder.RESET_BITS) == 12, (
        f"{covered} bits masked for {len(reset_ladder.RESET_BITS)} named resets")


def test_the_masks_leave_the_undocumented_bits_alone():
    """s0 0x46 bit 7 and s0 0x47 bits 5-7 are named nowhere in RD-5725-1.1.
    Holding an unnamed bit low is not a reset, it is a guess with the picture
    on the other end of it."""
    masks = reset_ladder.reset_masks()
    assert masks[0x46] & (1 << 7) == 0, "0x46 bit 7 is not documented"
    for bit in (5, 6, 7):
        assert masks[0x47] & (1 << bit) == 0, f"0x47 bit {bit} is not documented"


def test_clearing_preserves_everything_outside_the_mask():
    masks = reset_ladder.reset_masks()
    held = reset_ladder.held_in_reset(0xFF, masks[0x46])
    assert held | masks[0x46] == 0xFF, "cleared a bit outside the mask"
    assert held & masks[0x46] == 0, "left a reset bit set"
