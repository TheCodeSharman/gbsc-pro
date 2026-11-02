# The minifier buys filtering, not resolution, and it is a wash

The input formatter's scaling-down DDA works, its ratio is the datasheet's, and
installing one is not worth doing. What it buys at equal delivered count is the
difference between a filtered decimation and an aliased direct sample, and
measured on the bench that difference is a few percent.

**So the horizontal route is closed as a resolution mechanism**, and the shape
that stands is pass-through for a source the sink takes straight and the
scaling path for everything below it.
[`the-minifier-engages-on-the-coarse-factor.md`](the-minifier-engages-on-the-coarse-factor.md)
is how the block was made to work and what each of its halves does.

## The arithmetic closes it before any bench reading

`delivered = ratio x counter`, the counter is the raw line and eleven bits, and
what reaches the panel is the delivered count. A ratio is at most 1, so **a
ratio can never raise the delivered count** -- at best it holds it while the
ADC samples harder behind it. At 1280x720@60 into 1080p the raster shows 1443
and unity already delivers 1443; a ratio reaches the same 1443 only at 0.705
with the divider against its wall, and less at every deeper ratio.

The payoff was therefore always one thing, everywhere: **the same delivered
count, filtered down from more ADC samples rather than sampled at it and
aliased**. That is what had to be judged, and it had never been, because until
the block engaged no frame survived a ratio.

**The line-double FIFO's cap is not what decides this.** It bounds the
delivered count at about 1120 whenever the FIFO is in circuit, which minifying
always puts it in -- so it decides which modes could use a ratio, not whether a
ratio is worth using. A deeper buffer would buy the same wash at 1443 units
instead of at 1050.

## The measurement

`vga`, RISC PC at 1280x720@60, scaling path, 16:9, PM5544 with `ANIM OFF`, into
1080p. The divider is pinned with `/sampleclock?hold=` and the ratio installed
through `/inputscale`, so all three states are full solves landing on the same
delivered count -- capture 1050, `VDS_HSCALE` 666, the same window, the same
magnification, `PLLAD_CKOS` 0 throughout. The only difference between them is
how many ADC samples sit behind each delivered unit.

All three stay under the FIFO's cap, so no green tail contaminates any of them:
the greenness of the last ten columns of the lit picture reads -1.1, -1.2 and
-0.4 with a sustained run of 0.

| state | ADC samples per delivered unit |
|---|---|
| unity, divider 1050 | 1.00 |
| ratio 3/4, divider 1400 | 1.33 |
| ratio 0.547, divider 1920 | 1.83 |

Scored as the mean horizontal luma gradient across the card's gratings, in six
bands from plain card on the left to the coarsest bars on the right, plus the
colour on a grey grating and the per-pixel movement across a 12-frame clip:

| state | filter path | flat | finest | | | | coarsest | chroma | movement |
|---|---|---|---|---|---|---|---|---|---|
| unity @1050 | interpolator | 1.45 | 16.84 | 25.35 | 34.43 | 34.16 | 18.64 | 0.76 | 2.58 |
| 3/4 @1400 | interpolator | 1.90 | 23.92 | 28.88 | 33.12 | 33.26 | 18.19 | 4.29 | 3.08 |
| 0.547 @1920 | interpolator | **10.59** | 24.98 | 30.40 | 32.84 | 33.78 | 18.18 | 3.15 | 4.32 |
| 3/4 @1400 | low pass | 1.36 | 5.19 | 16.72 | 28.45 | 31.70 | 18.25 | 1.20 | 1.60 |
| **0.547 @1920** | **low pass** | 1.71 | 17.66 | 26.40 | 34.12 | 33.34 | 18.40 | 1.63 | 2.81 |
| unity @1050 | low pass | 1.29 | 1.65 | 8.37 | 15.88 | 28.98 | 18.21 | 0.60 | 1.39 |

**The fifth row against the first is the answer.** The route's best case --
1.83 times the ADC samples behind an unchanged delivered count, with the filter
the datasheet puts in front of a decimation -- reads 17.66 / 26.40 / 34.12
against unity's 16.84 / 25.35 / 34.43. Within a few percent on every band, for
twice the chroma on a grey grating and more movement. A viewer cannot see that,
and the card's wedge is the most favourable content it could be asked on.

## The filter path was wrong, and it made the block destructive

`applyScan()` wrote `IF_HS_SEL_LPF` from `lineDoubled`, so every undoubled
source took RD-5725-1.1's INTERPOLATOR half of the combined INT/LPF path -- and
an undoubled source is exactly the one that minifies. The third row is what
that costs: 10.59 of horizontal gradient over a column of plain grey card
carrying no content at all, against 1.45 at unity. Seen on the frame it is a
diagonal weave over the whole picture, with magenta fringing on a grey grating
and spurious bars where the card is flat.

The factor and the filter are now written from one named condition, because the
block thins the line two ways and both are a decimation: the doubler spends the
coarse octave and a ratio spends the DDA.

**The undoubled unity leg keeps the interpolator, and that is measured too.**
The last row is the filter applied where nothing is being thinned: it costs
that line its two finest gratings, 16.84 and 25.35 down to 1.65 and 8.37, and
buys nothing.

## Two instrument traps, both of which produced a wrong reading first

**`wedge.locate()` mis-fires on this framing.** It put the box at photo columns
551..656 where the card's gratings start at about 710 -- on the circle's left
edge and the flat block inside it. The box is found from a run of luma
crossings in a fixed row band, and the step out of flat card into grating is
not the only thing in that band at this magnification. Every figure derived
from it here is of the wrong pixels, and the bands above are measured against
the lit picture's own fractions instead. **A wedge reading taken on the scaling
path at an unfamiliar framing wants the box checked against a frame before it
is believed.**

**A held box slides when the picture pans.** A minifying line at ratio 3/4 sits
about 85 output columns left of where unity puts it, where the same line at
0.547 does not -- unexplained, and enough to make a box held in absolute pixels
read different card content. Held that way the 3/4 state scored 24.19 against
unity's 4.14; anchored to the lit picture it scores 1.79. The first of those
reads as a six-fold gain and is a measurement of the circle's edge.

**And wedge contrast cannot tell a resolved grating from an aliased beat.** The
wedge is sustained periodic content at Nyquist and beats rather than softens,
so both read high -- which is how the interpolator's weave scored 23.6 and
looked like detail. The frames settle it and nothing else does.

## What is left of the route

The mechanism stays: it works, it is covered, and `/inputscale` asks for a
ratio. Nothing chooses one, and on this evidence nothing should.

What would re-open it is content that is not a test card -- the wedge is
adversarial by construction, and the question the bands above cannot answer is
whether aliasing on ordinary picture content costs anything a viewer sees. The
whole-picture gradient moved 34.4 to 38.1 at the 99th percentile across the
same states, which is the same few percent.
