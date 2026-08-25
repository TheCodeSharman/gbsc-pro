# The input formatter's vertical blank strands the measurement that would replace it

**Status:** closed. The cause is established by single-register experiments in
both directions, and the fix is in `Geometry::holdReferenceSampling()`.

A source mode change into a frame with FEWER lines than the last one leaves the
geometry engine unable to measure the source, permanently. The solve never
completes, so capture stays frozen and the console repeats
`524 lines x 0.00 Hz -> line rate 0` and `no INPUT vsync` for as long as the unit
is left in that state.

## The circularity

Two quantities come out of the input formatter, and both are gated by its
vertical blanking window:

- the field rate, timed at `DEBUG_IN_PIN` off the block's test bus
- `HPERIOD_IF`, the line period, counted inside the same block

`IF_VB_ST` is written by `Geometry::write()` and by nothing else, and
`Geometry::write()` runs only after a solve has succeeded. A window whose start
lies beyond the frame never fires, so the block emits nothing to measure — and
the register that would put it back is only written once the measurement it
prevents has succeeded.

**Only the shrinking direction fails**, which is the signature. A taller frame
still contains the previous mode's window, so 524 → 311 recovers every time
while 311 → 524 strands: 311 lines line-doubled is a 622-line IF frame and
solves to `IF_VB_ST` 578, which is beyond the 524 lines the next mode presents.

## The evidence

One register, both directions, on a live instance:

| `IF_VB_ST` | source | `HPERIOD_IF` | test-bus vsync |
|---|---|---|---|
| 578 | 524 lines | 119 (50 on another run) | `no INPUT vsync` ×6 |
| 522 | 524 lines | 213, 212, 213 | `periodInput` ×5, 60.000870 Hz |

Writing 522 alone recovers a stranded unit with nothing else touched. Driving a
healthy unit the other way is the same result: at 311 lines, `IF_VB_ST` 700 is
beyond the 622-line IF frame and kills the test-bus vsync outright, while
`HPERIOD_IF` and `VPERIOD_IF` stay correct at 431 and 623 — so the two paths do
not fail together at every window, and the window has to be judged against the
frame rather than against a threshold.

The whole-chip diff either side of a `/sc?~` recovery is 29 registers, and every
sampling-clock register — `PLLAD_MD`, `PLLAD_CKOS`, `PLLAD_KS`, all four
decimators, `IF_HSYNC_RST`, `IF_PRGRSV_CNTRL` — is **identical** in the stranded
and recovered states. Nothing in the clock chain is involved.

## Why the reset path clears it

`setResetParameters()` writes `IF_VB_ST` 0 and `IF_VB_SP` 2, a window inside
every frame any source presents. That is the whole of why `/sc?~` clears the
fault, and why entering a 524-line mode from a boot measures fine while the
transition into it does not: the reset path runs at boot and parks the block in
a measurable state, and the first solve then writes a correct window for the
mode.

Nothing else in the firmware writes those two registers.
`InputFormatter::init()` writes the horizontal blanking sets and skips the
vertical one.

## Why it belongs in the reference

`holdReferenceSampling()` exists to put the sampling chain into a state the pass
chose before anything is timed through it, which is why the divider is parked at
the capture write limit rather than inherited. The vertical blank is part of that
chain on exactly the same reasoning, so it is parked with it, and the literals
live in the block that owns the registers so there is one owner rather than two.

## What this is NOT

**`HPERIOD_IF` railing is a separate, open fault.** The state saved as
`snapshots/hperiod-railed-latched-2026-08-20.json` has `IF_VB_ST` 578 against a
line-doubled 311-line source — a 622-line IF frame, so the window is IN range —
and it returns different garbage on almost every read rather than one stable
wrong value. It clears only on a cold boot. After this fix, a settled 524-line
source still reads a steady `HPERIOD_IF` of 50 with `IF_VB_ST` correct at 522
and the picture and the field rate both healthy. The two must not be merged.
[`hperiod-if-railing.md`](hperiod-if-railing.md) is unchanged by this.

**It is not a reason to trust `HPERIOD_IF` as the measurement.** It is a hardware
line-period counter, independent of the divider — proven by a reading of 213
taken while the divider was still the previous mode's 2250 — and so it is an
attractive replacement for timing edges at `DEBUG_IN_PIN`. It is not usable as
one until the railing above is understood, because it fails silently: a stable
wrong value that every stability check passes.
