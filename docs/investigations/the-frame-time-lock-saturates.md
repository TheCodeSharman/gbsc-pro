# The frame time lock saturates, and the phase wrap does not explain it

**The residual shake is the rate correction sitting at its +-0.06% clamp and
changing sign, and what causes that is NOT KNOWN.** Two models have been tried
against the bench and the second is refuted; what is established is the shape of
the fault, the arithmetic of the loop, and that the one-shot rate match is
innocent.

**THE PHASE IS NOT LOGGED, AND EVERY MODEL HERE IS INFERRED FROM THE DISPLAY
CLOCK INSTEAD.** The clock is two steps downstream of the phase -- phase to
correction to clock -- so a noisy phase and an oscillating one reach it looking
similar. Anything further wants the phase, the target and the error printed per
correction. Reaching for another model before that is what produced the refuted
one below.

**The rate is NOT what differs between a shaking boot and a steady one.** That
model is refuted by the survey below: all six boots matched the same source rate
to the milli-hertz and landed on the same display clock to within 88 Hz, and two
of them shook anyway.

## What the loop is

The output frame rate is steered so the read pointer crosses the write pointer
at `targetPhase` degrees into the input frame, which is 90 by default and wants
to be in vertical blanking.

    error       = phase - target                     ticks
    correction  = 0.0038 x error / ticksPerSecond x sourceRate     fraction
    outputRate  = sourceRate x (1 + correction)      clamped to +-0.0006

A rate error integrates into a phase error, so this is proportional feedback on
an integrator, sampled every `LockIntervalMs` -- 1670 ms. Its gain per step is

    0.0038 x LockIntervalMs x sourceRate = 0.0038 x 1.67 x 60.3 = 0.383

so each step removes 38% of the error and the next one is 0.617 of the last.
**That is stable and well damped**, and it is not where the fault is.

## What was measured

Bench unit on `vga`, RISC PC at 800x600@60, `SYNC 0`, separate sync, scaling
path, `PATTERN PM5544`. The lock armed with `/sc?W` and the console read for 95 s
per state. Shake is scored by `tools/gbsc-pro-hwtest/shake_survey.py`: the
standard deviation of a luma-gradient edge position across a six-second clip,
**with the horizontal axis as the control**, because a 30 fps camera against a
60 Hz panel beats and rolling shutter adds its own wobble. Both figures rising
together is the room; only the vertical rising is the unit.

### A boot that settles matches the arithmetic

Output rate against a source steady at 60316 mHz, one line per correction:

| step | output mHz | error mHz | ratio to previous |
|---|---|---|---|
| 1 | 60353 | +37 | |
| 2 | 60345 | +29 | 0.78 |
| 3 | 60334 | +18 | 0.62 |
| 4 | 60327 | +11 | 0.61 |
| 5 | 60323 | +7 | 0.64 |
| 6 | 60321 | +5 | 0.71 |
| 7 | 60319 | +3 | 0.60 |
| 8 | 60318 | +2 | |
| 9 | 60317 | +1 | |

Measured ratio 0.62 against the 0.617 the gain predicts. Once settled the clock
holds inside **232 Hz of 108023800 -- two parts per million** -- over the
remaining 70 s.

### A boot that shakes saturates instead

Same source, same build, a different boot:

| step | output mHz | clock | step size |
|---|---|---|---|
| 1 | 60343 -> 60307 | 108070560 -> 108005720 | at the clamp, down |
| 2 | 60307 -> 60286 | 108005720 -> 107968944 | down, past the source |
| 3 | 60286 -> 60322 | 107968944 -> 108033728 | at the clamp, up |

Sampled every 300 ms over 25 s the clock swings **+-65 kHz about 108.02 MHz**,
which is the +-0.06% clamp, reached on nearly every correction in alternating
directions. It does not decay over the three minutes the lock is left armed.

### The one-shot rate match is the same on both -- REFUTED as the cause

Six boots, lock armed, clipped 180 s after arming:

| boot | source mHz | output mHz | clock | vert sd | horiz sd | vert span |
|---|---|---|---|---|---|---|
| 1 | 60316 | 60316 | 108022960 | 0.006 | 0.005 | 0.05 |
| 2 | 60316 | 60316 | 108022960 | 0.005 | 0.006 | 0.04 |
| 3 | -- | -- | -- | 0.006 | 0.006 | 0.03 |
| 4 | -- | -- | -- | 0.005 | 0.006 | 0.03 |
| **5** | 60316 | 60316 | 108022872 | **0.274** | 0.017 | 1.22 |
| **6** | 60316 | 60316 | 108022872 | **0.328** | 0.012 | 1.25 |

The shaking boots and the steady ones start from the same matched rate and the
same clock. Sixteen to twenty-seven times the horizontal control is the unit,
not the camera. **So what differs is the phase, not the rate** -- which is what
`syncTargetPhase` exists to park, and what nothing was watching.

## Where the discontinuity is

`vsyncPeriodAndPhase()` reports a POSITION rather than a displacement:

    diff  = (outStart - inStart) % inPeriod
    phase = diff                              in [0, inPeriod)

Against a target at `0.25 x inPeriod` the raw difference therefore runs

    phase - target   in  [-0.25, +0.75) frames

which is not symmetric. A read pointer sitting a hundredth of a frame **below**
the target drifts down, crosses zero, and reappears at the top: the error jumps
from -0.01 to +0.74 of a frame between two samples 1.67 s apart. The loop
answers a three-quarter-frame error by slamming the clock to the clamp in the
opposite direction, sails past the target, and arrives back at the wrap from the
other side.

That is a real asymmetry and it predicted the boot dependence: a quarter of the
frame lies below the target, against two shaking boots in six. **The prediction
did not survive the bench.**

The error is read as the shorter of the two arcs now, in
`FrameSync::phaseError()`:

    error = phase - target
    if (error >  period / 2) error -= period
    if (error < -period / 2) error += period

The discontinuity is then half a frame from the target rather than a quarter,
and a pointer just below the target reads as just below it. `runVsync()` had the
same asymmetry in bang-bang form -- `phase > target` leaves the raster alone, so
a wrapped phase reads as far past the target and the raster is never stretched
-- and takes the same fix.

**The asymmetry was real and the fix stays. It is not what causes the shake.**

### The wrap is the cause -- REFUTED

Flashed and measured on the same source, the clock still saturates:

    108028880 -> 107964064 -> 108028840 -> 107964032 -> 108028808
    -> 108065896 -> 108001064 -> 107970632 -> 107991552 -> 108056352

Steps of -64816, +64776, -64808, +64776, +37088, -64832, -30432, +20920,
+64800. Many are exactly the clamp and the signs alternate, but the magnitudes
do not repeat -- **which is the reading that matters**, because a deterministic
limit cycle is regular and this is not. A correction driven by a NOISY phase,
saturating in whichever direction the noise points, fits the sequence better
than an oscillation does, and the two cannot be separated from the clock alone.

The wrap fix cannot be scored on shake counts either: the survey it was measured
against was stopped after one boot, because the live clock had already answered.

## Why this only became visible after the rate came from the engine

`runFrequency()` used to measure the source's field rate itself and refuse to
correct unless two readings agreed to `Clock::RateAgreement::RelativeTolerance`.
Those readings spread over a whole hertz on a source the engine holds to the
milli-hertz, so it refused about nine corrections in ten: **five corrections in
95 s against the fifty-seven the 1670 ms interval intends**. A loop running at a
fifteenth of its design rate makes a saturating correction look like slow drift
over the length of a clip, rather than like the swing it is.

So the cadence fix did not cause the saturation and did not cure it -- it made
it legible. The shake rate either side of that change is two boots in six
against one, which at six boots distinguishes nothing.

## What to do next

**Log the phase.** `phase`, the target and the unwrapped error, one set per
correction, beside the rates already on that line. It costs nothing, it is the
controlled variable, and it separates the two live candidates in one 95 second
capture:

| candidate | what the phase looks like |
|---|---|
| the measurement is noisy on some boots | jumps with no relation to the correction just made |
| the loop oscillates | moves smoothly, overshoots the target, comes back |

Only after that is a third model worth proposing.

## What is still open

- **Why it is per boot.** Four boots in six converge to two parts per million
  and hold; two saturate for as long as the lock is armed. Nothing in the rate
  or the clock distinguishes them at the start.
- **Whether the phase measurement is sound at all.** `vsyncPeriodAndPhase()`
  times an input edge, switches the test bus, and times an output edge some
  frames later, then takes the difference modulo the input period. The two
  samples are not simultaneous and the gap between them is not constant.
- **`targetPhase` is 90 degrees and nothing has measured that it is right.** The
  crossover shows as a stationary tear at `targetPhase/360` down the screen and
  belongs in vertical blanking; 90 degrees puts it a quarter of the way into
  live video. Only the picture can judge it, and `/framesync?phase=` sets it.
