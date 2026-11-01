# The scaling-down path, which the engine can drive and does not choose

The part has a non-linear scaling-down engine on both axes, upstream of the
frame buffer and entirely separate from the VDS. The horizontal half is now
reachable and the block engages: `Tv5725::InputScale` states the ratio,
`InputFormatter::applyScan()` writes it with the scan and turns the block on
from the ratio rather than the scan, the line counter shortens with it so every
window placed in IF units follows, and the divider rises to spend the room the
counter has gained. `/inputscale` asks for one.

**Nothing chooses a ratio.** The solve still runs at unity, which is what the
bring-up wrote and what the block has always held. What a ratio would be chosen
FOR is sampling density, and how much the block's own filtering costs against
what the extra samples buy is unmeasured -- so the mechanism works and the
policy is absent.

This page is what it is, what it would buy, and what has to be measured before
any of that is believed. The vertical half is untouched.

## Why it matters that it exists

**The VDS cannot minify.** `VDS_?SCALE` divides 1024 and the field is ten bits,
so the least magnification it can express is 1024/1023 — `Scale::Max`. Three
separate difficulties in the engine come back to that, and all three are a
missing downscale:

- **The sampling divider is bounded by the output.** `PLLAD_MD` has to serve
  two masters — sample the source finely, and do not keep more units per line
  than the display window can show — because a surplus is cropped rather than
  scaled. `VideoPath::dividerCeilingForOutput()` exists only for that, and it
  costs resolution on any source whose line is long relative to the raster.
  `docs/known-issues.md` carries the grating that beats because of it.
- **A capture larger than the raster breaks the picture** rather than shrinking
  it: past the scale's floor every further unit of capture is a unit of
  produced picture, and the playback is asked to fetch more pixels per output
  line than the line has clocks.
  `investigations/the-capture-may-not-outgrow-the-raster.md`.
- **`produced` has no knob that does not move the picture.** It is
  `capture x 1024 / VDS_?SCALE`, and both terms belong to the framing, which is
  why the width parity is a ±1 bias on the memory window rather than something
  the solve satisfies by construction. `OutputWindow::solve()` gives a pixel
  away at the right-hand edge for it.

A downscaler before the frame buffer answers all three with one mechanism: the
count written to memory stops being the count the ADC kept.

## The registers

Horizontal, in the input formatter, ahead of the line doubler:

| field | | |
|---|---|---|
| `IF_HS_RATE_SEG0..7` | `s1_03..s1_0a` | each segment's DDA increment, bits [11:4] |
| `IF_HS_RATE_LOW` | `s1_0b[3:0]` | bits [3:0], **shared by all eight segments** |
| `IF_HS_DEC_FACTOR` | `s1_0b[5:4]` | `00` ratio over ½, `01` under ½, `10` under ¼ |
| `IF_SEL_HSCALE` | `s1_0b[6]` | 0 takes CCIR data to the line doubler, 1 takes the scaled-down data |

Vertical, in the MADPT block, the same shape:

| field | | |
|---|---|---|
| `MADPT_VSCALE_RATE_SEG0..7` | `s2_29..s2_30` | per-segment DDA increment |
| `MADPT_VSCALE_RATE_LOW` | `s2_28[7:4]` | the shared low bits |
| `MADPT_VSCALE_DEC_FACTOR` | `s2_31[1:0]` | the coarse step |
| `MADPT_Y_VSCALE_BYPS` | `s2_02[6]` | |
| `MADPT_UV_VSCALE_BYPS` | `s2_02[7]` | |

**The rate is stated by RD-5725-1.1 as a formula.** For a scaling ratio `n/m`:

    rate = 4095 x (m - n) / n

so `n == m` is 0 — no scaling — and a ratio of ½ is 4095, the whole 12 bits.
The DDA therefore spans 1.0x down to 0.5x, and `IF_HS_DEC_FACTOR` carries it
below that in halves.

**Eight SEGMENTS, because it is a non-linear scaler.** It is meant for
anamorphic stretch: each eighth of the line may scale differently. All eight
set to the same value is a linear downscale, which is the only use proposed
here. **How the segments divide the line is not stated in the register
definition** and has not been measured.

## What the firmware does today

`InputFormatter::init()` writes every `IF_HS_RATE_SEG` and `IF_HS_RATE_LOW` to
0, and `IF_SEL_HSCALE` to **1**: the scaled-down path is selected with a null
rate, which is a pass-through. `applyScan()` owns the eight segments from there
on and writes the held ratio with every scan, so the block and the counter
cannot disagree. `Deinterlacer::init()` writes the vertical rates to 0 and sets
both `MADPT_*_VSCALE_BYPS`; nothing reaches the vertical half.

**`Tv5725::InputScale` is the ratio.** `forRatio(wanted, have)` gives the
increment that shows `have` units of line in `wanted` of them, truncated so the
ratio lands at or above the one asked for — a larger increment is a narrower
picture, and a black bar is worse than an overrun. `unitsFor()` is what a count
in IF units becomes, which is how the counter follows.

**THE COARSE FACTOR IS THE BLOCK'S WRITE ENABLE AND THE FIFO IS ITS ONLY
CONSUMER.** RD-5725-1.1 gives `IF_HS_DEC_FACTOR` as the scaling-down engine's
coarse octave -- "if the scaling ratio is less than 1/2, use it and DDA to
generate the we and phase" -- and that sentence is about the write enable: a
DDA increment beside a zero factor does nothing whatever, measured. The enable
then needs somewhere for a sparse write to become a shorter line, which is the
line-double FIFO -- `IF_SEL_HSCALE` sends the scaled data and its write enable
there, and it is the one RAM in the horizontal path. **So the factor and
`IF_LD_RAM_BYPS` both follow the ratio now**, doubling being a second use of the
same RAM rather than its purpose, and `IF_HBIN_SP`/`ST` go with them because
what those two mean is decided by the bypass bit.

**The factor is an enable and not a second ratio term.** An increment of 0
leaves the block inert whatever the factor holds, which is what every
line-doubled source has run since the factor was written from the scan, so the
counter is derived from the DDA's ratio alone. **The quarter step is
uncharacterised**: factor 2 with a unity increment compressed nothing and with
the whole twelve bits gave a scale that fits neither it nor the half.
`investigations/the-minifier-engages-on-the-coarse-factor.md`.

```sh
curl 'http://<ip>/inputscale?rate=1365'          # the increment directly
curl 'http://<ip>/inputscale?wanted=3&have=4'    # or the ratio
curl 'http://<ip>/inputscale?rate=0'             # back to unity
```

## What it would buy, in the order the evidence should be taken

### 1. Sampling density stops being bounded by the output

The one already written down, and the one the mechanism now reaches.
`SamplingClock::recommendedDivider()` and both of `VideoPath`'s ceilings read
the counter's bound and the raster's back as a bound on the divider through
`InputFormatter::samplesForUnits()`, so a held ratio raises all three and the
solve spends the room on density. At 1280x720@60 into 1080p the solve installs
1444 and samples 0.875 of a source pixel; a ratio of one half reaches 2890 and
1.75, putting the same ~1445 units in front of the VDS either way.

**Ask for the ratio and let the solve choose the divider.** `/inputscale`
installs one rather than re-applying the scan at the divider in force, so the
capture window, both scales, the stride and the fetch all follow from one
decision; `/sampleclock?hold=N` is the same discipline from the other end. A
hand-set subset of an interdependent solve measures the mismatch instead, and
reading one back from a still has produced several wrong calls.

### 2. Every input and output combination can be made to fit exactly

With the capture free to hold more units than the raster shows, the pair
(capture, written count) can be chosen so the picture fills the output exactly
rather than being fitted by a scale that rounds. That is what makes a
full-screen framing exact rather than within a pixel.

### 3. The width parity could be satisfied by construction

The parity that reaches the picture is the **produced width in output pixels**
— an even one shears, an odd one is clean, and why is not known.
`investigations/horizontal-scale-corruption.md`.

Today the only way to move `produced` is to move the capture or the scale, both
of which move the picture, so the engine biases the memory window by a unit
instead and the aperture follows it down. With a downscaler the written count
is independent of the framing, so `produced` can be landed on an odd integer
without the picture moving at all, and the bias — and the pixel it costs at the
right-hand edge — goes.

**That is the one claim here that should be expected to REPLACE the bias rather
than build on it**, and it only earns that if the mechanism is understood. A
parity satisfied by construction that still shears has explained nothing.

## What the bench said while the block was disabled

Every reading in this section was taken with `IF_HS_DEC_FACTOR` at 0, the FIFO
bypassed, or both, which is what `applyScan()` wrote for an undoubled source
until the enable followed the ratio. **They are kept for their register tables
and their controls, and none of them is evidence about the block.**

**Driven through the solve, every ratio tried breaks the frame.** `/inputscale`
re-solves from one decision, so this is the whole solve rather than a hand-set
subset of one. Measured off the USB capture, RiscPC on `vga` at 320x256@50:

| ratio | increment | picture |
|---|---|---|
| 1.00 | 0 | clean |
| 0.98 | 83 | broken |
| 0.95 | 215 | broken |
| 0.90 | 455 | broken |
| 0.80 | 1023 | broken |
| 0.75 | 1365 | broken |

**The geometry is not what is wrong.** A full snapshot diff across unity and
0.75 moves 17 fields and every one is the ratio applied: `IF_HSYNC_RST`
1100 -> 825, `IF_HB_SP2`/`IF_HB_ST2` 236..926 -> 177..695, `IF_LINE_SP`
1165 -> 890, `PB_CAP_OFFSET` 276 -> 207, `PB_FETCH_NUM` 173 -> 130, `VDS_HSCALE`
563 -> 422, and the eight segments at 85 with the shared low nibble 5, which is
0x555 = 1365. `IF_SEL_HSCALE` is already 1, so the scaling-down data path is
selected, and `IF_HS_DEC_FACTOR` 0 is right for a ratio above a half.

**It is reversible, and the control is clean.** Unity taken either side of a run
restores the picture exactly, the emitted extents and the frame's detail
returning to the same numbers.

**It is not a soft picture.** The upper part of the frame is roughly the card and
the lower part is noise, with a green cast over the middle. On a PROGRESSIVE
source, 800x600@60, the card is instead duplicated across the line with black
between the copies -- **so the line doubler is not the cause, both scan types
break**. On that source `VDS_HSCALE` also stays where it was, at 803, where the
doubled source's followed the ratio, which is a second thing to account for.

**The picture is the instrument, and automated frame metrics mis-read this three
ways.** A mean column-to-column difference reports a destroyed frame as 40% less
detail. A count of dark gaps inside the lit span calls every ratio intact,
because the noise fills the columns. A per-row noise threshold fires on the
card's own frequency wedge in a frame that is clean. Judge by eye, or build a
metric against a frame already known to be broken.

The frame broke because the counter had been shortened against a line the block
was still emitting whole, which is the two halves of one setting written apart.
An earlier reading that the block destroys the picture, and a later one that the
count question is thereby moot, are both superseded.

## What the block does when it is enabled

With the write enable set and the FIFO in circuit on a line-doubled source the
picture compresses with the ratio: at `PLLAD_MD` 2200, the counter held at 1100
and the VDS at 1:1, a DDA of 4095 puts the whole card inside a third of the
width -- circle, colour bars, grating, greyscale and both captions legible in a
narrow column -- and the same DDA with the coarse factor at 0 changes nothing.

Six registers were then flipped to the values the undoubled branch would write,
individually and all at once, and **not one of them breaks it**, so the FIFO
wants none of the line-doubled routing.

The solid green an undoubled source gave with the FIFO forced in by hand was the
capture window spanning a line that no longer existed: halving `IF_HSYNC_RST`,
`IF_HB_SP2`/`ST2`, `IF_LINE_SP`/`ST`, `PB_CAP_OFFSET` and `PB_FETCH_NUM` with it
gave a picture. Every one of those is derived from the counter, so the engine
cannot leave them behind -- which is why this finished in `applyScan()` and the
geometry it feeds rather than in register writes.
`investigations/the-minifier-engages-on-the-coarse-factor.md` has the tables and
the controls either side.

## The minifier works; what is left is the FIFO's read side

Measured on `vga` at 1280x720@60 into 1080p with the divider pinned by
`/sampleclock?hold=`, so only the ratio moves. At a held 1444 with a ratio of
three quarters the counter is 1083 and the frame carries **no shear**, which
says the block emitted exactly `1444 x 0.75` units: **the ratio is the DDA's
and `lineCounterFor()` is right as it stands** -- no halving from the FIFO, no
second halving from the coarse factor.

**The FIFO is required and `IF_HS_PSHIFT_BYPS` is not an alternative to it.**
With the FIFO bypassed, the coarse factor at 0 and the phase-adjustment path in
circuit -- which is what RD-5725-1.1 describes for a ratio above a half -- the
same counter gives a constant slip per row, so the line was still the full
1444 units and nothing scaled. And at unity with the FIFO forced in the frame
is solid green: `IF_SEL_HSCALE` hands the FIFO the block's data AND its write
enable, so a null rate leaves it writing nothing. Every earlier green frame was
that.

**What is left is vertical and it is the read side.** With the counter correct
and the line emitted correctly the card appears as two stacked copies, and
`IF_LD_SEL_PROV`, `IF_PRGRSV_CNTRL` and `IF_SEL_WEN` were all at their undoubled
values throughout. The lead is `IF_LINE_ST`/`IF_LINE_SP`, the FIFO's read
window, which the engine places past the end of the counter -- 64 / 1148
against a wrap at 1084, and 64 / 1509 against 1445 at unity. Bypassed, the pair
reaches nothing and the placement has never mattered; in circuit it is the read
reset, and a stop the counter never reaches never fires.

So **minifying does not require doubling**, and the payoff is not bounded by
what the output can afford to line-double.
`investigations/the-minifier-engages-on-the-coarse-factor.md` has the tables.

## What has to be established first

- **Why the FIFO puts out two lines per source line, and how to stop it.** The
  one blocker. `IF_LINE_ST`/`IF_LINE_SP` is the read window and the engine
  writes a stop the counter never reaches; nothing among the three routing
  registers changes the doubling.
- **Where the FIFO's line reset belongs on a minified line.**
  `InputFormatter::LineDoubleReset` is measured on doubled modes only. Wrong, it
  pans.
- **Where the eight segments divide the line**, since a linear scale needs all
  eight and a wrong boundary shows as a discontinuity partway across.
- **Whether the vertical scaler is reachable on an undoubled path at all.** It
  lives in the deinterlacer, which an undoubled source otherwise skips.
- **What it costs in sharpness.** Unmeasurable until a frame survives a ratio.
  Near-Nyquist detail does not fall when the rate registers are written alone,
  so the filtering a ratio was thought to cost is not established either.

`investigations/the-scaling-path-samples-below-one-to-one.md` is what the
missing downscale costs, measured against pass-through.
