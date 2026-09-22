# The capture tail was one unit short, and the two counters zero differently

`VideoSourceLine::lastCapture()` returns `units - 1`. It used to return
`units - 2`, which cost a sample of the front porch on every line and a line of
the frame on every field.

The unit it recovers is not the interesting part. What the measurement settled is
where each of the input formatter's counters takes its zero, and those turn out
not to match.

## The horizontal counter zeroes on the hsync pulse's LEADING edge

So units `0 .. HLOW_LEN` are the pulse itself, and a capture opened there takes
sync rather than frame.

Measured at 800x600@60 into 1080p on a 1439-unit line, with `IF_HB_SP2` forced
to 3 so the window opens inside the pulse. The source is DMT 9 — sync 128, back
porch 88, active 800, front porch 40, total 1056 — so on this line the pulse is
174.3 units and sync-plus-porch is 294.3.

The black before the card's first active pixel measures **405 photo columns**,
against three bands agreeing the first active pixel sits at photo x 452.3 and
the panel's left edge at 47.

| counter 0 is | black before first active pixel | predicted |
|---|---|---|
| hsync **leading** edge | sync 176 + back porch 118 = 294 units | **402 columns** |
| hsync **trailing** edge | back porch 118 units | 160 columns |

`STATUS_SYNC_PROC_HLOW_LEN` measures the pulse directly — 176 units here — and
`firstCapture()` adds `FirstCapturableUnit`, so the engine opens the capture at
177, on the back porch. That exclusion is necessary and correct.

## The vertical counter zeroes on the vsync pulse's TRAILING edge

So the frame reads back porch `0..22`, active `23..622`, front porch `623`,
vsync `624..627`. The pulse is at the **tail**, not the head.

Measured from a 100% framing photograph with the window at `IF_VB_SP` 0 ..
`IF_VB_ST` 626 — 626 captured units containing 600 active lines, so **26 units
of blanking, known exactly**. That known total is what makes the reading
self-calibrating, which matters because the panel's own edge is not the
aperture's: the MS9288A re-encodes and chooses its own active window.

The card supplies the reference. `PatLib.bas`'s `PROCcastell` draws its top row
at `y 0` and its bottom at `H% - d%`, with the corners stated outright at
`(0,0)` and `(0, H%-d%)` — so the pattern has no margin of its own and its first
and last bright rows are source lines 0 and 599. Measured 34.87 photo rows of
black above the card and 4.49 below, normalised to the known 26 units:

| | top | bottom |
|---|---|---|
| measured | **23.1** | **2.9** |
| counter 0 = vsync **trailing** edge | 23 | 3 |
| counter 0 = vsync **leading** edge | 27 | 0 |

## What follows

**The vertical head needs no sync exclusion.** `IF_VB_SP` 0 already opens on the
back porch. The asymmetry against the horizontal is real but is not an omission,
and the absence of any vertical equivalent of `HLOW_LEN` does not cost anything
at the head.

**The vertical tail cannot avoid the pulse.** The tail order is active, front
porch, vsync, so a capture that reaches the end of the front porch necessarily
takes sync lines with it. A 100% framing therefore shows a **bright band across
the bottom of the picture**: the vsync tip sits below black and comes back
through the ADC clamp as a saturated line. At `IF_VB_ST` 626 that is three sync
lines, which on the bench calibration is 4.0 photo rows, and the band measures
about four. It is the marker of a frame captured to its end, not a fault.

**A stop on the wrap point is not available.** `IF_VB_ST` 628 on this frame loses
sync lock at once, where 627 holds a clean picture.

## Why the vsync width is not excluded instead

Two routes were checked and neither works.

**No register measures it.** RD-5725-1.1's sync processor status block runs
S0_17/18 H total, S0_19/1A H low pulse length — documented as *for H-sync
polarity detection*, which is why the chip counts it at all — and S0_1B/1C V
total. S0_1D and S0_1E, where a vertical equivalent would sit, are **RESERVED**.
The part never needs the vsync width, so it never counts it.

**A fixed bound taken from the standards narrows the picture.** Across the 80
modes of `RetroScaler-Acorn.mdf` the vertical sync width runs 2, 3, 4, 5, 6 and
7 lines. A fixed 7-line exclusion clears the pulse everywhere, but because the
tail order is active → front porch → vsync it removes the front porch first and
then eats active: on DMT 9, whose vsync is 4 and whose front porch is 1, it
takes **two lines of picture**. Narrowing to hide blanking is the wrong
direction.

Measuring it on the ESP remains open. `SP_TEST_MODULE` 4 puts `vs_act_det` onto
`DEBUG_IN_PIN`, which `framesync.h` already samples for the vsync period, so
timing the pulse width is an extension of machinery that runs — at the cost of a
busy-wait in `loop()` and contention for the pin. It buys three lines of black at
one diagnostic framing.

## A freeze this overturns

A stop at `units - 1` was once measured as freezing the picture: on a 1265-unit
line a stop of 1264 held the last frame the buffer had, with every config
register reading correct and the firmware loop still running, where 1263 was
clean. That reading is what put the constant at `units - 2`.

It was taken with `SP_HS_LOOP_SEL` 1 — the sync processor's retiming module
bypassed — which left the input formatter's line counter running ahead of the
video, so `units` did not then mean what it means now.
`docs/investigations/the-capture-lag-was-the-retiming-bypassed.md`.

**If a stop at `units - 1` freezes the picture again, read `SP_HS_LOOP_SEL`
first.** Nothing in a register dump distinguishes the frozen state, and that bit
is what moved underneath it.
