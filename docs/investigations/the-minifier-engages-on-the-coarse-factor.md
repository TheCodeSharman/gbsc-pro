# The minifier's write enable is `IF_HS_DEC_FACTOR`, and the FIFO consumes it

The input formatter's scaling-down DDA shortens the line. **Two things have to
be true at once, and an undoubled source gets neither:**

1. **`IF_HS_DEC_FACTOR` must be non-zero.** A DDA rate written beside a zero
   coarse factor does nothing whatever. RD-5725-1.1 says so in the field's own
   description -- *if the scaling ratio is less than one half, use it and DDA to
   generate the we and phase* -- and the write enable is what that sentence is
   about.
2. **The line-double FIFO must be in circuit**, `IF_LD_RAM_BYPS` 0.
   `IF_SEL_HSCALE` 1 sends the scaling-down data **and its write enable to the
   line double**, so the FIFO is the only consumer the part offers. It is the
   only RAM in the horizontal path, and thinning a stream needs a buffer:
   writing sparsely and reading back contiguously is what turns a write enable
   into a shorter line. Doubling is a second use of the same RAM, not its
   purpose here.

`InputFormatter::applyScan()` writes `IF_HS_DEC_FACTOR` 0 and `IF_LD_RAM_BYPS` 1
for every undoubled source, so on such a source the block is disabled and its
consumer is out of circuit.

## The measurement

RiscPC on `vga` at 320x256@50, which the engine line-doubles, so the FIFO is
already in circuit. `PLLAD_MD` 2200, the counter held at 1100 throughout, and
the VDS taken to 1:1 (`VDS_HSCALE` 1023) so one captured unit is one output
pixel and a shorter line shows as a narrower picture. Judged with
`../../tools/gbsc-pro-hwtest/picture_match.py` over a scale search reaching
down to 0.10.

| DDA increment | `IF_HS_DEC_FACTOR` | DDA ratio | scale | corr |
|---|---|---|---|---|
| 0 | 1 | 1.000 | 1.000 | 1.000 |
| 1365 | 1 | 0.750 | **0.476** | 0.615 |
| 2047 | 1 | 0.667 | **0.408** | 0.606 |
| 4095 | 1 | 0.500 | **0.332** | 0.569 |
| 4095 | **0** | 0.500 | **1.000** | 0.995 |
| 0 | 2 | 1.000 | 1.000 | 0.665 |
| 4095 | 2 | 0.500 | 0.712 | 0.628 |
| 0 | 1 | 1.000 | 1.000 | 1.000 |

Unity taken at both ends returns 1.000 exactly. **The fifth row is the finding**:
the same DDA increment that compresses the picture to a third of its width with
the coarse factor at 1 does nothing at all with it at 0.

At ratio 0.50 the whole card -- circle, colour bars, grating, greyscale and both
captions -- is legible inside a narrow column with the rest of the line empty.
**The correlation is low because the frame is a compressed picture rather than a
scaled copy of the reference**, so the reported `scale` names the compression
only approximately; what it establishes is the direction and that it tracks the
ratio, not an exact factor.

## Why every earlier reading said the block did nothing

All of them were taken with `IF_HS_DEC_FACTOR` at 0, with the FIFO bypassed, or
both:

- The ratio sweeps driven through `/inputscale` on an undoubled source run with
  the coarse factor at 0 throughout, because `applyScan()` writes it from
  `lineDoubled`.
- The clean "does the picture narrow" test was run only with the FIFO bypassed.
- The green frame an undoubled source gives with the FIFO in circuit was taken
  with the coarse factor at 0, so the FIFO was in circuit receiving no write
  enable at all. It is not evidence about the FIFO's read side.

## What the engine does wrong

Three things, and they compound:

- `IF_HS_DEC_FACTOR` is written from `lineDoubled` rather than from the ratio
  being asked for. It has two owners and this is what the collision costs.
- `IF_LD_RAM_BYPS` is written from `lineDoubled` too, so the one consumer of the
  write enable is bypassed exactly when a ratio is wanted.
- `InputFormatter::lineCounterFor()` shortens the counter by the ratio. Whether
  that is right is **unestablished**: with the block disabled it was simply
  wrong, and now that the block engages the counter has not been swept against a
  working compression.

## The FIFO does not need the line-doubled routing

Searched where the block works rather than where it does not. From the
compressing state on the 312-line source -- FIFO in, coarse factor 1, DDA 4095,
VDS at 1:1 -- each register was flipped to the value the undoubled branch would
write:

| flipped to the undoubled value | corr | green |
|---|---|---|
| none, the working state | 1.000 | no |
| `IF_LD_SEL_PROV` 1 | 0.927 | no |
| `IF_PRGRSV_CNTRL` 1 | 1.000 | no |
| `IF_SEL_WEN` 1 | 1.000 | no |
| `IF_HS_SEL_LPF` 0 | 0.995 | no |
| `IF_HBIN_SP` 2, `IF_HBIN_ST` 0 | 0.921 | no |
| `IF_HSYNC_RST` 2200, the full line | 0.963 | no |
| **all six at once** | **0.862** | **no** |

**Not one of them breaks it**, and with all six flipped the card is still
compressed into a column with the rest of the line empty. So the FIFO wants
none of the line-doubled routing, and the green an undoubled source gives is
not the routing.

## The green tail is the line-double FIFO, and it caps what the minifier can deliver

The band at the right of a minifying undoubled frame starts at a CONSTANT count
of delivered IF units, whatever the divider and whatever the ratio. Measured on
`vga` at 1280x720@60 into 1080p at ratio 3/4, the divider walked with
`/sampleclock?hold=`:

| divider | delivered units | green starts, in IF units |
|---|---|---|
| 1444 | 1083 | **none** |
| 1600 | 1200 | 1161 |
| 1750 | 1313 | ~1181 |
| 1924 | 1443 | 1134 |
| 2046 | 1535 | 1125 |

**That is the line-double FIFO's own length**, and it is the same buffer
`SamplingClock::DoubledLineSampleLimit` holds a doubled line off:
[`the-tail-green-is-the-vds-line-filter.md`](the-tail-green-is-the-vds-line-filter.md)
measures its onset at **1118..1128 IF units on a doubled line**, and the cap's
2200 ADC samples is 1100 units, just under. A doubled line therefore never
reaches it and a minifying undoubled one has no equivalent bound, so it walks
straight past and the tail of every line is unwritten.

**So the FIFO caps the DELIVERED count at about 1120 whenever it is in
circuit** -- which minifying always puts it in. The consequence is sharp: a
raster needing more delivered units than that cannot be served by the minifier
at all, and 1080p needs 1443. **The minifier is out of reach for 1080p for this
reason as well**, and what it can serve is the small rasters.

The fix the measurement points at is one constant in IF units applied whenever
the FIFO is in circuit, through the ceilings `VideoPath` already states in
delivered units -- which subsumes `DoubledLineSampleLimit`, a divider constant
covering only the doubled half of the same bound.

## Still open: the FIFO cannot be brought up by writing registers

At 1280x720@60 and at 640x480@60 -- both left undoubled by the engine -- forcing
`IF_LD_RAM_BYPS` 0 gives a solid green frame, zeros reaching the frame buffer.
Nothing moves it:

- the counter at `PLLAD_MD` or at `PLLAD_MD / 2`
- the coarse factor at 0, 1 or 2, and any DDA increment
- the whole line-doubled routing written beside it
- the divider held at 1024, 1444 or 2047
- `SFTRST_FIFO_RSTZ`, `SFTRST_IF_RSTZ`, `SFTRST_DEC_RSTZ`, `SFTRST_MEM_RSTZ`
  and `SFTRST_MODE_RSTZ` each pulsed afterwards

**What separates the working case from every failing one is who configured the
FIFO.** It works on the source the engine SOLVED with the FIFO in circuit, and
keeps working through six register flips afterwards; it is green on every source
where the solve bypassed it and the register was written by hand later. That is
the standing rule about hand-set subsets of an interdependent solve, so the next
move is a change to `applyScan()` rather than another poke.

## There is no latch and no reset: the IF-unit geometry has to move with it

`VideoPath::applyScan()` is the whole code path, and it writes three things:
`InputFormatter::applyScan()`, then `VideoProcessor::applyLineDoubling()` and
`Deinterlacer::applyLineDoubling()` -- and those last two write only delay
alignments, `VDS_V_DELAY`, `VDS_Y_DELAY` and the MADPT luma delay. **Nothing is
latched and nothing is reset**, which is consistent with five soft resets
changing nothing.

A full 1536-register diff between two healthy solved states -- 320x256@50 line
doubled with the FIFO in, and 640x480@60 undoubled with it out -- resolves to
**39 fields**. Outside the IF's own scan fields and the geometry that must
differ between two sources, only `DEC1_BYPS` (which the oversampling row owns)
and `MADPT_Y_DELAY` move. So the solve holds no secret the register writes
could not reproduce.

**What the register writes missed is the IF-UNIT GEOMETRY.** The coarse factor
halves the line, and the capture window, the line limits and the playback fetch
are all counted in IF units, so every one of them has to halve with it. Measured
at 640x480@60 with the FIFO in and the coarse factor at 1:

| what was moved | mean | green excess | result |
|---|---|---|---|
| nothing but the counter | 63.6 | +141.1 | solid green |
| `IF_HSYNC_RST`, `IF_HB_SP2`/`ST2`, `IF_LINE_SP`/`ST`, `PB_CAP_OFFSET`, `PB_FETCH_NUM` all halved | 54.5 | **+37.9** | picture, repeated across the line |

So the green was the window spanning a line that no longer exists, and the
remaining fault is a counter that does not match what the block emits. **That is
why this cannot be finished by writing registers**: the ratio, the counter, both
windows, the stride and the fetch are one solve, which is the standing rule
about hand-set subsets. The change belongs in `applyScan()` and the geometry it
feeds.

## What the compression buys, and the counter that caps it

**SUPERSEDED IN ITS FIGURES.** This section read 1.75 and 2.18 samples per
source pixel from a divider of 2890 and 3599, on the premise that the counter
follows the ratio so the compression frees room in it. The counter sets the
output line rate and does not -- see the last section -- so the divider is
capped at **2047** undoubled at every ratio, and the three bounds are:

| bound at 1280x720@60, line rate 45013, into 1080p | divider |
|---|---|
| ADC rating at 1x, 98% | 3527 |
| line counter, 11 bits, RAW | **2047** |
| raster, 1443 delivered units / ratio | 1443 / r |

The counter and the raster cross at `r = 1443 / 2047 = 0.705`, which is the best
the mode can do: **divider 2047, 1.24 ADC samples per source pixel against the
0.875 the solve installs at unity.** A 42% gain rather than a doubling, and
still worth having -- pass-through runs 2038 on this source and resolves bundles
the scaling path loses.

**The DELIVERED count is 1443 either way**, so the gain is in how those 1443
units are arrived at: decimated from 2047 samples rather than sampled at 1443 and
aliased. Resolution at the panel is bounded by the raster, and no ratio lifts
that -- `delivered = ratio x counter`, so the compression can only ever reduce
it. What would lift it is a wider raster, which is the display clock.

## Reading these states

A still cannot tell the failures apart and each has been misread as another.

- A **shear** is a constant slip per row, stable frame to frame, so every
  frame-to-frame measure reports zero on it.
- A counter that divides the real line exactly gives a stable **duplicate**, not
  a roll. It holds perfectly still and its repeated content correlates with the
  card, so neighbouring-row correlation and displacement both call it clean. A
  counter of 1650 at `PLLAD_MD` 2200 and ratio 0.75 was recorded as the roll
  stopping and is one of these.
- Mean luma separates none of them.

`picture_match.py` is what does separate them, by asking whether the frame is
still the reference and at what scale, and **its scale search has to reach below
the compression being looked for** -- a floor of 0.55 clamped the readings above
and made a working compression look like none.

**And a routing register toggled on an already-broken frame proves nothing**:
every reading of `IF_SEL_WEN` and `IF_LD_RAM_BYPS` taken at a shortened counter
was uninformative for that reason. Make the change from a clean solved state
with the counter left alone.

`../scaling-down-path.md` is the path and its registers.

## What each half of the block does, with the divider pinned

Driven through the solve on `vga` at 1280x720@60 into 1080p, scaling path,
PM5544 with `ANIM OFF`, judged off the USB capture.

**The engine installs a ratio exactly as designed.** `/inputscale?wanted=1&have=2`
with the divider free takes `PLLAD_MD` 1444 to **2888** while `IF_HSYNC_RST`
stays at 1444, `STATUS_SYNC_PROC_HTOTAL` following to 2889 so the PLL latched,
and the capture window, the stride and the fetch do not move -- the same ~1445
units in front of the VDS at 1.75 ADC samples per source pixel against 0.875.
The console says `installing: divider from line rate 45013, doubled 0 -> 2888`.

**PIN THE DIVIDER WITH `/sampleclock?hold=`, OR THE RATIO AND THE SAMPLING MOVE
TOGETHER AND NOTHING IS ATTRIBUTABLE.** Held at 1444 so only the ratio changes,
the four states separate cleanly:

| state | `IF_HSYNC_RST` | frame |
|---|---|---|
| unity | 1444 | clean, `corr 1.000 scale 1.000` |
| ratio 3/4, FIFO in, factor 1 | 1083 | **no shear**, card duplicated vertically |
| ratio 3/4, FIFO out, factor 0, `IF_HS_PSHIFT_BYPS` 0 | 1083 | pure shear |
| unity, FIFO forced in | 1444 | **solid green** |

**THE MINIFIER WORKS AND ITS RATIO IS EXACTLY THE DDA'S.** The second row is the
finding: no shear means the counter the engine wrote matches the line the block
emitted, and `1444 x 0.75 = 1083` is what it wrote.
`InputFormatter::lineCounterFor()` is right as it stands -- no halving from the
FIFO, and no second halving from the coarse factor.

**The block needs the FIFO, and `IF_HS_PSHIFT_BYPS` is not the gate.** The third
row puts the phase-adjustment data path in circuit with the coarse factor at 0,
which is what RD-5725-1.1 describes for a ratio above a half, and the frame is a
constant slip per row against the same counter -- so the line was still 1444
units and no scaling happened. The datasheet's two bands do not divide the work
the way the field descriptions suggest.

**The solid green is the FIFO with no write enable.** The fourth row holds the
counter at a value known correct and leaves the block inert, and nothing reaches
the frame buffer at all. `IF_SEL_HSCALE` sends the scaling-down block's data AND
its write enable to the line double, so with a null rate the FIFO is handed no
write enable and writes nothing. Every earlier green frame was that, rather than
a window spanning a line that no longer existed.

## The counter sets the OUTPUT LINE RATE, so the ratio must not move it

With the counter correct for the compression and the line emitted correctly,
the card still appears as **two stacked copies**. That is not a read-side
mystery: it is the line doubler's own mechanism.

**Output lines per source line is `PLLAD_MD / IF_HSYNC_RST`.** The counter wraps
once per OUTPUT line, so a counter at half the divider emits two lines per
source line -- which is exactly how a doubled source is doubled, and nothing
else in the block does it. A counter shortened by a ratio multiplies the lines
by `1 / ratio` in the same way.

| state | `PLLAD_MD` | `IF_HSYNC_RST` | predicted | measured |
|---|---|---|---|---|
| ratio 1/2, divider free | 2888 | 1444 | 2.000 | **2.005** |
| ratio 3/4, divider held | 1444 | 1083 | 1.333 | duplicated |
| the same, counter forced to 1444 | 1444 | 1444 | 1.000 | **one card, duplication gone** |

The third row is the test: one register, from a state wrong in exactly one
respect, and the vertical comes right at once -- a single card at correct
proportions with the caption back at the foot of the frame.

**So `InputFormatter::lineCounterFor()` must not apply the ratio.** The counter
is the raw line, halved only by the doubler, because it is what sets the output
line rate; the ratio belongs to the capture window and to everything else
counted in IF units. `../scaling-down-path.md` said this before any of the
above was measured and it was right: the unpinned run appeared to refute it
only because the divider had moved too.

**The split is in the firmware.** `lineCounterFor()` takes no ratio and is the
raw line; `InputFormatter::usableUnits()` is the counter's span compressed by
the ratio, which is what the FIFO delivers and what `capturableLine()` and every
count placed in IF units is measured in. `recommendedDivider()` takes no ratio
either, the counter bounding the raw line, and the compression reaches the
divider only through the two ceilings `VideoPath` states in delivered units.

**What is NOT yet known is where the window then belongs.** The reading that
raised it -- the counter forced to the raw divider by hand while the window held
values the engine had computed for the scaled one, the card in the left 44% of
the output -- is a state wrong in two respects and says nothing about either.
Two candidates remain and a frame separates them:

- **The FIFO delivers the compressed line at the START of each counter-line**,
  so the written region is `0..ratio x counter` and the rest of the line is
  unwritten. The window belongs inside it, which is what the solve now places,
  and the picture fills.
- **The FIFO stretches the compressed line across the whole counter-line**, in
  which case the minifier buys nothing and the route is dead.

Two smaller things come with it. `InputFormatter::LineDoubleReset` 160 pans the
picture hard at this divider -- the green at the left of every minified frame --
and it is measured on doubled modes only. And `IF_LINE_ST`/`IF_LINE_SP` is NOT
the fault: `CaptureWindow::progressiveWindow()` spans exactly one line from
`ProgressiveStart` and is meant to roll past the wrap, which it does on every
working doubled source too.

**The control is clean at every step.** Unity either side of each run restores
the frame exactly, `corr 1.000 scale 1.000`, with Nyquist detail 10.99 before
and 10.86 after the whole sequence.

## The counter stays raw, and the frame says so

Measured on `vga` at 1280x720@60 into 1080p, scaling path, PM5544 with
`ANIM OFF`, divider pinned at 1444 with `/sampleclock?hold=` so only the ratio
moves, judged off the USB capture against a unity reference taken first.

With the counter raw and the window solved in delivered units -- `PLLAD_MD`
1444, `IF_HSYNC_RST` **1444**, `STATUS_SYNC_PROC_HTOTAL` 1444, delivered 1083,
window 126..1063 -- the frame carries **one complete card, undistorted**:
`match down corr 0.960 scale 1.000`. No duplication, no shear, nothing missing.

**So the first of the two candidates holds.** The FIFO delivers the compressed
line at the START of each counter-line and leaves the rest unwritten: the
content sits at the left, the remainder of the frame is black, and nothing is
stretched across the counter-line. The route is not dead.

## The ratio is the octave times the DDA, and the firmware spent an octave twice

**RETRACTED, AND THIS SECTION REPLACES IT:** an earlier reading here said the
coarse factor is the whole ratio and the DDA increment does nothing. It came
from a sweep driven through `/inputscale`, where the engine re-solves the window
and the scale for each ratio it is asked for -- so `window x magnification`
stayed at 1399 and the measured scale read 0.500 at every increment. That
constant is not the block's ratio. It is the engine compensating correctly for a
ratio the block was delivering at half.

**THE MODEL IS `ratio = 2^-IF_HS_DEC_FACTOR x 4095 / (4095 + increment)`**,
exactly as RD-5725-1.1 describes. Measured with the divider pinned at 1444 and
the window HELD at one solve, so the compensation is identical at every point
and the measured scale moves only with the block:

| factor | increment | model | predicted scale | measured | error |
|---|---|---|---|---|---|
| 0 | 1365 | 0.7500 | 1.001 | **1.000** | -0.001 |
| 0 | 2048 | 0.6666 | 0.890 | **0.888** | -0.002 |
| 0 | 4095 | 0.5000 | 0.668 | **0.668** | +0.000 |
| 1 | 0 | 0.5000 | 0.668 | **0.668** | +0.000 |
| 1 | 1365 | 0.3750 | 0.501 | **0.500** | -0.001 |
| 1 | 4095 | 0.2500 | 0.334 | **0.320** | -0.014 |
| 2 | 0 | 0.2500 | 0.334 | **0.320** | -0.014 |

The last two rows are the cross-check the model requires: one octave with the
DDA at its limit and two octaves with the DDA idle are the same ratio, and they
land on the same picture. The 4% deficit at the deepest ratios is unexplained
and is the same figure for both.

**LINE DOUBLING ALREADY SPENDS THE OCTAVE.** A doubled line is two ADC samples
per IF unit, which is that factor at 1 -- and forcing it to 0 on 320x256@50
breaks the picture outright, near-Nyquist detail 10.96 -> 5.29 with the card
magnified about four times and cropped, because the IF unit becomes one sample
while the counter stays at half the line.

So the factor belongs to the SCAN. `applyScan()` wrote it from `fifoInCircuit`,
which spent a second octave on a minifying undoubled line: the engine sized
every window for the ratio asked for while the block delivered half of it, and
the card landed in the left 44% of the output. That is the whole of the fault,
and the fix is one expression.

## What it buys, confirmed end to end

`/inputscale?wanted=3&have=4` with the divider free, on the fixed firmware:

| | unity | ratio 3/4 |
|---|---|---|
| `PLLAD_MD`, `IF_HSYNC_RST` | 1444 | **1924** |
| `STATUS_SYNC_PROC_HTOTAL` | 1444 | 1924 |
| delivered units | 1445 | 1443 |
| `VDS_HSCALE` | 916 | 914 |
| near-Nyquist detail | 6.75 | **8.30** |

33% more ADC samples behind the same delivered count, and the detail reaches
the panel. The counter follows raw and the PLL latches at the new divider.

## Still open

- **Why a minifying undoubled line is also panned** some 70 columns left of
  where unity puts it. `IF_HBIN_SP` is not the cause: 96, 48, 16 and 2 give the
  same frame, which retires the guess that `InputFormatter::LineDoubleReset`,
  measured on doubled modes alone, pans a minifying one.
- **Why the two deepest ratios come in 4% short** of the model, identically.
- **Whether a second octave is worth reaching.** `InputScale` tops out at 1/2
  inside one, so factor 2 is unused; the counter is the wall either way.
