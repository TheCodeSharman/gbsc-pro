# The cheap field rate is earned by the probes, not granted by a confirmation

`Tv5725::SourceMeasurement::sampleFieldRateHz()` has two estimators and one of
them is wrong for a source whose sync separator invents and drops edges. Which
one is in force used to be decided by the engine's confirmation protocol, which
is asking a different question, and on the Wii at 480p that cost a fully acquired
source for the life of the boot.

## The two estimators

- **The probes.** `recurringFieldRateHz()` times the source over a single pulse
  `RecurringRateSamples` times and asks which value RECURS, then takes one
  spanned reading and keeps it only if it lands in the cluster the probes found.
  Ten timings.
- **The median.** Three single-pulse timings, middle one wins. Three timings.

The median is right for a source whose timings differ only by the instrument's
own spread. It is wrong for one whose timings land on different multiples of the
frame period, because a median rejects an outlier and not a spread: measured on a
component source at 480p, the true 59.939 Hz is in 32 of 228 single readings and
a median of three returns it in about 5% of trials. Every wrong value is a
different rational multiple of the truth -- 120 distinct ratios across those 228,
appearing once or twice each -- so the truth RECURS to the digit where the errors
scatter, which is what makes the recurring value the estimator and the median the
wrong one.

**Spanning does not rescue it.** A spanned reading needs every period in the span
clean, and only 14% of eight-pulse readings are right on that source. So the
probes are single-pulse, and a spanned reading is kept only where it lands in the
cluster they found.

**0 rather than a best guess**, because sizing a divider from an uncorroborated
rate is the harm: a rate that cannot be measured is reported as one.

## What used to select it

`rateConfirmed_`, set by `VideoSourceAcquisition`'s rate recheck. That flag
answers *"has the boot's one re-measurement been taken?"* -- it widens the
corroboration tolerance back to every other reading's and stops a drifting source
re-solving every `RateRecheckPasses`. It was set unconditionally, before the
reading it took was judged, so a confirmation that could not be judged or that
refused the held rate still spent it. And nothing cleared it: the only writer was
the constructor.

So the sequence on the Wii at 480p, measured from `/bootlog` across three boots:

```
measured: 524 lines x 59.93 Hz -> line rate 31468      <- the probes, correct
installing: divider from line rate 31468, doubled 0 -> 1446
```

at which point `PLLAD_MD` 1446 is latched against `STATUS_SYNC_PROC_HTOTAL` 1446
with `STATUS_SYNC_PROC_VTOTAL` 524 and `STATUS_MISC_PLLAD_LOCK` 1 -- the source
fully acquired. Then the recheck spends the flag, the median comes into force,
and every reading after it is a multiple:

```
sampling: 524 lines x 40.07 Hz -> line rate 0          <- 59.93 x 2/3
sampling: 524 lines x 39.93 Hz -> line rate 0
sampling: 524 lines x  0.00 Hz -> line rate 0
```

`rateFollowsCount()` refuses each one against the held rate, the pass answers
`Unmeasurable`, and `tryNextReferenceClock()` replaces the latched divider with a
reference clock that cannot measure this source. From there the count reads 97 at
every detection entry and the engine cycles 2040/1400 for ever.

**No recovery short of a boot gets back**, measured in one window on the
collapsed state: `/sc?~`, `/input?src=ypbpr` and `/uc?h` each leave the count at
97..100, and a `/restart` reads 524 again on the next pass. The construction of
`SourceMeasurement` is the only thing that clears the flag, which is exactly why.

## What selects it now

Whether the probes AGREED, observed by the run that took them and held in
`SourceMeasurement`. `recurringFieldRateHz()` reports unanimity through
`everyProbeAgreed`, and `probesAgreed_` is set only when every probe of the
arriving source landed in one cluster. `modeChanged()` clears it, because which
source is arriving is when the separator's behaviour may differ.

This is a property of the source, measured, rather than a conclusion about the
engine's own protocol. A clean source earns the cheap instrument from its first
reading; one with three of nine agreeing never does, however much the engine
later agrees about what the rate IS.

**A rate being right does not make a single timing of it right**, which is the
conflation that made the old selection look reasonable. The corroboration asks
whether the held rate still describes the source. The estimator needs to know
whether one timing can be told apart from a multiple of itself. Nothing the first
question answers bears on the second.

## What it costs

Less, not more. A mode change on the bench RISC PC took three field-rate
readings; it takes two, and only the first runs the probes. `test_video_path.cpp`
states the arithmetic as `TimingsPerFirstReading + TimingsPerAgreedReading`.

## Refuted on the way

Four candidates for the same symptom, each tested on the failing state rather
than on a comparable one, and none of them should be reached for again.

| refuted | by |
|---|---|
| the ADC PLL's VCO is over `MaxSampleRateHz` at the reference | frozen, the divider swept 640..2040 through the engine's own owner: `STATUS_SYNC_PROC_HTOTAL` holds 427 at every step with `PLLAD_KS` 2, never within 2 of the divider, lock 0 throughout -- and the dividers whose real VCO lands at 161 MHz (1280, 1287, 640) lock no better than 2040's 257 MHz |
| the sync separator's level | `/samplinglog?sog=0&to=31` walks all 32 levels: `sp_vtotal` 97 and `field_rate_mhz` 0 at every one |
| `SyncProcessor::init()` did not run on an input change | all 34 static fields it writes hold their init values in the failing state, read by name |
| an oversampling A/B at the reference | `/sampleclock` passes `sampling_.lineRateHz()` to `applySampleRate()`, and on an unmeasured source that is 0 or the OUTGOING source's rate -- so the `os=` argument never reaches `applyOversample()` and two runs at oversample 2 and 4 are the same test twice |

## What is still open

The scan oscillation and the stall, both in `known-issues.md`. The fix here
retires the field rate as the explanation: it reads a steady 59.93 Hz through the
solved divider now.
