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

## Every figure here is a PASS-THROUGH figure, and the scaling path has no phase response

Captured at the dongle's native 1920x1080 throughout, box found per trial and
held across each sweep.

| path | mode | samples/pixel | `PLLAD_MD` | wedge peak/trough | swing |
|---|---|---|---|---|---|
| pass-through | 1280x720@60 | 1.235 | 2038 | 52.6 / 17.1 | **3.08x** |
| scaling | 800x600@60 | 1.360 | 1436 | 32.4 / 29.6 | **1.094x** |
| scaling | 1280x720@60 | 0.875 | 1444 | 11.3 / 11.2 | 1.009x |

On the scaling path the grating's column means read a flat 93..103 where
pass-through swings 66..179 between adjacent columns: the VDS resamples the
block away before it reaches the capture, and the absolute contrast falls with
it. No sampling instant can decide what survives a stage downstream of it.

**PATH AND DENSITY CANNOT BE FULLY SEPARATED, BECAUSE THE ENGINE CHOOSES THE
DIVIDER PER PATH.** The same source mode samples 2038 in pass-through and 1444
scaled, so the 1280x720@60 pair moves both at once and settles nothing on its
own; forcing one divider across both is what
`a-hand-set-divider-cannot-be-judged-against-a-solved-window.md` refuses.
**What carries the conclusion is 800x600@60 scaled sampling MORE densely than
pass-through 720p and still reading flat** -- 1.360 against 1.235, where this
page's own density curve does not collapse until about 2.55.

So the 14% above, and every swing on this page, describes pass-through. The
everyday scaling path shows no ADC phase response at all, which is where the
judgement about whether a search is worth making has to start.

**A FLAT FIELD'S ARGMAX IS NOISE, AND IT READS AS A REFUTATION.**
`phase_pair.py` refuses one for that reason: run on 800x600@60 scaled it
returned an offset concentration of 0.301 over eight acquisitions, which looks
like a scattered relationship and is a scattered measurement of nothing.

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

**The PICTURE's best `PA_SP_S`, which is the quantity with the variation.**
That is what the run above could not reach and what `phase_pair.py` measures:
both bests, per acquisition, step 1 over both fields. Eight re-acquisitions of
1280x720@60 in pass-through at `PLLAD_MD` 2038, every field swinging 2.3..3.1x
so neither argmax is noise:

| | across all 8 | at `SP_RT_HS_SP` 32, five of them |
|---|---|---|
| picture-best `PA_SP_S` | 25, 30, 1, 31, 0, 25, 4, 1 -- span 11 | 30, 1, 31, 0, 1 -- **span 3** |
| picture-best `PA_ADC_S` | 31, 26, 6, 23, 21, 28, 7, 18 -- span 21 | 26, 6, 23, 21, 18 -- **span 20** |
| offset concentration | **0.222** | 0.384 |

**The predictor is near constant where the target swings two thirds of the
field**, and that holds INSIDE one capture origin, so the origin's own one-step
movement cannot account for it -- which is what the recorded witness is for.
So the two bests do not move together, no fixed offset exists, and nothing the
scan measures predicts the ADC optimum. **The sub-question of whether the
`PA_SP_S` picture-best re-rolls per acquisition is answered with it: it does
not**, where `PA_ADC_S`'s does.

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
- **What in the acquisition sequence re-rolls it.** It survives a VCO reset and
  it is not in the register space, since the dumps are identical but for a
  register whose slope is too small. The remaining candidates are the divider
  latch, the sync processor reset, and the reference-clock install.
- **Whether `TestBus` level readback can score it.** `AutoGain` already reads
  the converter's ten-bit sampled level through `TestBus::selectLevel()` and
  `readLevel()`, so a level path exists and is proven. Whether the spread of
  that distribution has the resolution to grade 32 phases from samples
  scattered over a whole picture is untested, and the doubt is real:
  `AutoGain` only asks "did anything clip", a threshold question with a wide
  margin.
- **Whether 14% on the sharpest edges is worth a search.** The measurement is
  taken; the judgement is not made -- and it is a PASS-THROUGH 14%, the scaling
  path showing no response at all.
- **The sync phase's own headroom, which is a different question from this
  page's.** Across the eight pass-through acquisitions above the engine chose
  30 every time while the picture's best sat 0..6 steps away, mean 3.12, on a
  curve swinging 2.4..5.9x. **That is not licence to move it**: each best comes
  from a sweep inside one acquisition, which writes the register without
  re-acquiring, so it names a curve and not a value. What it bounds is worth
  measuring properly.

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
