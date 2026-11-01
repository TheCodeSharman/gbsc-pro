# The scaling path samples below one to one, where pass-through does not

At 1280x720@60 the scaling path asks the ADC for **1444** samples per line and
pass-through asks for **2038**, on the same part, the same source and the same
cable. 720p is 1280 active pixels in a line total of 1650, so those are **0.875**
and **1.24** ADC samples per source pixel: the scaling path samples the source
below one to one and destroys horizontal detail before anything reaches the
frame buffer.

Nothing about the ADC causes it. `Adc::MaxSampleRateHz` is 162 MSPS and the
scaling path spends 130 of it. The ceiling is `VideoPath::dividerCeilingForOutput()`,
which exists because **the VDS cannot minify**: a capture holding more units
than the display window shows is cropped rather than shrunk, so the divider is
held down to whatever the output raster can carry. Pass-through has no such
bound, because it does not scale -- the source's raster reaches the encoder
unchanged, and its only limit is `HD_HSYNC_RST`, the bypass channel's own
eleven-bit counter, measured at 2046 and hard against the wall.

## What the picture shows

PM5544's `PROCbandgrate` draws five frequency bundles across the chord, the
leftmost finest. The bundle still showing bars names the sampling limit the
line reached.

| state | `PLLAD_MD` | samples/px | oversample | finest bundle |
|---|---|---|---|---|
| scaling, solved | 1444 | 0.875 | 2x | flat grey |
| scaling, divider held | 2047 | 1.24 | 1x | bars |
| pass-through | 2038 | 1.24 | 1x | bars |

The same holds on `CARD`, whose wedge is one band chosen per mode. At 720p it
draws BAND 22, which reads as a featureless grey bar at 1444 and as a striped
grating at 2047 -- a fixed crop of the wedge gives a high-frequency rms of
**3.32** against **10.76**.

**The scaling path at a raised divider matches pass-through**, which is what
rules out every explanation that is not the divider. Taken at 16:9 with the
framing solved, animation off.

## Oversampling is not the lever, and the asymmetry is measured

`Adc::sampleRateHz()` is `divider x lineRateHz x oversample`, so the 162 MSPS
rating bounds the CONVERSION rate and a ratio multiplies it. Each crossover row
ceiling is half the one above, so every row tops out at the same conversion
rate. At 45013 Hz that allows 3527 kept at 1x, **1763 at 2x** and 881 at 4x.

So at 720p the 1x case is bounded by the eleven-bit line counter at 2047 and the
2x case by the ADC at 1763, and three states are reachable:

| kept | samples/px | oversample | conversion |
|---|---|---|---|
| 1444 | 0.875 | 2x | 130 MSPS |
| 1763 | 1.07 | 2x | 159 MSPS |
| 2047 | 1.24 | 1x | 92 MSPS |

**2047 at 1x resolves detail that 1444 at 2x loses entirely**, so the kept count
beats the anti-aliasing on this source -- which is the asymmetry
`SamplingClock::recommendedDivider()` already states. Pass-through is a second
instance of it: 2038 at 1x, sharper than 1444 at 2x.

**1763 is the configuration to try first.** It keeps the 2x row, needs a ratio
of only 0.82 to fit the raster against 2047's 0.71, and still clears one to one.

## Sampling density is not the only thing the missing downscale costs

The same inability to minify decides FRAMING, and it binds hardest where the
output is small. The capture is the source's and the VDS can only magnify, so
an output mode whose raster is narrower than the capture cannot be made to show
all of it: the surplus is cropped. A framing that fills the screen on one
output mode therefore cannot be held across a change to a lower-resolution one,
and the engine has no mechanism to shrink the capture to suit.

A downscale ahead of the frame buffer answers both at once, because the count
written to memory stops being the count the ADC kept: the divider can serve
sampling density while the written count serves the raster. `../scaling-down-path.md`
carries the third thing it would buy, the width parity.

## The minifier is the mechanism, and it does not engage on an undoubled source

`Tv5725::InputScale` writes the scaling-down DDA, which shrinks the count
written to memory without touching the rate the ADC samples at. That is the one
mechanism that lets the scaling path run the divider pass-through already uses.

Driven through a solve at 720p it does not shorten the line. With the engine
holding divider 2047 and a ratio of 0.706 -- `IF_HSYNC_RST` 1445, capture 1446,
`VDS_HSCALE` 823, stride and fetch all following -- the frame carries the card's
vertical structure in the right places with every line shredded horizontally,
which is a counter of 1445 against a line still 2047 units long. Restoring the
counter to the unscaled divider assembles the frame.

**On a line-doubled source it does engage.** At 320x256@50, `PLLAD_MD` 2200 and
a ratio of 0.75, the roll stops at a counter of **1650**, which is
`PLLAD_MD x ratio` and not `(PLLAD_MD / 2) x ratio`: the line shortens with the
ratio, and the line doubler's halving is lost to it, both fields of
`IF_HS_DEC_FACTOR` being the same register.

So the block is not broken, and what makes its write-enable engage on an
undoubled source is open.

## Refuted

Each tested directly at 720p against a solved state:

- **`IF_HS_PSHIFT_BYPS` is not the enable.** Clearing it leaves the frame
  unchanged, at ratios 0.995 and 0.75.
- **`IF_HS_DEC_FACTOR` alone is not the enable.** Setting it to 1 changes 22% of
  the frame's pixels, so it acts -- but the frame stays sheared, and at unity
  DDA with the counter at `PLLAD_MD / 2` the picture is line noise, so it is not
  a clean halving either.
- **`IF_HS_SEL_LPF` with it is not the enable.** The pair that makes an
  undoubled source resemble the doubled path leaves the frame sheared.
- **`IF_LD_RAM_BYPS` 0 is not available.** Routing an undoubled source through
  the line-doubler RAM gives a solid green frame.

## Reading these states

A still cannot distinguish the two failures and each has been misread as the
other. A **roll** is a frame-to-frame displacement and means the counter does
not divide the real line; a **shear** is a constant slip per row within one
frame, stable frame to frame, and means the same thing while reporting zero
displacement to any frame-to-frame measure. A counter that divides the real
line exactly gives a stable, repeated picture -- doubled rather than rolling --
so duplication without a roll is a counter error of an exact factor.

Mean luma separates none of them: a frame decimated by two has the same mean as
one that is not. Compare the frames, or the spectral power above half-Nyquist.

`../scaling-down-path.md` is the path and its registers. `../known-issues.md`
carries what the divider ceiling costs per mode.
