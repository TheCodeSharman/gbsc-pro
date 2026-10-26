# The ADC sampling phase is never tuned, and its best value re-rolls per acquisition

`PA_ADC_S` is where in the ADC clock the converter takes its sample. Nothing
derives it: `Adc::acquirePhase()` sets it to `oversample == 4 ?
halfSampleOn(MidField) : MidField`, the middle of a five-bit field, which is
not a sampling instant. Only the sync processor's phase is searched.

Below two ADC samples per source pixel the card's finest grating sits above the
sampler's Nyquist, so the phase decides how much of it survives.

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

## Oversampling is unavailable at the divider this mode runs

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
- **WHAT the PLL's re-latch draws, now that the re-latch is named.** The group
  write and restart re-rolls it and a bare `PLLAD_VCORST` does not, so the draw
  is inside `restartPll()` and the group it loads rather than in the VCO's own
  re-lock. Bisecting further by hand is what `/sampleclock` exists to prevent,
  so this wants a firmware-side split rather than register pokes.
- **`SP_JITTER_SYNC` s5_20[4]**, sync on both edges against falling only,
  written 0 unconditionally. The ADC PLL locks to HSync, so which edges form
  its reference is not obviously unrelated to where it locks. Untested.
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
- **THE ADC HAS A TEST BUS OF ITS OWN AND IT HAS NEVER BEEN SCANNED**, so "the
  test bus is exhausted" is not yet a statement anyone may make. `ADC_TA_EN`
  s5_05[0] gates it -- "ADC test enable, when 0 the ADC works normally" -- and
  `ADC_TA_CTRL` s5_05[1:4] selects among sixteen, described only as "ADC test
  bus control bit". The firmware writes the gate 0 at bring-up and never again.
  The 59-combination sweep that closed transition counting walked
  `TEST_BUS_SEL` and `IF_TEST_SEL`, neither of which is this.

  It is the one remaining candidate in the ADC's own domain, which is where the
  phase lives, and the two digital selectors are not: a signal upstream of the
  decimators could carry what the kept samples no longer do. Enabling it puts
  the ADC in test mode, so the picture is at risk and `/sc?~` is the recovery.
- **Whether 14% on the sharpest edges is worth a search.** The measurement is
  taken; the judgement is not made -- and it is a PASS-THROUGH 14%, the scaling
  path showing no response at all.
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
