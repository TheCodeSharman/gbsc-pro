# The ADC sampling phase is never tuned, and its best value re-rolls per acquisition

`PA_ADC_S` is where in the ADC clock the converter takes its sample. Nothing
derives it: `Adc::acquirePhase()` sets it to `oversample == 4 ?
halfSampleOn(MidField) : MidField`, the middle of a five-bit field, which is
not a sampling instant. Only the sync processor's phase is searched.

**IT IS NOT ALIASING AND IT IS NOT A CAPTURE FAILURE, AND BOTH DISTINCTIONS
DECIDE WHAT HELPS.** The card's finest grating is one-pixel bars, which at the
bench 720p source's 74.25 MHz pixel clock is a **37.1 MHz** component; sampling
2038 kept at 45013 Hz is 91.7 MHz, so Nyquist is 45.9 MHz and **the grating is
captured faithfully** -- point sampling above Nyquist loses nothing in
principle.

What varies three-fold across the field is the SAMPLE VALUES, and pass-through
puts those on screen without an ideal reconstruction: at **2.47 samples per
grating period** the peak-to-peak of the samples themselves depends on where
they land. **So the defect is in reconstruction, not in capture**, and the
remedy is a reconstruction filter -- which is exactly what a decimation filter
is, and why oversampling is the right technique here rather than the wrong one.
What forbids it is the converter's rating, below.

## THE SEARCH FOR A PHASE IS CLOSED. Three findings close it together

1. **A per-mode trim cannot be a feature.** The draw re-rolls on every
   acquisition, so a tuned value is worthless the next time the source returns
   and the user would retune every mode every time.
2. **No automatic set is reachable.** No register encodes the offset -- it is the
   phase of a counter inside the sync processor's own loop -- and no register
   reports a measurement of the picture, so the chip cannot rank phases for
   itself. The sections below have both.
3. **It is a hardware limit, and the limit is the converter's analog sampling
   rate.** The remedy is oversampling at the full kept count, and that asks
   **183.5 MHz of a 162 MSPS part** -- DS-5725-3.2's front page. Where the
   rating has headroom the firmware already takes it; where the phase bites it
   cannot.

What remains is a note on where the phase bites and why, not a feature to
build. **Do not re-open this as a tuning or search problem.**

## What it costs

Measured on `vga`, RISC PC at 1280x720@60 in pass-through, `PLLAD_MD` 2038,
oversample 1, scored per pixel on PM5544's finest grating off the USB capture.

A full sweep of the field, box held, three interleaved passes:

| | phase | contrast |
|---|---|---|
| peak | 30 | 53.2 |
| trough | 11 | 17.4 |

**3.06x across the field**, repeatable to under one grey level.

Over eight re-acquisitions of that one unchanged mode:

| | across acquisitions |
|---|---|
| achievable peak contrast | **52.6, sd 0.42** |
| trough contrast | 18.2, sd 1.54 |
| **at the engine's fixed `MidField` 16** | **18.0 .. 51.6, a 2.9x spread** |

**The curve's SHAPE is constant and its POSITION is not.** Every acquisition
can reach about 52.6; which phase reaches it moves across the whole field, and
16 is sometimes on the peak and sometimes in the trough. That is the whole
phenomenon: a picture that is sharp on one acquisition and soft on the next
with every register the same.

## What it costs on ordinary content: 14% of the sharpest edges

The wedge is adversarial by construction -- sustained periodic content at
Nyquist, which is what makes it beat rather than merely soften -- so **the 3x
figure is not a picture-quality claim**. The measure that is one is the luma
gradient over the whole lit picture with the wedge's row band cut out, leaving
the grid lines, castellations, bar edges and centre circle: the 1 px features
that occur naturally. `sharpness.py`.

Scored off the SAME clips as the wedge, whole field swept, two interleaved
passes:

| | 1280x720@60 | 640x480@60 | 1280x1024@60 |
|---|---|---|---|
| wedge contrast, peak over trough | **3.01x** | **1.05x** | 1.63x |
| p99 \|dx\|, the sharpest edges | 1.142x | 1.068x | 1.092x |
| mean \|dx\|, the whole picture | 1.034x | 1.023x | 1.020x |
| mean \|dy\|, THE CONTROL | 1.003x | 1.006x | 1.046x |
| picture luma | 1.003x | 1.002x | 1.003x |

**`mean |dy|` IS THE CONTROL AND IT IS WHAT MAKES THE REST READABLE.** The
phase is where in the ADC clock a sample of the LINE is taken, so vertical
sharpness is whole lines and cannot move with it. A swing appearing in both
axes is brightness, a re-lock or the link, and is not the phase.

The two curves are in phase -- 1280x720@60 peaks at wedge 5 / p99 3 and
troughs at wedge 22 / p99 19 -- so it is one phenomenon rather than noise. But
the correlation across the field is only **r = 0.383**, the curves having
different shapes at the low end, so the picture reading is not a proxy for the
wedge and each has to be taken on its own.

**AND THE PHENOMENON IS SAMPLING DENSITY, WHICH THE DENSITY CONTROL SETTLES.**
640x480@60 runs the same divider 2038 across a shorter source line -- about
2.55 samples per source pixel against 1280x720@60's 1.235 -- and there the
wedge swing collapses to 1.05x while its absolute contrast rises from
17.6..52.8 to 77.6..81.6. Above two samples a pixel the grating is no longer
above Nyquist and the phase stops deciding what survives, exactly as the model
says. The same thing shows across the five pass-through modes at one divider:
the contrast falls monotonically with the source's pixel count, and
`test_passthrough_acceptance.py`'s `WEDGE_CONTRAST_FLOOR` carries the figures
as the derivation of its own bar.

Note `mean |dy|` reads 1.046x on 1280x1024@60 against 1.003..1.006x elsewhere,
and the whole of that is one phase: **phase 4 disturbs the control on two
separate sweeps**, 2.938 against 2.725..2.734 at 1280x720@60 and 2.604 against
2.491..2.522 at 1280x1024@60. What phase 4 does is unexplained and it is one
field position out of 32, so it is noted rather than pursued.

**The conclusion the priority rests on:** the phase costs 14% of the gradient
on the sharpest edges at the worst sampling density the bench can reach, and
2-3% over the picture as a whole. Not 3x.

## The path comparison is CONFOUNDED by where the wedge box lands

Captured at the dongle's native 1920x1080 throughout, box found per trial and
held across each sweep.

| path | mode | samples/pixel | `PLLAD_MD` | wedge peak/trough | swing |
|---|---|---|---|---|---|
| pass-through | 1280x720@60 | 1.235 | 2038 | 52.6 / 17.1 | **3.08x** |
| scaling | 800x600@60 | 1.360 | 1436 | 32.4 / 29.6 | **1.094x** |
| scaling | 1280x720@60 | 0.875 | 1444 | 11.3 / 11.2 | 1.009x |

**THAT TABLE DOES NOT ESTABLISH WHAT IT APPEARS TO, AND THE RESAMPLING
EXPLANATION IS REFUTED.** `wedge.locate()` finds the box per capture, and it
lands in a DIFFERENT PART of the frequency wedge on the two paths -- columns
518..652 in pass-through against 691..794 scaled. Measured on the scaling path
in one capture, the located box reads contrast 50.6 with column means spanning
21..227, a properly resolved grating, where the pass-through box's columns read
a flat 117..136 there. So the scaled grating is not resampled away and the
11.3 above is a box on the wrong content.

The wedge is a frequency sweep, so a box further along it is COARSER, and
coarse bars sit well above Nyquist where no sampling instant decides anything.
This page's own instrument section states the mechanism: a phase that aliases
the finest block to grey slides the box onto coarser bars. **A swing measured
through a box that relocated between the two states is a comparison of two
spatial frequencies, not of two paths.**

What survives is narrower and still useful: within ONE path at ONE divider,
with the box genuinely held, the sweeps are sound -- the controls return
`r = 1.000` and the lit box's left edge does not move across a phase sweep. So
the 3.08x in pass-through is real, and so is every result below that was taken
that way. **Whether the scaling path has a phase response is OPEN**, and
settling it needs the box anchored to the same feature of the SOURCE card on
both paths rather than found per capture.

**PATH AND DENSITY CANNOT BE FULLY SEPARATED, BECAUSE THE ENGINE CHOOSES THE
DIVIDER PER PATH.** The same source mode samples 2038 in pass-through and 1444
scaled, so the 1280x720@60 pair moves both at once and settles nothing on its
own; forcing one divider across both is what
`a-hand-set-divider-cannot-be-judged-against-a-solved-window.md` refuses.
**What carries the conclusion is 800x600@60 scaled sampling MORE densely than
pass-through 720p and still reading flat** -- 1.360 against 1.235, where this
page's own density curve does not collapse until about 2.55.

So the 14% above describes pass-through, where it was taken. **It does not
follow that the scaling path is free of the phenomenon**, only that nobody has
measured it through a box anchored to matched content.

**A FLAT FIELD'S ARGMAX IS NOISE, AND IT READS AS A RESULT EITHER WAY.**
`phase_pair.py` refuses one below 1.5x for that reason.
`no-scan-derives-the-adc-sampling-phase.md` has what a flat field reported,
and why its steadier-looking column is the artefact rather than the finding.

## What the instrument had to be

**THE WEDGE BOX IS FOUND, NOT STATED.** The card scales with the mode and its
layout does not scale with the emitted raster: over three pass-through modes at
1920x1080 the dense block sits at x 0.19..0.31 of the lit picture on 800x600@60
and 1024x768@60 and at 0.27..0.44 on 1280x720@60. A fixed fraction covers
grating on one mode and flat card on another, and flat card barely moves
between frames -- so the shipped box reported 1280x720@60 at a movement ratio
of 0.151 where the grating alone reads 0.578, nearly 4x steadier than the
truth, in the forgiving direction, on the one number meant to catch shimmer.

**AND THE BOX IS HELD ACROSS A SWEEP, NOT RE-FOUND PER POINT.** Re-finding it
makes the instrument follow the thing it measures: a phase that aliases the
finest block into grey moves the step out of flat card to the RIGHT, so the box
slides onto coarser bars and the worse phase scores HIGHER. Seen on
1024x768@60, where re-finding put the box at 364 for phases 3..21 and at
460..614 for 29..31, and the field's apparent best landed in the latter.

**NO CELL GRID.** A grating this fine is sensitive to where a cell boundary
falls on it; two runs of the same phases once ranked them in opposite orders
purely by where the grid landed. Scored per pixel there is no grid to align.

`tools/gbsc-pro-hwtest/wedge.py` locates and scores it, and the sweep prints
the lit picture's left edge with every reading so a frame that moved is visible
in the results rather than hidden in them. Measured at 1280x720@60, that edge
reads one value across all 32 phases: **the phase does not pan the picture**,
so the readings are of identical content.

## What is refuted

**The chosen `PA_SP_S`, as a constant offset.** Over eight re-acquisitions it
took the three adjacent values 30, 31 and 0 while the best `PA_ADC_S` ranged
over 2..28; the circular concentration of `(best - sp) mod 32` was 0.509.
**That test was also mis-specified** -- it can only find an additive
relationship, and a steep multiplicative one is indistinguishable from noise
under it, and it held the predictor nearly constant.

**The PICTURE's best `PA_SP_S`, which is the quantity with the variation**, and
with it any derivation off the scan at all. Measured over eight pass-through
re-acquisitions with both bests taken per acquisition: the sync phase's best
spans 3 steps where the ADC phase's spans 20, inside one capture origin. A
near-constant predictor cannot produce a target swinging two thirds of the
field. **The `PA_SP_S` picture-best does not re-roll per acquisition**, where
`PA_ADC_S`'s does, and that asymmetry is what closes it.
`no-scan-derives-the-adc-sampling-phase.md` has the per-trial figures, the
instrument, and what would reopen it.

**`PA_SP_S` as a cause at all.** Held at 26, 28, 29, 30, 31 and 0 with the
source untouched and the box held, the optimum read 4, 4, 4, 4, 6, 6 and the
whole curve was near identical at every setting. **The causal slope is about
one step per step.** So the two-step spread the engine's search shows cannot
produce the sixteen-step swing measured across acquisitions, and `PA_SP_S` is
a marker of the state rather than the state.

**A SINGLE DIFFERING REGISTER IS NOT A CAUSE.** Six acquisitions with a full
1536-register dump at each: the pair whose optima were furthest apart, 4
against 20, differed in **one byte** -- `PA_SP_S`, 29 -> 31 -- and were
byte-identical everywhere else. That reads as a smoking gun and is not one;
only holding the register and measuring its slope exposed it. Hold whatever a
diff names and measure its causal slope before believing it.

**The sync phase scan's DISTRIBUTION.** The scan is a 32-entry dither array,
already taken per acquisition and printed by `reportScan()`, and its band is a
measurement of where the sync edge lies in the ADC clock period. Over eight
re-acquisitions its circular centroid read **13.7..14.9** -- stable to about
one step -- while the best `PA_ADC_S` ranged 1.1..28.1. So the sync edge sits
in the same place in the clock period every acquisition, and whatever re-rolls
is downstream of that.

**Every test-bus selector.** 59 live selector and input-formatter signal
combinations swept at a clear phase and a murky one whose contrast differed
3.00x: none moved more than 8%. No signal on the bus carries sampled-video
high-frequency content, so transition counting cannot score the phase the way
it scores the sync processor's.

**A re-lock of the ADC PLL.** Automation frozen, source untouched, no
re-acquisition, five `PLLAD_VCORST` pulses: the optimum read 6 every time with
peak 52.3..52.5 and trough 17.5..17.7. The lock phase that a VCO reset
re-establishes is not the variable.

**That a one-sample shift of the capture origin moves the optimum**, by
arithmetic rather than measurement: the phase field spans exactly one ADC
period, which is exactly `1 / samplesPerPixel` of a pixel, and a one-sample
origin shift moves the sub-pixel phase by exactly that -- one whole field,
which cancels. A derivation resting on a residual there has none to rest on.

## What re-rolls it: the ADC PLL's own re-latch

Measured on `vga`, RISC PC at 1280x720@60 in pass-through, `PLLAD_MD` 2038,
**source untouched throughout** -- no mode change, no input change, no
re-acquisition. `/sampleclock` rewrites the PLL group at the value already held
and restarts it. Full step-1 sweep after each, box found once and held:

| re-latch | 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|
| optimum | 22 | 20 | 20 | 8 | 18 |
| peak | 52.6 | 52.6 | 52.5 | 52.5 | 52.5 |

The count held 749, `STATUS_SYNC_PROC_HTOTAL` 2038 against the divider and
`PA_SP_S` 29 at every leg. **A 14-step span with nothing about the source or
the chosen divider changing**, and the ceiling flat to a tenth of a grey level.

**THE PHASE ADJUSTERS' OWN RESTART IS NOT IT, which is what makes this the
PLL's.** `applyPhaseAdc()` pulses `PA_ADC_BYPSZ` 0 -> 1 on every apply, so a
32-point sweep restarts the adjuster's lock circuit 32 times; three interleaved
sweeps agree to under one grey level and `r = 1.000`. The adjuster's lock loop
settles to the same place every time within one acquisition. A bare
`PLLAD_VCORST` pulse does not re-roll it either, so what draws is the group
write and restart rather than the VCO re-lock.

**The practical consequence is that a fresh draw costs about six seconds** --
`/sampleclock?md=<the value already held>` -- against the two and a half
minutes a re-acquisition costs. Every experiment on this phenomenon gets
cheaper by that factor, and a comparison across draws no longer needs the
source disturbed.

## A DRAW ROTATES THE CURVE, so the shape is the mode's and only the offset rolls

Thirteen draws at 1280x720@60 in pass-through, divider 2038, one held wedge
box. Scored as the circular SHIFT that best aligns each curve to the one before
it rather than by where the optimum sat:

| | shift | residual at it | residual unrotated | peak |
|---|---|---|---|---|
| seven draws, before the ordering fix below | +16, +8, +9, +11, -5, +11 | 3.1..5.7 | 9.1..24.4 | 51.8..52.3 |
| six draws, after it | -14, -3, +13, +13, -4 | 1.4..6.6 | 4.7..18.3 | 50.9..52.5 |

Rotation carries most of every difference, against a control repeat of 0.67 --
so **the curve is a property of the mode and a draw moves its offset**. The
peak is within 1.6 of 52.5 on all thirteen, so the ceiling did not move either.

Two things follow. **The optimum is the noisier statement of the two**: the
peak is a plateau several steps wide, so it hops between neighbouring draws
that have not moved. And **the unknown is ONE quantity rather than 32** -- which
is what a per-mode constant would have to be tuned against, and why controlling
the roll is worth more than a trim that the next acquisition re-rolls.

The rotation is not exact: the residual at the best shift is 1.4..6.6 against
the control's 0.67, so something beside the offset moves as well. What it is is
not established.

## The phase applies were ordered wrongly, and that is NOT the draw

`Adc::applyPhases()` ran before `Adc::latch()` on both restart paths --
`VideoPath::restartAndLatch()` and the sketch's `restartAfterBypassSwitch()` --
and that latch reloads the ADC PLL group and re-locks the clock the two phase
adjusters align to. Neither `PA_SP_S` nor `PA_ADC_S` is in the group
`PLLAD_LAT` loads, so there was nothing to gain by preceding it. The applies
run after it now, and `test_video_path.cpp` pins the order.

**It does not stop the draw**, which is the finding rather than the fix. Seven
draws before and seven after, same mode, divider and box:

| | optima across the draws |
|---|---|
| before | 11, 1, 19, 20, 22, 6, 22 |
| after | 19, 4, 16, 2, 3, 15, 17 |

Both span the field. So the draw is in the PLL's own re-lock and not in when
the adjusters are restarted around it -- **which closes the whole sequencing
family**, the obvious place to look next. A lock circuit restarted against an
already locked clock is repeatable, measured: three interleaved 32-point
sweeps, each pulsing `PA_ADC_BYPSZ` 32 times, agree to under a grey level.

## The ADC PLL's own control bits do not redraw it, one at a time

Each pulsed on a settled pass-through acquisition at 1280x720@60, divider 2038,
one held wedge box, a 32-point sweep after each and a no-op leg first as the
control. A pulse writes the complement of what the bit holds and writes it
straight back, which is a rising edge on a bit resting high and a reset pulse
on one resting low. Scored as the shift against the previous curve:

| pulsed | what it does | shift | residual |
|---|---|---|---|
| none | the control | +0 | 0.25 |
| `PLLAD_LAT` | reloads MD, ND, KS, CKOS and ICP | +0 | 0.21 |
| `PLLAD_LEN` | the clock enable, off and on | +0 | 0.22 |
| `PLLAD_VCORST` | the VCO held and released | +0 | 0.21 |
| `PLLAD_PDZ` | the PLL powered down and up | +0 | 1.21 |
| `PA_ADC_BYPSZ` | the phase adjuster's own lock circuit | +0 | 0.93 |

**None of them moves the curve**, so the draw is in none of the parts the
restart is built from -- and reloading the whole group at its own values is
among them, which is the part the earlier reading named. `PLLAD_PDZ` left one
phase of its sweep unlit, which is the power cycle in flight rather than a
reading.

## IT IS THE SYNC PROCESSOR'S RESET, AND NOTHING ELSE AROUND THE RESTART

Pulsed the same way on the same settled acquisition, continuing the table
above. The sweep after each is scored as the shift against the one before it:

| pulsed | what it resets | shift | residual |
|---|---|---|---|
| none | the control | +0 | 0.13 |
| `ADC_POWDZ` | **the converter, powered down and up** | +0 | 0.16 |
| `SFTRST_DEC_RSTZ` | the decimation | +0 | 0.14 |
| `SFTRST_SYNC_RSTZ` | **the sync processor** | **+16** | 10.65 |
| `SFTRST_MODE_RSTZ` | mode detection | +0 | 4.45 |
| `SFTRST_IF_RSTZ` | the input formatter | +0 | 0.15 |

**Power-cycling the ADC does not move the sampling phase and resetting the
sync processor does**, which places the draw outside the converter entirely.

**THE DATASHEET STATES THE CHAIN OUTRIGHT, in `ADC_CLK_PLLAD` s5_00[2]**:
"PLLAD input clock is from sync processor" at 0, "from OSC" at 1. It reads 0 on
every path here, so the converter's clock is multiplied up from a pulse the
sync processor generates -- separator, retiming, HS to PLL
(`SP_HS2PLL_INV_REG` s5_56[1], "HS to PLL invert"), `PLLAD`, ADC clock. Both
phase adjusters sit on that clock and not on the data: `PA_SP_S` is documented
"PA_PLLAD phase control" despite its name, and `PA_ADC_S` follows it.

Resetting the sync processor restarts the block generating the reference, and
the phase follows it. **That is also why the two inert legs are inert rather
than badly measured.** The converter cannot hold a phase it does not generate,
and a PLL re-locking to an UNCHANGED reference returns to the same alignment by
definition -- which is what `ADC_POWDZ` and the `PLLAD_*` pulses each measure
at +0. It is equally why the source's own rigid relationship between sync and
pixels does not reach the converter: the clock is locked to a pulse the chip
regenerates, and the sync processor counts in ADC samples, so what the reset
re-draws is that counter's phase within one sample of its own output.

**AND IT DRAWS AFRESH, which is what makes it the lottery rather than a
control.** Four identical resets on settled acquisitions gave **+16, -7, -12
and +0** -- three moved the curve and the fourth landed back where it was,
which a draw over 32 offsets does. A no-op leg repeated three times gave +0 at
residuals 0.13, 0.15 and 0.25, so the +0 reset is the draw coming up short
rather than the method failing.

Each leg's sweep leaves one phase unlit, the source being briefly absent across
the reset, so these residuals are not comparable with the inert legs' and the
peak dips and recovers -- 52.0, then 49.7, then 51.9 and 51.8. The shift is the
finding; the rest is the reset in flight.

**What this does NOT license is removing the reset.** The sync processor
carries most of the per-mode state -- the coast window, both retime positions,
the clamp, `SP_H_PULSE_IGNOR` -- and whether any of those needs the reset to
take effect is unestablished. The PHASES do not: `applyPhaseSyncProcessor()`
loads through `PA_SP_LAT` and `PA_SP_BYPSZ`, a 32-point sweep moves the picture
with no reset involved, and a bare `PA_ADC_BYPSZ` pulse measures +0. Each call
site also has its own reason, and `VideoPath.cpp` states one that is nothing to
do with phase: the reset leaves the capture counter's origin at 0 and the
reference-clock install is what writes it.

## The sync processor's own test bus does not carry the draw either

Its test bus is the right domain -- the draw is in that block, and
`SP_TEST_MODULE` s5_63[1:3] exposes eight stages by name, **6 being the
retiming module** that generates the pulse the PLL locks to, with
`SP_TEST_SIGNAL_SEL` picking eight signals of each. `TEST_BUS_` at s0_2E is a
24-bit block, so a pair can be read as a value rather than counted as edges.
Bring-up already leaves `SP_TEST_EN` 1 on module 7, and holding test mode on
does not disturb the lock: eight draws at `STATUS_SYNC_PROC_HTOTAL` 2038
against the divider with peaks of 52.7..53.3.

**WHAT MADE THE READ WORTH TRYING** where the level path is closed: the level
path fails because an HTTP read samples the whole frame at 43 Hz against a
wedge that is 0.7% of it, whereas the draw's offset is STATIC for the life of a
draw. A value that is not moving can be read slowly.

**AND THAT PREMISE HOLDS FOR ONLY 27 OF THE 64.** Read ten times inside one
draw, **37 pairs dither** -- every signal of `cs_sep`, `retiming` and
`out proc` among them -- so a single read of one samples the wire's state and
varies whether or not the phase did. **A bare across-draw table therefore reads
as 44 varying signals when 37 of them are the read's own timing**, which is the
trap this measurement exists to avoid.

Of the 27 that hold still, across eight draws whose shifts were +8, -2, +1, -8,
-2, +10 and +14:

| | pairs |
|---|---|
| static within a draw and unchanging across all eight | **20** |
| static within a draw and varying across draws | 7, all of `vs_pol_det` |
| live, so unreadable by a single read | 37 |

**The seven are the bus in another state, not a signal.** They move only in
draws 3 and 8 -- and in those two scans nearly every pair including the live
ones reads `0x40xx`, so it is the scan rather than the pair. The shifts at
those draws were -2 and +14, which share nothing.

**SAMPLED ON THE DEVICE, THE LIVE ONES DO NOT CARRY IT EITHER.** `/sptestbus`
takes a distribution per pair from `loop()` -- 2000 samples each, all 64 pairs
in about a minute -- and reports its count, extremes, mean and value table, so
a live wire has a statistic rather than one asynchronous read.

Across draws whose shifts were -4 and +6, **40 of the 64 pairs have an
identical mean** and the rest move by a few dozen counts. Five move by almost
exactly 16384:

| pair | mean, draw 1 -> draw 2 | low bound either side |
|---|---|---|
| 5/4 `cs_sep` | 18588 -> 2036 | 16389 -> 0 |
| 5/6 `cs_sep` | 21049 -> 4810 | 20480 -> 4096 |
| 6/0 `retiming` | 21334 -> 4964 | 16384 -> 0 |
| 6/2 `retiming` | 49749 -> 33350 | 16574 -> 191 |
| 6/4 `retiming` | 17708 -> 4964 | 16388 -> 4 |

**That is bit 14 of the shared bus, and it is a RESET MARKER rather than a
phase.** It reads 1 on the first acquisition and 0 on every draw after it --
`[1, 0, 0, 0]` on all five pairs across four draws -- while the curve moved at
every one of those draws. One bit cannot encode a 32-step offset in any case,
and this one does not even move with it.

**AND THE DEVICE'S OWN VERDICT OVER SIX DRAWS IS THE SAME.** Judged on 2000
samples per pair rather than ten HTTP reads, **29 of the 64 dither inside a
draw** and of the 35 that hold still, **not one reads a different value at any
draw**. The two live counts differ -- 37 by ten reads, 29 by two thousand --
because a wire that sits still through ten reads is not a wire that sits still,
which is itself the argument for sampling on the device.

**AND NOTHING CORRELATES WITH THE OFFSET, which is the statistic rather than an
impression.** Six draws at cumulative offsets 0, -4, +2, +5, 0 and +12, every
pair's mean against the offset:

| pair | r | its means across the six |
|---|---|---|
| 2/1 `hs_act_det` | -0.845 | 77 77 77 77 77 **76** |
| 7/2 `out proc` | +0.633 | 1858 1857 1858 1858 1857 1858 |
| 6/5 `retiming` | -0.573 | 9868 9782 9604 9725 9788 9651 |
| 5/2 `cs_sep` | +0.536 | 3012 3047 3031 3123 3067 3084 |

**36 of the 64 means never move at all**, and of the 28 that do, the only one
clearing 0.8 moves by a single count in one draw of six. The largest real
amplitude is 2.7% of its own mean at r = -0.57, which six points cannot
distinguish from chance before 64 candidates are accounted for.

**What that does and does not establish.** Six draws against 64 candidates rule
out a STRONG relationship, not a weak one -- but a predictor good enough to set
a 32-step phase has to be near-deterministic, so what is ruled out is what
would have been useful.

So nothing the sync processor's test bus carries encodes the offset, read as a
value or sampled as a distribution. `sp_test_bus.py`, `/sptestbus`.

## Controls that do not reach it

Each measured on one held draw with the first leg repeated last as the control,
so a difference belongs to the bit rather than to the hour. None of them is a
fix and none is unexplored any more.

| control | what it is | result |
|---|---|---|
| `PA_ADC_LOCKOFF` s5_18[6] | the phase adjuster's lock circuit disable, written 0 at bring-up and never again | **inert** -- peak 52.6 / 52.8 / 52.6, `r = 1.000`, max difference 0.8 against a control difference of 0.8 |
| `ADC_FLTR` s5_03[5:4] | the ADC's own analog corner, 150 / 110 / 70 / 40 MHz, bring-up writes the widest | **attenuates, does not stabilise** -- peak falls 52.7, 51.5, 48.7, 40.8 while the swing holds 2.59x, 2.36x, 2.41x, 2.61x |
| `SP_HD_MODE` s5_3e[1] | SD against HD mode, written 0 unconditionally including for an HD source | **inert** -- peak 52.8 at every setting, swing 3.07x / 3.08x / 3.05x, identical RGB means |

**Why the analog filter cannot help, which is the general shape of the
problem.** At 2038 samples across a 1650-pixel line the sampler runs 91.7 MHz
against a grating fundamental near 37 MHz: the grating is already BELOW the
kept Nyquist of 45.9 MHz. So this is not out-of-band content folding down,
which is what an anti-alias filter removes -- it is sparse sampling of a
near-Nyquist signal, where the recovered contrast depends on where the samples
land. `ADC_FLTR` 3 at 40 MHz sits on top of the 37 MHz signal and kills it
rather than steadying it. **The filtering that would help acts at the KEPT
rate**, which is what a decimator does and an analog corner ahead of the
converter does not.

## `SP_JITTER_SYNC` ROTATES the curve, and it is the first control that moves it

`SP_JITTER_SYNC` s5_20[4] syncs the processor on both the rising and the
falling edge of HSync rather than the falling one alone, and is written 0
unconditionally. The ADC PLL locks to HSync, so which edges form its reference
is where it can decide the sampling instant -- and it does.

Measured on `vga` at 1280x720@60 in pass-through, divider 2038, one held draw,
the legs interleaved 0, 1, 0, 1, 0:

| leg | optimum | peak | trough | swing | mean | best circular shift against the first leg |
|---|---|---|---|---|---|---|
| 0 | 11 | 53.0 | 17.9 | 2.96x | 36.1 | -- |
| 1 | 25 | 52.0 | 17.5 | 2.97x | 40.1 | **+13, residual 6.02 against 21.81 unrotated** |
| 0 | 11 | 52.1 | 17.5 | 2.98x | 35.4 | +0, residual 0.68 |
| 1 | 25 | 52.0 | 17.2 | 3.02x | 39.8 | **+13, residual 5.82 against 21.82** |
| 0 | 11 | 52.0 | 17.6 | 2.95x | 35.4 | +0, residual 0.67 (control, `r = 1.000`) |

**It is a rotation and not a re-roll**: the same +13 of 32 steps twice, the
optimum landing on 25 both times and back on 11 every time it is cleared, with
the peak and the swing unmoved. The residual after rotating is 6.0 against the
control's 0.67, so the curve is not purely translated -- but the translation
carries three quarters of the difference.

**What it costs where nothing chooses it.** With a fixed phase of 16 the two
settings are not equivalent on this draw: 47.6 at `SP_JITTER_SYNC` 0 against
the rotated curve's 23.7, a factor of two on the same picture. Nothing in the
firmware chooses between them, so which one a build runs is whichever the
bring-up wrote.

It does not locate the optimum, so it is not the fix on its own. What it shows
is that the sampling instant has a deterministic, reversible control beside the
32-step field -- the first thing found that moves the draw at all.
`phase_curve.py --legs SP_JITTER_SYNC=0,1 --csv`.

## The ADC's own test bus is inert, which closes the ADC's domain

`ADC_TA_EN` s5_05[0] gates a test bus inside the ADC and `ADC_TA_CTRL`
s5_05[1:4] selects among sixteen, documented only as "ADC test bus control
bit". It was the last unscanned selector, and the one in the converter's own
domain where the phase lives rather than downstream of the decimators.

**It does nothing, on two independent instruments.** With the gate raised and
each of the sixteen selected in turn:

- **Nothing reaches `TEST_BUS_SEL`.** Every one of the 32 chip selectors reads
  inside the control's own envelope. Three candidates were flagged against a
  single control sweep and all three fell to 0.0 when re-read -- selectors 0
  and 2 carry a few transitions a window and 5, 11, 14, 15, 16 and 18 swing by
  200 to 385 between two windows on an untouched unit, so **one control sweep
  cannot score a leg here**. `adc_test_bus.py` takes the envelope from repeated
  controls interleaved with the legs.
- **The emitted frame does not move.** Wedge contrast 10.5 at every setting,
  frame mean 135.76..135.90 against a control of 135.90, and the lit box
  87..1813 identical throughout. A register that reads back is not proof the
  hardware acted on the write, so the picture is the instrument that matters:
  the gate reaches nothing the converter does either.

**`ADC_TEST` s5_0c[4:1] beside it is live and is destructive**, which also
proves the probe would have seen a response. "When = 1 the ADC output clock
will be invert / for ADC test / reserved", written 9 at bring-up. Three of its
four bits do something and the top one is inert -- every value and its +8
partner read identically:

| bits set | frame |
|---|---|
| neither of weight 2 or 4 (0, 1, 8, 9) | the picture, mean 136 |
| weight 2, weight 1 clear (2, 10) | nothing at all, mean 0.08 |
| weight 2, weight 1 set (3, 11) | a flat field, R 74.9 G 75.0 B 74.8 |
| weight 4, weight 1 clear (4, 12) | the solid green screen, R 2.8 G 99.5 B 82.2 |
| weight 4, weight 1 set (5, 13) | nothing at all |
| both, weight 1 clear (6, 14) | nothing at all |
| both, weight 1 set (7, 15) | a flat field, mean 74.9 |

So an inverted ADC output clock is not reachable as a coarse half-sample phase
step: every live combination is the green screen or no output, and the picture
returns the moment 9 goes back. **Neither field is a phase control and neither
is an instrument for one.**

## Oversampling is unavailable AT 2038, and reachable at 1740

Not untried: the tap does not exist. Oversampling here takes an EARLIER VCO tap
and decimates, `PLLAD_CKOS = postDivider - stepsFor(ratio)`, and the post
divider comes from RD-5725-1.1's crossover table at 80 / 40 / 20 MHz. CKO is
2038 x 45013 = 91.7 MHz, above the top row, so `PLLAD_KS` reads 0 and
`oversampleFor()` halves any request back to 1. The part's rating closes it
independently: 2 x 91.7 = 183.4 MHz against 162 MSPS.

**Upstream behaves identically**, so this is not a regression -- stock's
`setOverSampleRatio()` opens `case 2:` with `if (ks == 0) { setOverSampleRatio(1,
false); return; }`. What stock does carry that this fork does not is a
`hi_res && !bypass` branch raising `ADC_CLK_ICLK1X` at ratio 1 and bumping
`PLLAD_CKOS` at ratio 2; both are guarded off the pass-through path.

**Buying the tap costs kept samples, and that comparison is UNSETTLED.**
Oversampling becomes legal below the 80 MHz row, which at this line rate means
a divider of 1776 or less -- 1.076 samples per source pixel against 1.235, so
2.15 samples per grating period against 2.47. A run at `md=1776 os=2` read a
peak of 28.0 against 52.8, **and that figure is not evidence**: the wedge box
was found once and held across a divider change, which moves and rescales the
picture, so the held box lands on a different part of the grating. The divider
also rewrites `SP_RT_HS_SP` -- measured 32 -> 22 -- and that origin's formula
is calibrated for a sampling density and wrong off it, costing the grating far
more than the phase does.

**So a divider comparison in pass-through has to co-optimise the capture
origin**, or it measures the origin formula's error at that divider. Nothing
here says whether oversampling helps at a properly placed origin.

### THE CONVERSION RATE IS NOT THE BINDING CONSTRAINT, AND NEITHER IS ALIASING

The source is CEA-4, `h_timings:40,220,0,1280,0,110` -- 1650 pixels a line at a
74.25 MHz pixel clock -- so its information is band limited at **37.1 MHz**,
half the pixel rate, and any conversion rate above 74.25 MHz folds nothing.
Ratio 1 already converts at 91.7 MHz.

| | kept `PLLAD_MD` | ratio | conversion | samples per grating period |
|---|---|---|---|---|
| the engine's choice | 2038 | 1 | 91.7 MHz | **2.47** |
| the ratio-2 row | 1740 | 2 | 156.6 MHz | **2.11** |

**So raising the conversion rate buys nothing here**, which is why the ADC's own
analog corner buys nothing either -- every `ADC_FLTR` setting sits above
Nyquist on this board and only attenuates.
`the-analog-filter-corner-is-above-nyquist.md`.

What the eleven-bit counter costs is the quantity that DOES decide the phase
sensitivity: **kept samples per grating period**, capped at 2.48 on this
source.

### THE `ADC_CLK_*` FAMILY CANNOT RAISE IT, AND IS WHERE THE QUESTION LANDS

| field | what it selects | why it does not help |
|---|---|---|
| `ADC_CLK_ICLK2X` s5_00[3] | ICLK2X = the ADC output clock, or /2 | a **divider**. The input formatter counts at ICLK1X, so `kept = ICLK1X / lineRate` -- these can only LOWER the kept count. `Adc::applySampleRate()` writes both straight from the ratio |
| `ADC_CLK_ICLK1X` s5_00[4] | ICLK1X = ICLK2X, or /2 | as above |
| `ADC_CLK_PA` s5_00[1:0] | the PA_ADC adjuster's input clock: 00 PLLAD's CLKO2, 01 PCLKIN, 10 V4CLK | PA_ADC delays the clock the converter SAMPLES on, so a source not locked to the incoming hsync cannot deliver a fixed count per line. V4CLK is identified nowhere in RD-5725-1.1's extraction |
| `ADC_CLK_PLLAD` s5_00[2] | PLLAD's input: 0 the sync processor, 1 the OSC | 1 removes the draw by removing the lock to the source |

**So the family IS the mechanism** -- the ICLK stages are the oversampling and
`ADC_CLK_PLLAD` names where the draw comes from -- and no setting of it raises
the kept count or keeps a sync-locked sample clock without the draw. The last
two rows are ruled out by what a sampling clock has to do, not by measurement.

### IT LANDS CLEANLY, AND IT JITTERS UNTIL THE SYNC PHASE IS RE-SEARCHED

`/sampleclock?md=1740&os=2` applies: `PLLAD_KS` 1, `PLLAD_CKOS` 0,
`ADC_CLK_ICLK1X` 1, `DEC2_BYPS` 0, `STATUS_SYNC_PROC_HTOTAL` 1740 against the
divider, count 749 unchanged, `STATUS_MISC_PLLAD_LOCK` 1.

**The whole picture then jitters horizontally, and the cause is `PA_SP_S` left
over from the previous divider's search.** `/sampleclock` re-derives
`SP_RT_HS_SP` -- 32 to 23 -- and does not re-search the sync processor's phase.
Measured as the per-line wander of four vertical edges across frames, each
frame's whole-picture pan removed so a displacement cannot read as a wander:

| `PA_SP_S` | 0 | 4 | 8 | 12 | 16 | 20 | 24 | 28 |
|---|---|---|---|---|---|---|---|---|
| mean wander, px | 0.161 | 0.096 | 0.093 | **0.070** | 0.072 | 0.073 | 0.109 | **0.199** |

The value carried in is **29, the peak of that curve**, and the quiet band
12..20 reads 0.070 against a ratio-1 control at 2038 of 0.075..0.136 on the
same edges. **So the oversampled state is QUIETER than the control once the
phase is re-searched**, and the jitter is not VCO noise at 156.6 MHz.

**`SP_H_PULSE_IGNOR` IS NOT THE CAUSE, which is worth stating because the
arithmetic says it should be.** `/sampleclock` leaves it at
`pulseIgnoreFor(2038, true)` = 81, which is 4.66% of the 1740 line where this
block's own constants put the holding band at 2.75..2.81% and 4.9% inside the
dither. Measured either side, 81 and 48 give identical wander and an identical
`STATUS_SYNC_PROC_HTOTAL` spread of 1739..1741. A count dither is the wrong
instrument for a sub-pixel wander, and plus or minus one on that register is
inside the healthy band anyway.

### AND IT DOES NOT CURE THE GRATING, BECAUSE THE TRADE RUNS THE WRONG WAY

Read on the panel at the quiet phase, the finest grating is still troubled and
at best marginally better. The arithmetic says why, and it closes the route.

The grating's finest bar is one source pixel, so its period is two and Nyquist
asks for **more than one KEPT sample per source pixel -- 1650 a line**. That is
a bound on the kept count and not on the conversion rate, because the kept
count is what carries the pixels:

| | kept | samples per source px | **samples per grating period** |
|---|---|---|---|
| ratio 1 | 2038 | 1.235 | **2.47** |
| ratio 2 | 1740 | 1.055 | **2.11** |
| the counter's wall | 2047 | 1.241 | **2.48** |

**OVERSAMPLING IS THE RIGHT TECHNIQUE AND THE CONVERTER IS 13% TOO SLOW FOR
IT.** A kept sample at ratio 2 is not a discarded-neighbour point sample --
`DEC2_BYPS` names a 2x-to-1x stage, so it is filtered, and that filter is the
reconstruction the pass-through path otherwise lacks. What it cannot do is
extend BANDWIDTH past half the decimated rate, so the kept count still sets the
band.

| | kept | conversion needed | within 162 MSPS |
|---|---|---|---|
| ratio 1, as the engine runs it | 2038 | 91.7 MHz | yes |
| **ratio 2 at the same kept count** | 2038 | **183.5 MHz** | **no** |
| ratio 2 at the divider the row forces | 1740 | 156.6 MHz | yes, at 97% |

**The crossover table IS that rating expressed per row** -- every row tops out
at the same conversion rate, so the 80 MHz row is 162 MSPS halved and forces
kept to 1777 or less at ratio 2. At the 1740 that leaves, **the decimation
filter's transition band is centred at half its output rate, 39.2 MHz, against
the grating's 37.1 MHz** -- on the shoulder. So the filter removes the phase
variation by attenuating the component that was varying: softer and steadier
arrive together. At ratio 4 the shoulder falls to 19.6 MHz and the grating goes
entirely.

**PUSHING PAST 162 MSPS IS SELF-DEFEATING, not merely out of specification.**
The rating is a sample-and-hold settling limit, so past it the amplitude error
is largest where the input moves most between samples -- the 37 MHz grating,
which is the component the oversampling is there to protect. It would also be
the hardest result to read: an overclocked S/H degrades gradually rather than
failing to lock, so `STATUS_SYNC_PROC_HTOTAL` still reads the divider and the
picture is still there, subtly worse.

**WHERE THE RATING HAS HEADROOM THE FIRMWARE ALREADY TAKES IT**, which is what
makes this structural rather than a missed option: a 15.6 kHz source keeps 2047
AND runs ratio 4, converting 128 MHz, and `recommendedDivider()` chooses it. The
gap opens only on sources whose kept count needs more than 162 MSPS halved.

**There is no configuration that buys both, and the eleven-bit line counter is
why.** Resolving that grating with room to spare wants of the order of 3300
kept samples against a wall of 2047. **So the phase lottery on a
high-resolution source in pass-through is a symptom of a capture that cannot
carry the source's finest detail, and no choice of phase, ratio or divider
removes it.**

### THE TRADE IS QUANTIFIED: 37% of the grating for half the lottery's spread

Measured on `vga`, RISC PC at 1280x720@60 in pass-through, PM5544 with
`ANIM OFF`, automation frozen, `PA_SP_S` held at the phase the walk chooses for
each density -- 30 at 2038, 12 at 1740. Three interleaved passes over all 32
ADC phases per density, `adc_phase_sweep.py`, console quiet on every pass.

| | kept | mean | best | worst | spread | spread / mean |
|---|---|---|---|---|---|---|
| ratio 1 | 2038 | 33.8 | 42.8 at 18 | 20.5 at 3 | 22.3 | 66% |
| ratio 2 | 1740 | 21.1 | 26.5 at 25 | 16.0 at 20..23 | 10.5 | 50% |

**The box relocation does not confound it**, which is what makes the comparison
available at all: `wedge.locate()` takes its box as a share of the wedge's own
run rather than as a fraction of the frame, so both boxes start at the wedge's
own left edge -- 518 at 2038 against 519 at 1740.

**The picture does not move or rescale between the two**, which a pass-through
path invites doubt about: the lit box is the full frame at both densities, the
located left edge is one pixel apart, and `mean |dx|` over the whole picture
falls 6.6% where a horizontal compression would raise it.

**What does differ is the box's RIGHT edge**, 633 against 653, the detected
wedge run being longer at 1740. That puts the ratio-2 box 20 px further onto
coarser bars, which biases its contrast UPWARD -- so the deficit measured
below is the conservative end of the real one.

**And the clips carry their own control.** `mean |dy|` is the vertical
gradient, which a horizontal sampling phase cannot touch, and it is read off
the very frames the wedge is scored on:

| | the finest grating | the whole picture, `mean dx` | **the control, `mean dy`** |
|---|---|---|---|
| ratio 1 | 33.8 | 3.051 | 2.944 |
| ratio 2 | 21.1 | 2.849 | 2.837 |
| ratio 2 costs | **37%** | 6.6% | **3.6%** |

37% lost on content at 37.1 MHz beside 3.6% on content nowhere near it is a
low-pass filter rather than a global softening, and it puts the decimation
stage's shoulder where the arithmetic above puts it.

**So the spread halves and the whole distribution falls below it.** 25 of the
32 phases at ratio 1 beat the BEST phase at ratio 2 -- a draw landing anywhere
on the ratio-1 curve beats ratio 2 at its optimum 25 times in 32. The lottery
is milder and it is played for less, which settles the direction the section
above could only argue.

**The engine's own phase makes ratio 2 worse still rather than better.**
`acquirePhase()` writes `MidField` 16 whatever the ratio. On the ratio-1 curve
measured here 16 reads 42.4, within 1% of that curve's peak; on the ratio-2
curve it reads 16.9, nine tenths of a grey level off its floor. Both are ONE
draw of a quantity the re-latch section above shows rotating over a 14-step
span, so neither is a property of the ratio -- but a ratio-2 reading taken as
the firmware would actually configure it is the bottom of an already lower
curve.

**Spread is the sound statistic across densities and the optimum is not**,
which is what this measurement turns on. The peak and the optimum's position
both move with the draw, so a single sweep per density compares two draws; the
spread is a property of the whole curve and survives a rotation of it.

**RATIO 1 DOMINATES IT OUTRIGHT, which is what closes the trade.** The spread
being milder would still be worth something if the two curves overlapped, and
they do not:

| | floor | ceiling | phases below ratio 2's floor |
|---|---|---|---|
| ratio 1, draw A | 20.5 | 42.8 | **0 of 32** |
| ratio 1, draw B | 17.5 | 42.7 | **0 of 32** |
| ratio 2 | 16.0 | 26.5 | -- |

No phase at ratio 1 is worse than the worst phase at ratio 2, and 9 to 14 of
ratio 2's own phases are worse than the worst of ratio 1's. **So there is no
risk-averse case for ratio 2 either** -- it does not trade a better worst case
for a worse best one, it loses at both ends. A milder lottery whose whole range
sits below the other's floor is not a safer bet.

### 1777 IS THE CEILING AT RATIO 2, AND ASKING PAST IT INSTALLS RATIO 1

The headroom left between the divider the row forces and the converter's
rating is 1740 against `162e6 / 2 / 45013` = 1799, so the obvious next move is
to spend it. Measured, it is not there to spend:

| asked | `PLLAD_KS` | `DEC2_BYPS` | ratio installed | mean | best |
|---|---|---|---|---|---|
| 1740 os 2 | 1 | 0 | **2** | 24.1 | 26.6 |
| 1777 os 2 | 1 | 0 | **2** | 23.8 | 28.2 |
| 1800 os 2 | 0 | 1 | **1** | 27.4 | 34.4 |
| 1850 os 2 | 0 | 1 | **1** | 32.1 | 41.7 |
| 2038 os 2 | 0 | 1 | **1** | 22.5 | 31.7 |

**The ratio comes back reduced rather than refused**, which `oversampleFor()`
does by design -- the post-divider row has no faster tap, so a ratio the clock
cannot give is halved. `reportSampleClock()` prints `DEC2_BYPS`, so the console
says which ratio actually landed; a register read taken over HTTP instead will
report the divider asked for beside a decimator nobody looked at, and the three
ratio-1 rows above then read as oversampling pushed further.

So the usable range at ratio 2 is 1740..1777, and across it nothing moves: mean
24.1 against 23.8. **The crossover row binds before the rating does**, and the
3.4% of kept count the rating would still allow is not reachable.

Those readings are taken at `PA_SP_S` 9, which is where the walk landed at each
divider and which sits inside the dirty band -- so their absolute levels are
depressed against the sweeps above and only the comparison WITHIN the table is
sound. That is enough for the question it answers, which is where the ratio
stops being installed.

## NO REGISTER REPORTS A MEASUREMENT OF THE PICTURE, so no on-device search
## can rank phases

The hunts on this page all ask whether some register ENCODES the draw. The
other question is whether the chip can SCORE a phase for itself, and the
register map answers it without a bench run: it cannot.

The status area is s0_00..s0_1b and is fully accounted for -- the IF, sync
processor, VDS, memory FIFO, GPIO, interrupt and misc blocks -- and **not one
field in it reports a measurement of picture content**. The detectors that
compute one keep the result internally:

| detector | what it computes | what is readable |
|---|---|---|
| `VDS_BLEV_AUTO_EN` | Y min and max over the picture, for black-level expansion | nothing. `VDS_USER_MIN`/`MAX` are the MANUAL-mode inputs, not readbacks |
| `MADPT_HFREQ_DET_EN` | a high-frequency indicator in auto mode | nothing. `MADPT_HFREQ_ID` is the user's value for manual mode |
| `MADPT_NOISE_EST_EN`, `MADPT_STILL_NOISE_EST_EN` | global and still noise estimates | nothing. `MADPT_*_NOISE_VALUE` are inputs |

So a runtime search needs an instrument outside the register file, which leaves
the test bus -- and the arithmetic in this page's open list puts that at the
order of a million samples per phase to resolve a wedge that is 0.7% of the
frame. **A chip-computed quality metric is not a route that was tried and
failed; the hardware does not expose one.**

**AND THE DEFECT IS NOT PRESENT IN WHAT SUCH A METRIC WOULD MEASURE.** The
phase costs 14% on the sharpest edges and 2-3% over the picture, so on ordinary
content there is no signal to rank phases by. A metric needs the artefact to be
visible in what it scores, and only sustained near-Nyquist content makes it so.

## The lottery, in one acquisition

The engine holds `PA_ADC_S` at `MidField` 16 whatever the draw. Two draws of
the same source, mode and divider, each a full step-1 sweep with the box held:

| | reachable peak | at the engine's 16 | share of the peak |
|---|---|---|---|
| one draw | 52.1 at phase 15 | 51.6 | **99%** |
| the trough of that same draw | | 13.4 at phase 5 | 26% |

Same acquisition, same everything: the field spans 51.6 down to 13.4 and where
the engine sits is the draw's business. That is the artefact as a viewer meets
it, and it is why the fix is a search rather than a constant --
`no-scan-derives-the-adc-sampling-phase.md` has why no rule names the value.

## What is open

- **THE CEILING, WHICH IS A DIFFERENT QUANTITY FROM THE OPTIMUM AND IS WHY A
  BYTE-IDENTICAL DIFF IS NOT THE REFUTATION IT READS AS.** The achievable peak
  is recorded above as 52.6 with sd 0.42, and one acquisition reached **43.9**
  on the same wedge box, mode and divider with the console certified quiet,
  between two that reached 53.2. On such an acquisition no phase reaches what
  its neighbours reach and the picture is worse everywhere rather than at the
  engine's phase, which is what a viewer reports as the artefact being more
  pronounced. **Every register hunt so far grouped its dumps by where the
  OPTIMUM sat.** Grouping by the ceiling has never been done, and it is the
  same six dumps' worth of work.

  The 43.9 is a maximum over eight phases at step 4, so a peak narrower than
  the measured 6-wide plateau could be missed, and the box was located with a
  hand-set phase in force. A full step-1 sweep in a low-ceiling state is what
  confirms the figure, and `snapdiff.py --save` at both ends is what the diff
  needs -- a `dump_registers.py` dump leaves 928 addresses out, 114 of them
  live on a settled unit.
- **WHAT the PLL's re-latch draws, now that the sequencing is excluded.** The
  group write and restart re-rolls it, a bare `PLLAD_VCORST` does not, and
  moving the phase applies after the group's latch does not stop it -- so the
  draw is in the re-lock itself. **`phase_curve.py --pulse` is how it is asked
  one part at a time**: it writes the complement of a one-bit field and writes
  it straight back, which is a rising edge on a bit resting high and a reset
  pulse on one resting low, and reports the shift that followed against a no-op
  leg as the control. `PLLAD_LAT`, `PLLAD_LEN`, `PLLAD_VCORST`, `PLLAD_PDZ`.
  **This is what a per-mode constant depends on**: the curve is the mode's and
  only the offset rolls, so a roll that can be stopped or made repeatable turns
  the phase into something tunable once, and a trim without that is re-rolled
  by the next acquisition.
- **Whether `TestBus` level readback can score it. STILL OPEN, and an HTTP
  read cannot answer it.** The level path is proven -- `AutoGain` reads the
  converter's ten bits through `TestBus::selectLevel()` and `readLevel()`, and
  driven the way the firmware drives it, `TEST_BUS_SEL` 15 with `TEST_BUS_EN`
  and `PAD_BOUT_EN` raised, green returns a 0..1020 distribution. A sweep taken
  over HTTP is not an instrument for it: at 43 Hz the readings collapse to a
  few values quantised to 64 and 128, and the statistics swing by 270..390%
  across the field against contrast at `r = -0.2..-0.35`, which is noise rather
  than a null.

  **The arithmetic says what would be needed.** The read is I2C paced and
  uncorrelated with pixel position, so it samples the WHOLE frame; the wedge is
  0.7% of it, and a 3x swing there moves the total variance about 0.04%.
  Resolving that wants of the order of a million samples per phase. On-device
  sampling through `SamplingLog` reaches perhaps 20 kHz against HTTP's 43 Hz,
  which makes it minutes per phase -- **a bench experiment, and not a runtime
  search** even if it reads. Transition counting is separately closed: 59
  selector and formatter combinations against a 3.00x contrast difference, none
  moving more than 8%.

  `TEST_BUS` itself is absent from `tv5725_registers.json`, which carries only
  `TEST_BUS_2E` and `TEST_BUS_2F`; the firmware's `TEST_BUS` is the 16 bits
  across both, masked to ten. Selector 14, the red level, returns a narrow
  quantised range where green and blue return the full span, and that is
  unexplained.
- **Whether 14% on the sharpest edges is worth a search.** The measurement is
  taken; the judgement is not made -- and it is a PASS-THROUGH 14%. **What the
  scaling path does is OPEN and not flat**, every sweep taken there having gone
  through a box this page's confound section refutes.
- **The sync phase's own headroom, which is a different question from this
  page's.** The engine's choice is repeatable and sits a few steps off the
  picture's best on a curve that swings several fold, and a sweep inside one
  acquisition names a curve rather than a value, so moving it needs a
  measurement nobody has taken.
  `no-scan-derives-the-adc-sampling-phase.md`.

`/phase?adc=N` holds the phase against the engine and `/phase?clear` gives both
phases back. **Neither makes the engine re-choose**, and an override survives
every re-acquisition because `choosePhaseAdc()` returns early while it is set
-- so a register read after either names the last experiment rather than the
firmware, and the order to reach the engine's own value is `clear` and THEN a
re-acquisition. A restart clears every hold at once, the overrides being
statics. `resolve_watch` certifies that no re-solve crossed a reading, which a
hold does not: the arm takes the sync pad away and the re-install re-latches
the ADC PLL whatever is held. `tools/gbsc-pro-hwtest/adc_phase_sweep.py` walks the field and
reports the wedge and the picture off one clip, `sharpness.py` is the picture
measure and `wedge_calibrate.py` reads every mode so a bar can be set,
`phase_hunt.py` dumps every register per acquisition and diffs by optimum, and
`phase_correlation.py` and `phase_distribution.py` are the two refuted
correlations.
