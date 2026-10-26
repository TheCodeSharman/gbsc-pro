# No scan derives the ADC sampling phase

`PA_ADC_S` is where in the ADC clock the converter takes its sample. Nothing
sets it from a measurement: `Adc::acquirePhase()` writes `oversample == 4 ?
halfSampleOn(MidField) : MidField`, the middle of a five-bit field. Below about
two ADC samples per source pixel that choice decides how much of the source's
finest detail survives, and the phase that wins re-rolls on every acquisition.
`the-adc-sampling-phase-is-a-lottery.md` is the phenomenon and what it costs.

This page is the model for deriving it rather than searching for it, and the
measurement that refutes it.

## The model, and why it was convincing

The sync processor's phase, `PA_SP_S`, IS searched -- `Adc::acquirePhase()`
walks the field, scores each phase by whether the line count reads the divider
exactly, and takes the middle of the widest clean run. That scan is a
measurement of where the source's sync edge falls inside the ADC clock period,
taken every acquisition and already paid for.

Both phases are positions within the same clock period, and the thing that
re-rolls is upstream of both adjusters: it is in no register, six acquisitions
with full 1536-register dumps being byte-identical but for `PA_SP_S` while the
best `PA_ADC_S` moved 4 to 20. A shared hidden cause is exactly what that looks
like. If the two optima moved together at a fixed distance, the scan would
already contain the answer and `PA_ADC_S` would need no search of its own --
one subtraction, no instrument, no extra acquisition time.

## Why the first test of it had no power

`phase_correlation.py` correlated the best `PA_ADC_S` against the phase the
ENGINE CHOSE for `PA_SP_S`. Over eight re-acquisitions that chosen value took
30, 31 and 0 -- all one band -- while the best `PA_ADC_S` ranged over 2..28. A
run that holds its predictor constant cannot find or reject a relationship with
it, whatever the target does, and the circular concentration of
`(best - chosen) mod 32` it reported, 0.509, is a statistic about nothing.

**The chosen phase and the picture's best are different quantities.** The scan
scores the COUNT, so what it answers is which phases let the sync processor
measure the line; the picture's own best is a different question and is where
the variation is.

## What the instrument had to be

`phase_pair.py` measures BOTH picture-bests per acquisition, so the predictor
varies within the run. Five properties are load-bearing:

- **The box is found once per acquisition and held across both sweeps.**
  Re-finding it per point makes the instrument follow the thing it measures: a
  phase that aliases the finest block to grey slides the box onto coarser bars
  and the worse phase scores higher.
- **Step 1 over both fields.** The plateau is about six wide, so a step of four
  can miss a narrower peak.
- **The ADC field is swept first, with the engine's own sync phase in force;
  then the sync field with the ADC pinned where the engine left it.** Each best
  is measured against one baseline rather than against the other sweep.
- **`SP_RT_HS_SP` is recorded as a witness at both ends of every trial.** It
  places the capture counter's origin and a wrong one costs the grating far
  more than either phase, so a run whose origin moved is one to cut rather
  than to interpret.
- **A black frame is the worst reading, not a broken instrument.** At 1.36 ADC
  samples a source pixel a sync phase in the field takes the output to nothing,
  on six of eight acquisitions of 800x600@60; aborting there discards the trial
  that was about to name the band.

Captured at the dongle's native 1920x1080 throughout. Asking for the source's
own size returns a resampled frame, and the resampler's own filtering then
answers the clarity question.

## The run

Eight re-acquisitions of 1280x720@60 in **pass-through**, `PLLAD_MD` 2038,
source bounced away to 800x600@60 and back between trials so each is a genuine
re-acquisition. `chosen_sp` is what `Adc::acquirePhase()` picked; the two
`best` columns are the picture's, off PM5544's finest grating.

| trial | `PA_SP_S` chosen | `PA_SP_S` best | `PA_ADC_S` best | offset | `SP_RT_HS_SP` | ADC swing | SP swing |
|---|---|---|---|---|---|---|---|
| 1 | 30 | 25 | 31 | 6 | 33 | 3.08x | 2.40x |
| 2 | 30 | 30 | 26 | 28 | 32 | 3.13x | 2.53x |
| 3 | 30 | 1 | 6 | 5 | 32 | 2.97x | 3.17x |
| 4 | 30 | 31 | 23 | 24 | 32 | 3.06x | 3.38x |
| 5 | 30 | 0 | 21 | 21 | 32 | 2.51x | 4.20x |
| 6 | 30 | 25 | 28 | 3 | 33 | 3.11x | 2.84x |
| 7 | 30 | 4 | 7 | 3 | 33 | 2.98x | 3.72x |
| 8 | 30 | 1 | 18 | 17 | 32 | 2.28x | 5.91x |

Every field swings 2.28x or more, so neither argmax is noise -- which is the
precondition the companion run below fails.

## The refutation

| | across all 8 | at `SP_RT_HS_SP` 32, five of them |
|---|---|---|
| picture-best `PA_SP_S` | 25, 30, 1, 31, 0, 25, 4, 1 -- span 11 | 30, 1, 31, 0, 1 -- **span 3** |
| picture-best `PA_ADC_S` | 31, 26, 6, 23, 21, 28, 7, 18 -- span 21 | 26, 6, 23, 21, 18 -- **span 20** |
| offset concentration | **0.222** | 0.384 |

**The predictor is near constant where the target swings two thirds of the
field.** A function of a quantity spanning three steps cannot produce an output
spanning twenty, at any slope the causal measurement allows -- `PA_SP_S`'s
slope on the ADC optimum is about one step per step, held at 26, 28, 29, 30, 31
and 0 with the source untouched.

**And it holds inside ONE capture origin**, which is what makes it a
refutation rather than another underpowered run: the five trials at
`SP_RT_HS_SP` 32 share an origin, so the one-step movement across the full set
cannot account for the scatter. The three trials at origin 33 have a
concentration of 0.962, which reads as the fixed offset the model wants and is
three points with a predictor spanning eleven -- too few to weigh against the
five.

So the two bests do not move together, no fixed offset exists, and nothing the
scan measures predicts the ADC optimum.

## What the same run settles about the sync phase

**The `PA_SP_S` picture-best does NOT re-roll per acquisition**, where
`PA_ADC_S`'s does -- span 3 against span 20 at one origin. That was recorded as
unmeasured and is the asymmetry the whole result rests on.

**The scan's own choice is repeatable and close.** It picked 30 on all eight
acquisitions, and the picture's best sat 0..6 steps away, mean 3.12, on a curve
swinging 2.4..5.9x. **That is not licence to move it**: each best comes from a
sweep inside one acquisition, which writes the register without re-acquiring,
so it names a curve and not a value. What it bounds is worth measuring
properly.

## The run that cannot answer it, and why it looks like it can

The same eight trials on 800x600@60 on the **scaling** path, `PLLAD_MD` 1436:

| trial | `PA_SP_S` chosen | `PA_SP_S` best | `PA_ADC_S` best | offset | ADC swing |
|---|---|---|---|---|---|
| 1 | 27 | 21 | 29 | 8 | 1.09x |
| 2 | 28 | 21 | 31 | 10 | 1.11x |
| 3 | 27 | 2 | 29 | 27 | 1.09x |
| 4 | 27 | 16 | 29 | 13 | 1.09x |
| 5 | 26 | 3 | 26 | 23 | 1.06x |
| 6 | 28 | 28 | 6 | 10 | 1.10x |
| 7 | 28 | 0 | 31 | 31 | 1.10x |
| 8 | 27 | 21 | 29 | 8 | 1.09x |

It reports an offset concentration of 0.301, and the `PA_ADC_S` column looks
STEADIER than the pass-through one -- 29 four times out of eight. **Both
readings are artefacts of a flat field.** Peak and trough are about three grey
levels apart, so the argmax picks between near-identical readings and whatever
clustering appears is which noise won.

**Low sampling density is necessary and not sufficient.** This mode samples
1.360 per source pixel against pass-through 720p's 1.235, so density does not
excuse it; the scaling path resamples the grating away before it reaches the
capture, and no sampling instant can decide what survives a stage downstream of
it. `the-adc-sampling-phase-is-a-lottery.md` carries that measurement.

`phase_pair.py` refuses a field whose peak over trough falls below 1.5 for this
reason, the threshold separating every pass-through mode on record -- 1.05x
flat, 1.63x and 3.01x not.

## What would reopen it

- **A predictor that is not the scan.** The refutation is of `PA_SP_S`, chosen
  or best. Nothing here bounds a quantity nobody has measured.
- **A multiplicative or non-monotonic relation.** The offset statistic finds an
  additive one, so a steep multiplicative law is indistinguishable from scatter
  under it. The span argument is the stronger half and does constrain this:
  any law mapping a 3-step input range onto a 20-step output range has a slope
  near seven, against a measured causal slope near one.
- **A second instrument agreeing on the picture-best.** Every best here is a
  wedge-contrast argmax. The whole-picture gradient and the wedge correlate at
  only r = 0.383 across the field, so the two are not proxies for each other
  and a derivation scored on one is not scored on the other.

What does NOT reopen it is a re-run on the scaling path, at any density.
