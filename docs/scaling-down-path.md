# The scaling-down path, which the engine can drive and does not choose

The part has a non-linear scaling-down engine on both axes, upstream of the
frame buffer and entirely separate from the VDS. The horizontal half is now
reachable: `Tv5725::InputScale` states the ratio, `InputFormatter::applyScan()`
writes it with the scan, and the line counter shortens with it so every window
placed in IF units follows. `/inputscale` asks for one.

**Nothing chooses a ratio.** The solve still runs at unity, which is what the
bring-up wrote and what the block has always held. Making it automatic needs the
capture window, both scales and both output windows to come from one decision
with the ratio, and what the block does to the count has to be measured first.

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

**The coarse factor is NOT part of it.** `IF_HS_DEC_FACTOR` is the line
doubler's halving, and one field cannot carry two meanings — so the reachable
range is the twelve-bit DDA alone, 1.0x down to 0.5x.

```sh
curl 'http://<ip>/inputscale?rate=1365'          # the increment directly
curl 'http://<ip>/inputscale?wanted=3&have=4'    # or the ratio
curl 'http://<ip>/inputscale?rate=0'             # back to unity
```

## What it would buy, in the order the evidence should be taken

### 1. Sampling density stops being bounded by the output

The one already written down. `docs/known-issues.md` proposes raising the
divider past `SamplingClock::recommendedDivider()`'s ceiling and letting
`IF_HS_DEC_FACTOR` take the difference, judged on the finest grating of the
test card at a fixed framing. **It names only the coarse factor**; the 12-bit
DDA beside it is finer and is the better tool, since the ratio wanted is rarely
a half.

This is the cheapest experiment and it needs no engine change — the registers
can be written by hand against a frozen framing.

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

## What the bench says: the block breaks the picture at every ratio

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

**So the solve must not choose a ratio yet**, and the question below about what
the DDA does to the count is answered in the only sense that matters: whatever
it does to the count, the frame does not survive it.


## What has to be established first

- **What the DDA actually does to the count.** Write a known ratio against a
  frozen framing and count the units that reach the frame buffer, rather than
  trusting the formula. A read-back proves nothing here; only the picture and
  the geometry can say.
- **Where the eight segments divide the line**, since a linear scale needs all
  eight and a wrong boundary shows as a discontinuity partway across.
- **Whether the vertical scaler is reachable on a progressive path at all.** It
  lives in the deinterlacer, which a progressive source otherwise skips.
- **What it costs in sharpness.** A decimating DDA is not a filter; scaling a
  line down before the frame buffer may alias where the VDS's interpolator
  would not.
