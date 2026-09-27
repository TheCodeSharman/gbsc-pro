# The scaling-down path, which the firmware does not use

The part has a non-linear scaling-down engine on both axes, upstream of the
frame buffer and entirely separate from the VDS. The firmware initialises every
one of its rate registers to zero and bypasses the vertical half, so nothing on
the bench has ever run through it.

This page is what it is, what it would buy, and what has to be measured before
any of that is believed. Nothing here is measured yet.

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
0, and `IF_SEL_HSCALE` to **1**. So the scaled-down path is selected with a
null rate, which is a pass-through — not a defect, but not obviously deliberate
either. `Deinterlacer::init()` writes the vertical rates to 0 and sets both
`MADPT_*_VSCALE_BYPS`.

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
