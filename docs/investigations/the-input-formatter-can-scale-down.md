# The input formatter can scale down, and nothing uses it

`VDS_?SCALE` divides 1024 and tops out at 1023, so the video display scaler
cannot minify at all: the picture it produces is always at least as wide as the
capture it was given. That bound is what refuses a declared shape on a source
whose capture is wider than the narrowed room — 800x600@60 into 1080p asks for
1042 output units against a capture of 1090. [aspect-ratio.md](../aspect-ratio.md).

**It is not the part's bound.** The input formatter carries its own horizontal
scaling-down block, ahead of the VDS, and `Axis::scaleCeiling()` already says so.
There is a vertical twin in the deinterlacer, `MADPT_VSCALE_RATE_SEG0..7`.

## What the block is

Eight segment DDA increments — `IF_HS_RATE_SEG0..7`, s1_03..s1_0a — each the top
eight bits of a twelve-bit value whose low nibble they share in
`IF_HS_RATE_LOW`, s1_0b[3:0]. Eight of them because the scaling is
**non-linear**: each segment of the line may be scaled differently, which is
what an anamorphic stretch wants. Writing one value to all eight is the linear
case.

RD-5725-1.1 states the increment for a scaling ratio of `n/m` as
`4095 x (m - n) / n`, so the ratio a value `v` gives is

```
ratio = 4095 / (4095 + v)
```

`IF_HS_DEC_FACTOR`, s1_0b[5:4], is a coarse range beside it — 00 above a half,
01 below a half, 10 below a quarter. `IF_SEL_HSCALE`, s1_0b[6], selects the
scaled data path.

## What the firmware does with it today

`InputFormatter::init()` writes all eight segments and the shared nibble to
**zero**, which is a ratio of 1:1, and `IF_SEL_HSCALE` to **one**. So the block
is in circuit and idle.

The one part in use is the coarse factor: `IF_HS_DEC_FACTOR` is written
`lineDoubled ? 1 : 0`, which is the halving that makes an IF unit two ADC
samples on a doubled line and one on an undoubled one — the relationship
`SourceMeasurement::ifLineFor()` states.

## What the bench says

Measured with automation frozen, RiscPC 800x600@60 on `vga` into 1080p, the card
pattern with its animation off, read off the USB HDMI capture. The signature is
the column profile of the emitted frame — where its strong luma gradients sit —
because the picture's **extent** cannot see this at all.

| `IF_SEL_HSCALE` | DDA | `IF_HS_DEC_FACTOR` | mean luma | first profile edges |
|---|---|---|---|---|
| 1 | 0 | 0 | 78.23 | 5, 6, 7, 8 |
| 1 | 1365 | 0 | 77.69 | 4, 10, 11, 12 |
| 1 | 1365 | 1 | 75.28 | 27, 28, 31, 33 |
| 1 | 0 | 2 | 73.8 | 44, 45, 46, 86 |
| 0 | 0 | 0 | 73.23 | 2, 3, 5, 34 |
| 0 | 1365 | 0 | 73.23 | 2, 3, 5, 34 |
| 0 | 1365 | 1 | 73.25 | 2, 3, 5, 34 |

Three things follow.

**The block reaches the picture.** At `IF_SEL_HSCALE` 0 the frame is invariant to
both controls, to two decimal places of mean luma and to the column; at 1 it is
not. The selector is the path, and it is already on.

**The DDA acts on its own, at `IF_HS_DEC_FACTOR` 0.** 1365 is the increment for a
ratio of three quarters, and it moves the profile without the coarse factor.

**The picture's EXTENT does not move**, at any of these settings: content spans
the emitted frame at every row of the table. That is the trap and the finding.

## Why the extent does not move, and what follows

The capture window is **downstream** of the scaler. `IF_HB_SP2`/`IF_HB_ST2` are
placed in IF units, and the scaler decides how much of the source an IF unit
holds — so scaling down compresses the source into fewer units while the window
still spans the same ones, and the VDS still magnifies that span across the same
aperture.

**So the block cannot be judged by hand, and a measurement that only asks where
the picture starts and stops is blind to it.** Narrowing `IF_HB_ST2` by the same
ratio does not complete the experiment either: the VDS scale and both output
blanking pairs are frozen with it, so the write covers less of an unchanged
aperture and what shows beyond it is unwritten memory. This is the standing rule
about interdependent solves, and it applies here exactly.

To use the block, the solve has to own it: the ratio, the capture window, the
VDS scale and both output windows together, from one decision.

## What it would buy

**Minification, which the part is otherwise without.** A shape needing 1042
output units from a 1090-unit capture needs a ratio of 0.956 — an increment of
187 — after which the VDS magnifies 1042 to 1042 at unity. The cost is 4% of the
captured samples, against a stretch of 32% for filling instead.

**Arbitrary oversampling.** The sampling divider is chosen to maximise the kept
count, and what it can keep is bounded by the line counter's eleven bits and by
what the row binds. A scaler after the ADC decouples the two: sample the line as
densely as the counter allows, then decimate to whatever the output wants, with
the filtering in the digital domain rather than in the choice of divider. Whether
that is measurably sharper than sampling at the target rate is untested and is
the reason to build it.

**Neither is proven.** What is proven is that the block is in circuit, that the
DDA reaches the emitted frame, and that the capture window sits after it.
