# The clamp had two owners and two units

A black bar on every line of the Wii's 480p picture, 8% in from the left, with
the few columns beside it shimmering. It is the ADC clamp window sampling the
black level off picture instead of off the back porch, and it is reached by two
writers of `SP_CS_CLP_ST`/`SP_CS_CLP_SP` disagreeing about what the number
counts.

## What the register counts

**The source's own pixel, at the chip's 27 MHz reference.**
`SyncProcessor::clampFromReferenceClock()` selects that clock, and walking the
register against the emitted frame confirms it. Measured on the Wii at 480p into
1920x1080, `PLLAD_MD` 1212, `VDS_HSCALE` 746, automation frozen:

| `SP_CS_CLP_ST` | the bar's emitted column |
|---|---|
| 108 | 160 |
| 160 | 295 |
| 220 | 459 |
| 300 | 672 |

**2.667 emitted columns per count**, against **1.885 per ADC sample** and
**2.665 per source pixel** at that state. The three are distinguishable because
the divider is not an integer multiple of the source's pixel rate: 1212 ADC
samples span a line of 858 source pixels. So the register is neither in ADC
samples nor a fraction of the divider.

Where the back porch is, in the register's own counts, by reading the dark run
either side as the window walks: at a start of 65 and below the picture holds a
mean of 205 and its black floor reads 13.7; from 70 up the mean falls to 152-176
and the floor clips to 4.4, which is the whole frame being pulled down to make
picture black. **The usable range on this source is 0..66 and the picture starts
at 67.**

Opening the capture across the whole line -- `IF_HB_SP2` 2 -- puts the sync tip,
the back porch and the picture on one frame, and the two blanking levels are 8
apart: the sync tip ends at capture unit 59 and the source's content starts at
165. A start of 108 therefore lands 41 source pixels inside the picture.

## What it costs

Two things, and only the first is obvious:

| | clamp 108 | clamp 19 |
|---|---|---|
| the bar | 10.4 against 124.2 local | none |
| picture mean | 152.4 | 205.7 |

So the bar is the visible half and the clamped-dark frame is the other: a clamp
on picture takes the black level off whatever is under it, and the whole image
moves with it.

## The two owners

`VideoPath::applySamplingClock()` passed the **ADC divider**, giving
`1 + 1212 x 0.089 = 108`. `VideoSourceAcquisition::placeClampWindow()` passed
the quarter-count the fractions were fitted to,
`27e6 / (4 x lineRate) - 1 = 213`, giving `1 + 213 x 0.089 = 19`. Both are
correct for the fraction they were fitted against and only one of them is where
the back porch is; which one the register holds is whichever wrote last.

**On separate sync the two agree**, because both reduce to the divider there, so
the divergence is specific to a composite or sync-on-green source -- which on
this bench is the Wii alone.

The conversion lives inside `SyncProcessor` now and neither entry point takes a
line: both take the rate and the divider.

## The VDS line filter is refuted as the cause

It is what the artefact was first filed against, because the bar comes and goes
with `/uc?m`. Writing `VDS_D_RAM_BYPS` directly at one acquisition settles it --
the filter in circuit and bypassed, nothing else touched:

```
col     filter IN   filter OUT
155         16.4        16.5
156         13.1        13.0
160         10.3        10.3
```

and the frame-to-frame standard deviation of the shimmering columns beside it is
the same to a tenth either way. **The bar is there with the filter out.**

The correlation is through the divider. `VideoPath::dividerCeilingForLineFilter()`
caps the divider so the capture fits the filter's 1020-sample line buffer, so
turning the filter on moves the divider -- 1446 to 1212 here -- and the divider
is what the broken writer placed the clamp from. A change of picture option is
not a reason to doubt a picture option.

`the-tail-green-is-the-vds-line-filter.md` is the artefact the filter does own,
at the END of the line, and it is a different one.

## What is not settled

**Where the register's origin is.** Count 0 sits about 12 ADC samples after the
sync pulse ends on this source, which is consistent with the retimed pulse
rather than with the incoming edge, and one source cannot separate them. It
matters because the fractions are of a line the origin is not the start of:
`ClampStartCsyncComponent` of 0.089 against the quarter-count is 19 counts here
and would be 76 against the whole line, which is past the back porch. The
fractions are fitted, not derived, and the second source needed to derive them
is a composite one at a different line rate.

`known-issues.md` carries the clamp landing inside the sync pulse on both
branches, which is the same fractions read at the other end of their range.
