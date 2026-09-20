# One rate tolerance answered five questions, and the field rate does not wander

`VideoSignal::RateTolerancePerMille` was a single constant, 50, compared
against by five sites. Its own comment sized it as a gross-error net: *the
bench transient is 15.6% out and a settled source drifts by tenths of one, so
anything between separates them.* Both of those numbers are from a measurement
path that has since been replaced, and the lower one has collapsed to zero.

## What the field rate actually does

`SamplingLog::rates()` times the source's field rate from inside `loop()`, one
reading per pass, and logs the line count beside it. An HTTP poll cannot answer
this: every request is deferred to `loop()` and answers at tens of hertz, so it
samples the loop rather than the rate.

```sh
curl 'http://<ip>/samplinglog?rates=250'
```

250 samples a mode, on a settled source:

| source | readings | field rate | spread |
|---|---|---|---|
| RISC PC 320x256@50, 311 lines | 250 | 50.081 Hz, every one | **0.000%** |
| RISC PC 800x600@60, 627 lines | 250 | 60.317 Hz, every one | **0.000%** |

60.317 is DMT's figure for 800x600@60 exactly. The instrument is one vsync
period timed between two rising-edge ISRs reading `ccount`, with no averaging —
both edges take the same path, so the interrupt latency largely cancels.

**The errors are bimodal, and that is the finding.** A 900-sample run spanning a
source mode change:

| population | n | what it reads |
|---|---|---|
| settled, count agreed | 111 | 60.317 Hz, 0.000% spread |
| count unsettled, mid-change | 718 | 50.03 to 50.69 Hz, a 1.3% band |
| gross | 54 | 99 Hz to 15901 Hz — 2x to 300x |
| timed out | 17 | 0 Hz |

A reading is either exact or orders of magnitude out. There is no 15.6%
population and no 4.2% one: the 60.0-against-62.5 that `test_source_key.cpp`
recorded as one source never appeared once in 750 samples. The ~1.3% band exists
only while the LINE COUNT is unsettled, and `rateFollowsCount()` requires
`lines == heldLines` before it compares rates at all, so those readings never
reach a tolerance.

**A settled reading is repeatable, not merely close.** `RateAgreementPerMille`
is 1 — two successive readings must agree to 0.1% for `rateSettled()` to take
the fast path — and it has always passed, which corroborates the above
independently of this measurement.

## The five questions

The sites were never asking the same thing, and the spreads differ by orders of
magnitude:

| question | site | what sizes it |
|---|---|---|
| does the line rate follow the held count? | `SourceMeasurement::rateFollowsCount()` | 0.000% to accept against 15.6% and up to reject |
| has the line period moved? | `SourceMeasurement::hasLineRateMoved()` | a change detector in its own units, compared only against its own earlier value |
| does the corroborating measurement agree? | `VideoSourceAcquisition::corroborate` | the instrument's own spread, 0.000% |
| are these two readings one source? | `SourceKey::operator==` | long-term drift, which is not measured here |
| is this the standard it claims? | `SourceTiming::matching()` | not measurement at all — how far a real source sits from the rate it names, 0.16% on the bench RISC PC |

`ratesAgree()` now takes the tolerance and has no default, so each site names its
own and none can inherit another's.

**A sixth site was not asking about a rate.** `VideoPath::installSampling()`
compared two DIVIDERS through it. A divider is a derived integer carrying no
measurement noise, and the only spread it has is quantisation — a field rate
wobbling across a step chose 2506 and 2508, which is 0.08%. Applying a net sized
for a 15.6% transient forgave 480p's divider against 576p's, which is a
deliberate 4% apart, and the two SD modes shared whichever clock was arrived
from. `DividerJitterPerMille` is 2.

**The values did not move.** Tightening the rate ones costs genuine movement: a
source may change rate at a constant count — a RISC PC does, and that is the
case the mode-change latch exists for — and every such change inside the
tolerance is refused until `HeldRateRejectionLimit` lets it out. The gap between
0.000% and 15.6% is wide, and the middle is the right place to sit. What was
wrong was one number standing for five questions, not the number.

## The grid was the dominant error, and it overshot the clock ceiling

`SourceKey` rounded the rate to a whole hertz, and that rounded value is what
`OutputMode::solve()` generates the output raster and the display clock seed
from. The justification written above the call was the wander this measurement
refutes: *one unchanged 800x600 source settles at 60.38 Hz after one mode change
and 60.72 after the next.*

`horizontalTotalFor()` is `floor(clock / rate / lines)`, so a rate rounded DOWN
makes the total too LARGE:

| assumed rate | total at 1125 lines | the clock the line then runs at |
|---|---|---|
| 50.000 (whole hertz) | 1920 | **108.17 MHz, against a 108 MHz `EngineCeilingHz`** |
| 50.080 (hundredths) | 1916 | 107.95 MHz |

So the grid did not only cost 0.53% of raster width on a 60 Hz source — it put
the display clock above the ceiling the divider had just been chosen for, on
every source whose rate is not a whole number, which is every source.
`horizontalTotalFor()`'s own comment says it takes a measured rather than a
nominal rate *because truncating it to 50 moves the answer by a pixel*; the key
was handing it the truncation.

**Quantised at all so the raster is solved once per source**, rather than
re-solved on a reading that moved in its last digit. That is the property worth
keeping, and it never rested on the rounding: `SourceKey::operator==` compares
with a tolerance, so identity survives a reading that moves. How fine the grid
may be is a property of the instrument, and the instrument is repeatable to
better than 0.002%. `RateStepsPerHz` is 100.

Measured on the bench across the change, one unchanged source, card clean and
complete at every landing:

| | before | after |
|---|---|---|
| 1080p `VDS_HSYNC_RST` | 1919 | **1915** |
| 480p `PLLAD_MD` | 1880 | **1876** |
| 576p `PLLAD_MD` | 1954 | **1952** |

**The stored framing is not affected and could not be.** A framing is a
proportion of the source line, expanded against the live measurement, so no
change to the raster reaches it. What the grid reached was everything measured
in output pixels AGAINST that raster — the raster totals, the clock seed, the
zoom floor at `raster / maxMagnification`, and the capture bound
`dividerCeilingForOutput()` derives from the raster it solves. An 11 px
systematic on a 1920 px line is 200x the 0.05 px jitter floor the framing
measurements work to.

**The persisted framing line still carries whole hertz.** `FramingLine::write()`
serialises `lrintf(key.rateHz())` and the parser reads an integer. A key read
back sits inside `SourceIdentityPerMille` of the live one, so lookups match, and
the file's rate reaches no raster — `framedKey_` is built from the measurement
on every solve. Tightening `SourceIdentityPerMille` below about 1% would break
that, and the file format would have to carry the fraction first.

## What is not measured

**Long-term drift.** Every run here spans seconds to about a minute, on a unit
that had been powered for hours. Whether a source's rate walks thermally over
tens of minutes, or differs across a cold start, is unmeasured — which is why
`SourceIdentityPerMille` stays at 50 rather than following the instrument down.

**Only two sources, both RGBHV from one machine.** The Wii on `ypbpr` is the
bench's only sync-on-green and only interlaced source and has not been sampled
this way. An interlaced field count alternates, so `rates` would be read against
a count that is itself moving.
