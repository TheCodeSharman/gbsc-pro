# The capture stop must be a unit the counter reaches

`CaptureWindow::lastCapture()` returns `units - 2`. It returned `units - 1`,
which is the input formatter's line total itself — a value the counter never
equals, so a window stopped there never closes. On the line-doubled path the
capture then stops being written coherently: the picture freezes and the frame
is played out twice down the output.

## The counter, and where the extra unit came from

`InputFormatter::applyScan()` writes `IF_HSYNC_RST` with the line counter and
holds `lineUnits_ = counter + 1`. `IF_HSYNC_RST` is RD-5725-1.1's *"total pixel
number per line"*, so a counter written with 1100 runs **0..1099** and the span
is 1100. Against `lineUnits_` of 1101 the three bounds resolve as:

| | value at `IF_HSYNC_RST` 1100 | the counter reaches it |
|---|---|---|
| `units - 1` | 1100 | **no** |
| `units - 2` | 1099 | yes |

## Measured

RiscPC on `vga` at 320x256@50, `SYNC 0`, `PATTERN CARD`, engine-solved at a
forced 100% framing, then automation frozen and `IF_HB_ST2` the only variable.
Read off the emitted frame with `hdmi_capture.py`, scored by counting the card's
one red block:

| `IF_HB_ST2` | `IF_HSYNC_RST` | emitted |
|---|---|---|
| **1100** | 1100 | **two cards**, the upper one cut, the card's own animation stopped |
| 1099 | 1100 | one card, complete, animating |
| 1098 | 1100 | the same |

The stopped animation is the diagnosis: the source keeps drawing, so a frozen
picture is the write side rather than the read side.

**Only the extreme of the range reaches it**, which is why the default framing
has never shown it. The default takes 94% of the counter and stops well short;
`IF_HB_ST2` lands on the total only at a framing that asks for the whole line.
One zoom step in is clean for that reason alone, not because the step changes
anything else — the two states differ in `IF_HB_ST2` and `VDS_HSCALE` and in
nothing else across all 1536 registers.

## What it refutes

[the-capture-tail-was-one-unit-short.md](the-capture-tail-was-one-unit-short.md)
moved the bound to `units - 1` on the reading that the earlier `units - 2` was a
correction for the sync-processor retiming being bypassed. The retiming half of
that page stands; **the tail half does not.** Its verification was taken at
800x600@60, an undoubled source, which tolerates a stop on the total — the fault
needs the line doubler in circuit.

**The unit recovered was never picture.** Horizontally it is the last sample of
the front porch and vertically a line of the vsync pulse, so one rule serves both
axes and both scan modes rather than a scan-dependent bound that would buy
blanking at the price of a branch.

## Scoring it

The green frame's extent cannot see this. A second copy landing inside the first
one's bounding box leaves the outermost green where it was, and several readings
taken that way reported a doubled frame as clean. **Count a feature that occurs
once per copy** — the card's single red block — and the answer is a number.
