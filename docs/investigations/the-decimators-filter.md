# The decimators filter, and the channel line is the divider

Two things about ADC oversampling in pass-through, measured together because the
first hid the second.

`Adc::applyOversample()` takes a faster tap of the one VCO (`PLLAD_CKOS`) and
puts the decimators in behind it (`ADC_CLK_ICLK1X/2X`, `DEC1_BYPS`,
`DEC2_BYPS`). So the ADC samples at `ratio x PLLAD_MD` per line and the
decimators bring it back to `PLLAD_MD`.

## The played-out line is the divider, whatever the ratio

`HdBypass::applyPassThroughSampling()` derived the channel's raster from
`divider / ratio`, on the reading that oversampling costs delivered samples.

**It does not.** Measured, RiscPC on `vga` at 1600x600@60, passed through with
`PLLAD_MD` 2039:

| | `HD_HSYNC_RST` | `HD_HB_ST` | picture |
|---|---|---|---|
| ratio 1 | 2047 | 2039 | full screen |
| ratio 2, raster as derived | 1027 | 1019 | **content in the left portion, the rest black** |
| ratio 2, raster put back by hand | 2047 | 2039 | full screen |

The clipped state survives a `PAD_SYNC_OUT_ENZ` toggle unchanged, with the sink
still naming the same mode, so it is not the encoder holding a stale timing --
`docs/investigations/encoder-stale-timing.md` is not this. Writing the raster
back with nothing else moved restores the picture whole.

So the decimators undo the faster tap exactly: `PLLAD_MD` samples a line reach
the channel at either ratio, and the raster follows the divider.

**This is what made the ratio untestable before.** An earlier attempt reports
both states giving no signal at a channel line of 1024; that is this, the raster
halved under a full line of samples.

## They filter, so more oversampling is better and it is free

With the raster held at 2039 the two ratios differ in nothing but the ADC clock,
which is the comparison the question needs. Photographed interleaved, three
frames a state, two independent passes; the PM5544 frequency wedge split into
its blocks and measured against the colour-bar row of the SAME frame, which
carries no fine detail and so says whether two frames are comparable at all.

The number is the **beat envelope**: an aliased grating rides a slow swell, so
smoothing the fine structure away leaves the alias behind. Repeatable to 2%.

| wedge block | ratio 1 | ratio 2 | |
|---|---|---|---|
| 0 | 10.06 | 5.94 | **-41%** |
| 1 | 0.96 | 0.64 | **-33%** |
| 2 | 19.34 | 16.01 | **-17%** |
| 3 | 3.34 | 3.41 | +2% |
| 4 | 6.96 | 6.81 | -2% |
| bar row (reference) | 0.46 | 0.49 | +6% |

It is visible as well as measurable: at ratio 1 the first block **aliases down to
a lower frequency** -- a dozen wide stripes carrying a beat -- and at ratio 2 it
renders as many more fine, even stripes.

Blocks 3 and 4 do not move because there the PANEL is the limit rather than the
scaler's sampling, which is what makes the first three blocks the measurement.

So `Adc::OversampleAsClockAllows` is what pass-through asks for.
`oversampleFor()` clamps it to `2^postDivider`, which is the ceiling: each
doubling costs a step of `PLLAD_KS` headroom and RD-5725-1.1's crossover table
has no row below /8.

## The trade this leaves open

`HdBypass::dividerFor()` maximises the divider against the channel's counter and
a 162 MHz clock bound, and takes no view of oversampling. But the crossover table
puts `PLLAD_KS` at 0 for any CKO at or above 80 MHz, and at 0 there is no tap to
take -- so a divider chosen for density alone can cost the filtering entirely.

The cliff is at a line rate of `80e6 / 2039` = **39.2 kHz**. The bench source at
1600x600@60 is 37.9 kHz, three percent below it, which is why ratio 2 is
available here at all. Above it the divider stays at the counter's 2039, CKO
passes 80 MHz, and the ratio drops to 1.

A source above the cliff could be sampled less densely to buy the filtering back
-- at 48.4 kHz, 2039 samples undecimated against 1652 samples oversampled by two.
**Which of those looks better is not measured**, and no mode on the bench monitor
definition reaches above the cliff to try it.

## What this does not show

- **Anything about the scaling path.** `SourceStandard` asks for its own ratio
  per source class there, and `PLLAD_MD` is bounded by the capture write limit
  rather than by the channel counter, so the trade above has a different shape.
- **What the decimators do to content below Nyquist.** The measurement is of
  alias beat, which is content above it.
