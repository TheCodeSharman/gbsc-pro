# The capture origin has a term at each end of the cable

`SyncProcessor::RetimeOriginSamples` is 63 ADC samples, added to the retime stop
the input formatter's line counter takes its origin from. Measured against the
mode file at full framing, the source's active region lands anywhere from 2.5
samples early to 10 late, and which it is varies by mode.

**Two terms of that are found and corrected, and both were the scaler's.**
Oversampling ratio one puts the origin 16 samples earlier than ratio two, which
is what the two 108 MHz modes were reading 17 samples off for
(`UndecimatedOriginSamples`); and the inverted sync path measures the pulse 5
samples wide, which `retimeStopFor()` subtracts straight into the origin on
every high-active source (`InvertedPulseWidthSamples`).

**What is left is one line in sampling density, fitting seven modes to a
residual rms of 0.48 samples**, with a slope of about four SOURCE PIXELS. That
is the term at the source's end, and the scaler cannot see the pixel clock it
would need to measure it.

**Two controls settle that the remainder is not one quantity.** Neither end of
the cable can produce what the other's control measures, so both contribute and
no single correction absorbs either:

- **The scaler has a term**, because holding one source at one mode and moving
  only the divider moves the offset by 6.9 samples. The source cannot see the
  divider.
- **The source has a term**, because two modes the chip cannot tell apart land
  2.2 samples apart. The scaler cannot see the pixel clock that separates them.

Every reading here is anchor-free: `counter_origin.py`, which creeps the capture
window's own start until the feature leaves the counter. `full_margins.py` reads
about two samples higher and that has to come off first.

## The divider moves it, with the source untouched

One mode, one source, the engine solving the whole geometry around a held
divider, with its own unheld choice beside them:

| `PLLAD_MD` | units/px | sample rate | d samples | d source px |
|---|---|---|---|---|
| 800 | 2.00 | 25.17 MHz | +3.2 | +1.6 |
| 1400 | 3.50 | 44.06 MHz | +9.4 | +2.7 |
| 1440, unheld | 3.60 | 45.31 MHz | +10.1 | +2.8 |

Neither unit is constant, so the offset is neither a fixed count of samples nor
a fixed time. The held points and the unheld one lie on one trend, which is what
says a hold is a sound way to take this.

## The pixel clock moves it, with every observable timing identical

`800x600@60` and `1600x600@60` are the monitor definition's only pixel-clock
twins: sync 3.2000 us, line 26.400 us, active starting 5.4000 us in, same
polarity and field rate, and the engine solves the same `PLLAD_MD` 1438 and the
same retime stop 1325. Nothing the TV5725 can observe differs. Three interleaved
acquisitions each:

| | units/px | d samples | d source px |
|---|---|---|---|
| 800x600@60, 40 MHz | 1.363 | +5.1, +5.1, +5.1 | +3.7, +3.8, +3.7 |
| 1600x600@60, 80 MHz | 0.681 | +2.9, +2.9, +2.9 | +4.2, +4.2, +4.2 |

They repeat to better than 0.1 source pixels and land 2.2 samples apart. **Both
sample at one rate**, so *constant in ADC samples* and *constant in time* are one
hypothesis across this pair, and it is refuted.

**RE-TAKEN AFTER BOTH CORRECTIONS AND AFTER THE NEAR-EDGE GUARD**, so the
instrument defect that made a run on the emitted frame's first column read as
the card's near edge cannot be what produced them:

| | units/px | lead against the file | d samples |
|---|---|---|---|
| 800x600@60, 40 MHz | 1.363 | 215.6 against 216 | **-0.5**, -0.5 |
| 1600x600@60, 80 MHz | 0.681 | 427.7 against 432 | **-3.0** |

Still 2.5 samples apart, with the engine solving `SP_RT_HS_SP` 1330 for both.

**NO MECHANISM IS MEASURED.** A delay common to a source's sync and its video
cancels, because the scaler times video from the sync edge, so what the numbers
require is a *differential* delay between the video path and the sync generator
beside it. Nothing here has measured one, and a mechanism proposed to fit these
numbers is a story until a probe carries it.

Two candidates are refuted rather than untried. **A mis-stated reference is
not available**: the mode file's blanking is VESA DMT exactly -- 640x480
`96+48=144`, 800x600 `128+88=216`, 1024x768 `136+160=296` -- and the card's
green frame is on framebuffer column 0, `PROCframe` drawing `PROCpix(0,0,1,H%)`.
**Nor is a smeared marker**: a band-limited feature is detected LATE and its
smear is a fixed time, so it would cost more source pixels at 80 MHz than at 40
and push the 80 MHz reading positive. It reads 3.9 source pixels more negative.

## The scope settles it, and the predictions are registered

The twin pair is the only place on the bench where sync timings are identical
and the pixel clock is not, so a probe on HSYNC and GREEN measures the one thing
no measurement through the scaler can separate. Both modes are H+, so the raster
is counted from the sync pulse's RISING edge:

| | 800x600@60, 40 MHz | 1600x600@60, 80 MHz | difference |
|---|---|---|---|
| sync width | 3.2000 us | 3.2000 us | 0 |
| line | 26.4000 us | 26.4000 us | 0 |
| video where the file says | 5.4000 us | 5.4000 us | **0 ns** |
| video lagging by 4.10 source px | 5.5025 us | 5.4513 us | **51.3 ns** |

**Read the DIFFERENCE, not the absolute number.** Probe skew, cable length and
trigger offset are identical across the pair and cancel; an absolute reading
carries all three.

**TAKE IT ON `PM5544`, NOT ON `CARD`.** `CARD`'s green frame is ONE source
pixel -- 25 ns at 40 MHz and 12.5 ns at 80 MHz -- and a pulse that narrow need
not reach full amplitude through the DAC and the cable, which makes a 50%
threshold ambiguous in exactly the direction that would fake the effect.
`PM5544` marks the same edge with a feature that does reach it, and needs no
change to the source:

- its left castellation band starts at framebuffer column 0, `PROCcastell`
  drawing `PROCpix(0, FNgy(j%)+1, e%, ...)` over a white ground filled from
  `0,0`, so it is the same edge the frame marks;
- it is `e% = CW% DIV 3` wide, and `CW%` computes to **47 source pixels in both
  twins** -- 375 ns and 187 ns, both full amplitude;
- `NY%` is fixed at 13 and `FNgy()` depends only on `H%`, which is 600 in both,
  so the white/black alternation down the left edge is IDENTICAL in the pair and
  drops out of the difference the same way probe skew does.

Its animation flips the corner squares yellow against white, both full scale on
the green channel and so invisible there; `ANIM OFF` regardless.

**THE BORDER IS THE TRAP, AND IT IS ASYMMETRIC.** `1600x600@60` carries 78
source pixels of left border where `800x600@60` carries none, and a border drawn
in a colour fires the measurement 975 ns early -- twenty times the effect, on one
mode only. `PROCpatinit` forces it black with the flash off. The tell is that
1600x600 must read about 5.40 us and not about 4.43.

**They are not one entry doubled.** Sync 128 -> 256 and the line total
1056 -> 2112 both double, while the back porch goes 88 -> 98 and a 78-pixel
border appears either side. What the comparison needs is doubled: the sync
width, the line total and the distance to the first framebuffer pixel. The
**75 Hz pair is not a twin for this question** -- its active regions start
4848.5 and 3393.9 ns in -- so it compares quantities belonging to the sync path
and not the placement of video.

## A line in sampling density fitted one family before the corrections

Seven H-negative readings at 25 to 45 MHz, spanning three modes, three dividers
and a four-fold range of density, fit

    d = 3.97 x (units per source pixel) - 4.33 ADC samples

to a residual rms of 0.56 samples and a worst case of 0.86 over a 10-sample
range, with six H-positive readings a constant 4.2 above it. **Five modes
outside that set refute it.** Fitted over all thirteen states, the best model in
density, sample rate and polarity leaves a residual rms of 4.4 samples, and the
residuals are structured rather than scattered: both 1024x768 modes sit 8.5
low and both 108 MHz modes 5 to 7 high.

**THE 1024x768 HALF OF THAT STRUCTURE DOES NOT SURVIVE THE OVERSAMPLING FIX, AND
NOTHING NEEDS MEASURING ON THOSE MODES.** The 8.5 was fitted while the two
108 MHz modes still carried their 16-sample step, which drags the line they were
fitted with. Refitting the same eleven anchor-free states with the step
corrected:

| model | rms | 1024x768 residuals |
|---|---|---|
| density alone | 1.99 | -2.84, -2.68 |
| density and polarity | **1.28** | **-0.97, -0.95** |

-- inside the two samples one mode's reading moves between sessions. A divider
hold on `1024x768@60` cannot say otherwise either: the output raster caps its
density at 0.71..1.07, which the fitted slope turns into 1.6 samples of travel.

**So those coefficients describe one family of modes and are not a correction.**
What survived the wider set was the pair of controls, not the line through them
-- until the two scaler terms came off. With the oversampling step and the
polarity step both corrected, a line in density fits seven modes spanning both
polarities to a residual rms of 0.48 samples, which is the section below.

## The polarity step is the measured PULSE, and it is corrected

`retimeStopFor()` writes `PLLAD_MD - pulse + origin`, so whatever the pulse
reads wrong places the source by the same amount. The pulse does not read the
same on both polarities.

Seven modes in ONE session, every one at oversampling ratio two, the pulse the
firmware derives against what the mode file states, read in the same pass as the
origin beside it:

| mode | u/px | sample rate | polarity | pulse - filed | d samples |
|---|---|---|---|---|---|
| 1280x768@60 | 0.87 | 69.17 MHz | negative | -4.4 | -1.4 |
| 1280x800@60 | 0.87 | 72.64 MHz | negative | -4.8 | -2.6 |
| 1024x768@60 | 1.07 | 69.55 MHz | negative | -2.7 | -1.9 |
| 640x480@60 | 1.81 | 45.57 MHz | negative | -3.3 | +3.3 |
| 1360x768@60 | 0.81 | 69.26 MHz | **positive** | **+1.8** | +2.3 |
| 1600x600@60 | 0.68 | 54.40 MHz | **positive** | **+1.7** | +2.9 |
| 800x600@60 | 1.36 | 54.40 MHz | **positive** | **+1.7** | +5.1 |

Mean **-3.80** against **+1.73**, a split of **5.53 ADC samples**. The origins
taken beside them step **5.27** across the same boundary -- a line fitted on this
session's H-negative modes alone puts the three H-positive ones +4.95, +6.30 and
+4.56 above it.

**The two are the same quantity and the regression says so.** Fitted against
sampling density, `d` carries the pulse error with a coefficient of **0.85**
where `stop = MD - pulse + origin` wants exactly 1, at a residual rms of 0.90.
Nothing else in the set moves with it.

**The polarity is read from the chip, not from the mode file.** The definition's
`sync_pol` is two bits and only values 0 and 3 had ever been measured, which
leaves the horizontal and vertical polarities moving together. `1280x768@60` and
`1280x800@60` are `sync_pol` 1 -- H-negative with V positive -- and both read
`STATUS_SYNC_PROC_HSPOL` 0 and sit on the H-negative side. The step follows the
horizontal polarity alone.

**It is the INVERTED PATH that reads wide, not the complement arithmetic.** On
every high-active state here `SP_HS_INV_REG` was 1 and the register held the
pulse rather than its complement, so `hsyncPulseSamples()` never took a
complement at all. What differs is that the count was taken through
`normaliseHsyncPolarity()`'s inversion.

**A constant count rather than a constant time**, so far as one divider can say:
the three H-positive states sit at `PLLAD_MD` 1438 and 1444 while their sample
rates differ by 27%, and the error reads +1.7, +1.7 and +1.8 where a fixed time
would have run +1.7 to +2.2. The H-negative side cannot arbitrate -- it spreads
22% as a count and 21% as a time, which is the register's own wobble.

`SyncProcessor::InvertedPulseWidthSamples` is **5**, taken off a high-active
source's pulse where the reading is made. Two measurements that share no model
choose it: the split itself, and the value minimising the seven origins'
residual against density -- 0.64 samples at 5, against 2.53 uncorrected and 0.76
at 6. `RetimeOriginSamples` was calibrated on H-negative states, so the direct
reading is the one it suits and the inverted one is what moves.

**Measured on the unit afterwards, the same seven modes in one session:**

| mode | polarity | before | after | moved |
|---|---|---|---|---|
| 1280x768@60 | negative | -1.4 | -1.4 | 0.0 |
| 1280x800@60 | negative | -2.6 | -2.6 | 0.0 |
| 1024x768@60 | negative | -1.9 | -1.9 | 0.0 |
| 640x480@60 | negative | +3.3 | +1.7 | -1.6 |
| 1360x768@60 | **positive** | +2.3 | **-3.2** | -5.5 |
| 1600x600@60 | **positive** | +2.9 | **-2.9** | -5.8 |
| 800x600@60 | **positive** | +5.1 | **-0.6** | -5.7 |

`SP_RT_HS_SP` moved by exactly +5 on each high-active mode -- 1415 to 1420 at
`1360x768@60` -- and the realised displacement is 5.5 to 5.8, the same overshoot
the oversampling step showed. The one H-negative mode that moved is the
session's own spread.

**The polarity term is retired.** The offset of the H-positive modes from a line
fitted on the session's H-negative ones falls from **+5.27 to -0.36**, and a
model in sampling density alone now fits all seven to a residual rms of **0.48
samples** where it left 2.53 before. Adding a polarity term to that buys 0.03
and gives it a coefficient of -0.37.

**What is left is the slope.** `d = 4.10 x (units per source pixel) - 5.93` over
the seven, which is about four SOURCE PIXELS -- the term the scaler cannot
measure, because it cannot see the source's pixel clock. The intercept is
already centred for the density range the bench reaches, so moving
`RetimeOriginSamples` trades one end of it for the other.

## The two 108 MHz modes were the only two at oversampling ratio one

`Adc::postDividerFor()` crosses at CKO 80 MHz and `applyOversample()` reduces the
ratio to what the post divider has room for, so a source whose divider and line
rate put CKO above 80 MHz runs `PLLAD_KS` 0 and oversampling ratio **one**, with
both decimators bypassed. Of the thirteen states above those two are the only
ones; the rest run two, and one held point runs four.

**Holding the divider across that crossover moves the offset 16 samples with the
source untouched** -- one pixel clock, one sync width, one line rate, one
polarity:

| mode | `PLLAD_MD` | CKO | `KS` | ratio | d samples |
|---|---|---|---|---|---|
| 1280x960@60 | 1444 | 86.64 MHz | 0 | 1 | +17.0 |
| 1280x960@60 | 1380 | 82.80 MHz | 0 | 1 | +16.0 |
| 1280x960@60 | 1320 | 79.20 MHz | 1 | 2 | +0.8 |
| 1280x960@60 | 1200 | 72.00 MHz | 1 | 2 | +1.1 |
| 1280x1024@60 | 1260 | 80.62 MHz | 0 | 1 | +16.5 |
| 1280x1024@60 | 1240 | 79.34 MHz | 1 | 2 | +0.7 |

The last pair is a 1.3% change of divider either side of the boundary. **So the
pixel clock, the sync width and the line rate are all refuted**: none of them
moved, and a term belonging to any of them cannot do this.

**It is the RATIO and not the PLL's own crossover row.** The two move together on
every state the engine reaches, so they are separated by forcing ratio one at row
one. `/sc?o` does it: `PLLAD_CKOS` and the decimators move together, so the kept
rate -- and the whole solved geometry -- stays where it was, which is what makes
the pair comparable. At `PLLAD_MD` 1240 and CKO 79.34 MHz, one divider, one VCO,
one capture window:

| ratio | d samples |
|---|---|
| 2 | +0.7 |
| **1** | **+16.8** |

**And it is constant in samples, not in time.** Seven ratio-one states over two
modes, seven dividers and CKO 79.3..92.4 MHz mean **16.83** with an sd of 0.50,
where a fixed 195 ns would have fallen 2.3 samples across that range. Four
ratio-two states mean **0.90**, sd 0.16. The step is **15.9 ADC samples**.

**Ratio four is not a further step of the same kind.** A latency of N ADC
conversion clocks would cost N kept samples at ratio one, N/2 at two and N/4 at
four, so a 16-sample step from one to two implies 8 more from two to four.
`320x480@60` held at `PLLAD_MD` 800 runs ratio four and sits 0.4 samples off the
ratio-two family's own line, not 8. `SyncProcessor::UndecimatedOriginSamples`
therefore states a step at ratio one alone rather than a `32/ratio` law.

Two explanations were closed before the divider hold, and neither was needed:

- **Not the machine's video bandwidth.** `1280x960@60` at 256 colours asks
  73.7 MB/s of video DMA, four times what it asks at 16. The two read **+17.1 and
  +16.9**, with the same solved divider, raster and density.
- **Not a raster the source cannot deliver.** The measured line rate matches the
  file to 0.00% on both -- 60.000 kHz against 60.000, and 63.981 against
  63.981 -- with the vertical totals right as well.

**Fixed, and measured on the unit.** `SyncProcessor::UndecimatedOriginSamples`
is 16, added to the origin at ratio one, with the ratio-two path left alone:

| mode | ratio | before | after |
|---|---|---|---|
| 1280x960@60 | 1 | +17.0 | **-1.1** |
| 1280x1024@60 | 1 | +16.9 | **+1.3** |
| 1024x768@60 | 2 | -1.9 | -1.9 |
| 320x480@60 | 2 | +10.1 | +9.6 |

The realised movement is 18.1 and 15.6 samples against the 16 written, which is
the two-samples-between-acquisitions spread below rather than a wrong constant --
the two before-and-after pairs are different acquisitions either side of a flash.

## Traps

- **The anchor belongs in ADC samples, not in source pixels, and it is not quite
  a constant either.** Anchored against anchor-free over five modes spanning a
  four-fold range of density it runs 1.67 to 2.76 samples -- a 48% spread,
  against 184% for the same differences read as source pixels. Some of that 48%
  is the cross-session drift below. Take it off before converting an anchored
  reading to source pixels, or do not convert it.
- **`VDS_HSCALE` is 10 bits and the scaler cannot shrink**, so the captured line
  has to fit the output raster, and that is what caps a divider hold -- about
  `PLLAD_MD` 1780 against a 1600-unit raster. Above it the card's far edge
  leaves the frame and the reading is refused rather than wrong. The same bound
  refuses `1400x1050@60` outright.
- **A feature below one capture unit per source pixel is caught by a single
  sample**, which quantises the crossing by half a unit -- 0.5 samples against
  the 2.2 the twins differ by, so it does not account for them. `PROCframe`
  draws one pixel and takes no width, so a thicker feature needs a source change.
- **One mode's reading moves about 2 samples between sessions** while repeating
  to 0.2 within one. Compare readings taken in one session, or carry the spread.
- **Origin readings at 75 Hz do not repeat at all.** 1125 lines at 75 Hz is not a
  standard mode; `800x600@75` read +8, +29, +17 and +15 samples on four
  acquisitions.
- **A reading taken while anything else drives the source is not a reading.** A
  mode change landing inside a creep moves the picture under a frozen unit, and
  the crossing that comes back is neither state -- measured, `1280x1024@60` read
  +10.2 that way against +17.3 undisturbed.
- **`counter_origin.py`'s vertical creep reports its own failure** by disagreeing
  with its expectation. The horizontal is the sound one.
