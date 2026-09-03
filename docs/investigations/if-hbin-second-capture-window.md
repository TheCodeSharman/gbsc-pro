# `IF_HBIN_SP` is a second capture window, and nothing owns it

**Status:** the staleness is settled and reproduced; who should own the field is
open. The bench measurements below are all at 320x256@50 on the RiscPC, VTOTAL
311, `PATTERN PM5544`.

## The two windows

The input formatter blanks the source line twice, from two different registers,
and only one of them is derived.

| window | fields | owner | active span |
|---|---|---|---|
| set 2 | `IF_HB_SP2` .. `IF_HB_ST2` | `Geometry` — `InputLine::firstCapture()` .. `lastCapture()` | 80 .. 1124 |
| hbin | `IF_HBIN_ST` .. `IF_HBIN_SP` | nobody | 272 .. |

Both were read in one pass with `PLLAD_MD` 2250, `STATUS_SYNC_PROC_HLOW_LEN`
159, `IF_HSYNC_RST` 1125. That gives an IF line of 1126 units at a 7.07% hsync
duty, so `firstCapture()` is `ceil(1126 x 0.0707)` = 80 and set 2 is written at
exactly what the engine computed.

The hbin window stops 192 units later, and the engine does not know it exists.
**The framing the engine solves is therefore not the framing the chip applies.**

RD-5725-1.1 names `IF_HBIN_ST`/`IF_HBIN_SP` (s1_24, s1_26, 12 bits each)
*"horizontal blank for scale down start/stop position"* and, on the same rows,
*"horizontal blank for scale down line reset start/stop position"*.

## The scale-down path is idle, and the window still moves the picture

`IF_HS_RATE_SEG0`..`SEG7` and `IF_HS_RATE_LOW` are all 0, so no ratio is
programmed and the scaling-down DDA decimates nothing. `IF_SEL_HSCALE` 1 only
puts the block in circuit. So the register does capture-window duty under a
scaling name, and no resolution is lost through it.

Measured by writing the field on a settled unit, one change at a time, camera
fixed:

| written | picture |
|---|---|
| `IF_HBIN_SP` 272 | test card 700..1510 in the photo, panel edge at 1630 |
| `IF_HBIN_SP` 80 | test card 810..1620, hard against the panel edge |
| `IF_HBIN_ST` 0 -> 32, `SP` held at 272 | no movement |
| `IF_HB_ST`/`IF_HB_SP` (set 0) alone | no movement |

So `IF_HBIN_SP` alone carries it. The card keeps the same width in both, 810
photo px, so it is a **translation and not a change of magnification**.

`IF_LD_WRST_SEL` is 1, which RD-5725-1.1 documents as selecting the
hbin-generated line write reset over the line-generated one. Clearing it with
`IF_HBIN_SP` still at 80 does **not** move the picture back, so the write-reset
select is not the coupling — the blanking window itself is. `InputFormatter::init()`
is the only writer of `IF_LD_WRST_SEL`.

## The downscaler cannot simply be switched out

`IF_SEL_HSCALE` is a data-path selector, not a scaler enable. RD-5725-1.1,
s1_0b[6]: *"select the data path after horizontal scaling-down"* — 0 takes the
data and write enable **from CCIR** to the line double, 1 takes the
**scaling-down data and write enable**. Since nothing here scales down, 0 reads
like the correct setting. It is not, on two counts, both measured:

- **Colour breaks.** At `IF_SEL_HSCALE` 0 the geometry is untouched — the test
  card lands in exactly the same place, same width — but black renders green and
  blue renders magenta. The board feeds the 24-bit path (`IF_SEL24BIT` 1,
  `IF_SEL_656` 0), so there is no CCIR data to select.
- **It does not take hbin out of circuit.** With `IF_SEL_HSCALE` 0 and
  `IF_HBIN_SP` moved 272 -> 80, the picture still shifts right by the same
  amount. The blanking window gates the write enable either side of that
  selector.

So the second window cannot be disabled, only owned. **Two different bits were
tried and both are negative** — `IF_LD_WRST_SEL` (s1_28[1]), which is the one
named for choosing whether hbin generates the line write reset, and
`IF_SEL_HSCALE` (s1_0b[6]), which chooses whether the line doubler takes its data
and write enable from the scale-down block at all. Neither test included an IF
block reset, so if a selector only latches on one, both negatives would look the
same as a selector that does nothing.

## Why a mode change leaves the wrong value

Three writers, and the arm that needs the field writes none of it:

- `InputFormatter::init()` writes `IF_HB_ST` 2, `IF_HB_SP` 72, `IF_HBIN_ST` 0,
  `IF_HBIN_SP` 272. It runs from `BringUp::init()`, which
  `doPostPresetLoadSteps()` calls only `if (BringUp::armed())` — and only
  `setOutModeHdBypass()` and `bypassModeSwitch_RGBHV()` arm it. **A mode change
  does not.**
- `SourceStandard::applyProgressive()` writes `IF_HB_SP` 0 for every progressive
  standard, and then `IF_HB_ST` 30 / `IF_HBIN_ST` 0x20 / `IF_HBIN_SP` 0x60 for
  standard 3, a different trio for standard 4, and two of them for standard 8.
- `runSyncWatcher()`'s scaling-RGBHV arms write `IF_HBIN_SP` 0x50 at two sites,
  behind a `needPostAdjust` flag set when the wanted standard is 3.
- `SourceStandard::applySd()` writes **none of the four**.

So 800x600@60 lands on standard 3 and leaves `IF_HBIN_SP` at 80; returning to
320x256@50 lands on an SD standard, which leaves it there. Confirmed live on the
console and by field read: at 627 lines `IF_SEL_WEN` 1, `IF_PRGRSV_CNTRL` 1,
`IF_HBIN_SP` 80; back at 311 lines `IF_SEL_WEN` 0, `IF_PRGRSV_CNTRL` 0,
`IF_HBIN_SP` still 80.

A full 1536-register capture either side of the recovery differs in **8 bytes**:
the four above, plus `MADPT_Y_DELAY`, `VDS_Y_DELAY`, `VDS_V_DELAY` moving by one
and `SP_H_CST_SP` by seven, which are delay and coast noise. `/geometry` is
byte-identical in both states, so the engine's held framing is never what moved.

## Why reselecting the input recovers it

`applyInputSelection()` sets `rto->sourceDisconnected`, and the reconnect passes
through a bypass switch, which calls `BringUp::arm()`. The next load then re-runs
`InputFormatter::init()` and the field returns to 272. That is the whole of the
recovery: it does not repair a framing, it re-establishes a constant.

## Why the fix is a deletion

A per-standard branch is the wrong shape for these fields twice over. It is a
second owner of a derived quantity, and it keys on an aggregate the rest of the
design is dropping: what the chip is handed is an **input source** and an
**output resolution**, and a single number standing for both is what lets a
progressive arm and an SD arm disagree about who writes a capture window.

So the four writes come out of `SourceStandard::applyProgressive()`, and the two
`IF_HBIN_SP` 0x50 writes come out of `runSyncWatcher()` with the `needPostAdjust`
flag they hang off. That leaves one writer and a value that no longer depends on
which mode preceded this one. `test_source_standard.cpp` locks it: no standard
writes any of the four.

Deleting the unconditional `IF_HB_SP` 0 changes what standards 3, 4, 8 and 9 run
with — 72 rather than 0 — and that is a bench question, not a host one.

## What is not established

**Whether 272 is right.** `InputFormatter.cpp` records that `IF_HBIN_SP` had no
constant across the ten scaling tables — 136 to 272 — and no derivation, so 272
is inherited rather than computed. The value that reads as the fault, 0x50 = 80,
happens to equal `firstCapture()` for this source, which is the value that makes
the two windows agree. The state where they agree is the one with the picture off
the panel.

**Whether hbin is a capture-domain quantity at all.** A 192-unit change moves the
picture roughly 190 output pixels, read off a photograph with the panel spanning
about 1110 photo px. A shift of the capture window would be magnified by
`VDS_HSCALE` — 596 here, so 1.718x, or about 330 output pixels. The reading
therefore leans towards hbin *not* shifting the capture window, but it is a camera
estimate and cannot carry the decision.

What separates the two: creep `IF_HBIN_SP` across its range at two magnifications
and see whether the shift tracks `VDS_HSCALE` or stays 1:1. Three points minimum,
per `docs/scaler-geometry-model.md`. If it tracks the scale, the field belongs to
`Geometry` alongside set 2 and the engine's framing becomes true for the first
time; if it stays 1:1 it is a display-domain offset and belongs wherever the
output window is solved.

The `placePicture` centring failure is the same order of magnitude — it asks for
`VDS_HB_SP` = -37 and clamps at 8 — so an unaccounted 192 units is a candidate
explanation for it and should be re-checked once hbin has an owner.
