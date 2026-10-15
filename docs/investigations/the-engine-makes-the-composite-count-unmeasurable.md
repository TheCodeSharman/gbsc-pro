# The composite count is scattered by the engine, not by the source

Frozen, the composite-sync source measures its own line count in **99.0% of
1198 samples**. With the engine running, the same source at the same separator
level measures it in **78.9% of 598**, across 43 distinct values from 0 to 1890.
The scatter the whole composite fault rests on is substantially the engine's own
doing, and the source is far more measurable than every reading before this
said.

Measured on `vga`, RISC PC at 800x600@60 with `SYNC 1`, output 1920x1080,
`scale-rgbhv = 0` so pass-through is permitted, on a `GBS_SAMPLING_LOG=1` build.

## The count, frozen against running

`/samplinglog?ms=25&for=30000` samples from inside `loop()` and reads the whole
set in one pass, which is what makes two registers comparable to each other. The
frozen window was taken by catching `acquired` on `/geometry` and freezing on
that poll, so the state held is a solved one.

**The separator level is the confound** -- the recovery ladder walks
`ADC_SOGCTRL` and the freeze stops the ladder -- so both windows print it at
each end and the comparison stands only because it matched.

| | engine running | **frozen** |
|---|---|---|
| samples | 598 | 1198 |
| `ADC_SOGCTRL` | 12 | 12 |
| coast, `SP_H_PULSE_IGNOR` at the start | 7/6, 99 | 7/6, 99 |
| distinct `STATUS_SYNC_PROC_VTOTAL` | **43** | **9** |
| range | 0 .. 1890 | 171 .. 624 |
| within 620..630 | 472 of 598 = **78.9%** | 1186 of 1198 = **99.0%** |
| `STATUS_MISC_PLLAD_LOCK` high | 70.2% | 78.4% |
| the register set at the end | **torn down**: ignore 16, `PLLAD_MD` 2040, `HTOTAL` 34 | held exactly |

The last row is the mechanism in one line. The engine's arm rewrites the sync
processor into the **search** configuration -- pulse ignore 16, the reference
divider -- which is the configuration for a source nothing has measured yet, and
through it the next reading cannot be the source's. So a scattered count arms a
re-solve, the arm installs a configuration that guarantees the next count is
also scattered, and the loop closes.

**It does not say the source is perfect.** 12 of 1198 frozen samples sit outside
620..630 and one reads 171, so the separator does drop a pulse. The claim is
twentyfold, not absolute: 1% against 21%, where four consecutive agreeing
samples are what arms a change.

**A frozen comparison IS available on this source**, which is what makes the
measurement possible at all. `known-issues.md` records freezing taking
`STATUS_SYNC_PROC_VTOTAL` to 97..135 with both test buses timing nothing -- that
is the Wii on `ypbpr`, and the RISC PC on `vga` composite holds 622/623 instead.

## Refuted: the vertical edge does not arrive every field

The field rate reads **36.99 Hz against a true 60.31** whenever the engine takes
it through the latched clock, and the reason is not a missing pulse.

`/testbus` counts transitions over a window from inside `loop()`, which is an
instrument independent of the edge ISRs `measureRateHz()` times. Row 0 of an
`if=3` sweep is exactly the pin the rate measurement reads --
`TestBus::selectInputVsync()` writes `TEST_BUS_SEL` 0 and `IF_TEST_SEL` 3 --
over a 500 ms window:

| `PLLAD_MD` | `STATUS_SYNC_PROC_HTOTAL` | transitions | pulses/s |
|---|---|---|---|
| 1436 | 1436, locked | 62 | 62.0 |
| 1436 | 1436, locked | 58 | 58.0 |
| 2038 | 2038, locked | 62 | 62.0 |
| 2038 | 2038, locked | 56 | 56.0 |

**The pin carries the source's field rate at the latched clock.** So the
conclusion that "the sync processor sees the source while the edge at
`DEBUG_IN_PIN` does not arrive every field" does not hold, and neither does the
candidate it named -- the composite post coast deciding whether the vertical
blanking reaches the pin. The coast was 7/6 in every reading above and in every
reading below.

## Refuted: it is the latched clock that reads the rate wrong

`/samplinglog?rates=` times the field rate through exactly the call the engine
makes, `TestBusRateMeasurement::sourceFieldRateHz()`, but from `loop()` -- so it
answers with automation FROZEN, where nothing rewrites a register between
readings.

| state | `PLLAD_MD` | lock | median | commonest | 36.99 Hz seen |
|---|---|---|---|---|---|
| unfrozen, mid-solve | 2040 | unlocked | 60.32 | 60.32 x26 of 50 | no |
| **frozen, solved clock** | 2038 | **locked** | **60.32** | **60.32 x41 of 50** | **no** |
| frozen, reference clock | 2040 | locked | 60.32 | 60.32 x43 of 50 | no |

**Frozen at the clock the solve installed, the rate reads correctly.** The
latched clock is therefore not the variable, and the console's perfect split --
30 of 30 readings at the unlatched reference clock reading 60.31, 16 of 16 at
the latched clock reading 36.99 -- is a correlation with where in the solve
sequence the reading falls, not with the clock it is taken through.

**The level sweep's reading that the field rate cannot be read at a reference
clock does not survive either.** It reads 60.32 there, 43 of 50 frozen and 30 of
30 in the engine's own measurements. That earlier reading was taken with the
separator level being walked, so the separator rather than the clock was moving.

## Refuted: the one-pulse span carries a fixed delay

`sampleFieldRateHz()` spans **one** pulse on both of its paths --
`recurringFieldRateHz()` takes nine single-pulse readings and `medianOfThree()`
three -- where `/samplinglog?rates=` defaults to **eight**, and
`debugPinSpanEdges()` runs `delay(7)` and enables `WIFI_LIGHT_SLEEP` after the
first edge of a span. A fixed 10.5 ms added to this source's 16.58 ms field
period would read as exactly 36.99 Hz, and the header states the division
outright: one pulse carries the whole of both edge ISRs' latency, and N divides
it by N.

**It does not.** Frozen on one solved state, every span reads the source:

| pulses | readings | median | commonest |
|---|---|---|---|
| 1 | 40 | 60.34 | 60.32 x20, 60.35 x20 |
| 8 | 40 | 60.32 | 60.32 x31 |
| 1 | 40 | 60.31 | 60.29 x16, 60.31 x15 |
| 8 | 40 | 60.32 | 60.32 x36 |
| 2 | 40 | 60.32 | 60.33 x10, 60.32 x9, 60.31 x8 |
| 4 | 40 | 60.32 | 60.32 x31 |

The register set was read at both ends and held exactly. So `delay(7)` costs
nothing measurable here, and 36.99 Hz appears at no span.

## Refuted: the sync pad being away

Every wrong reading is preceded by `sync pad: away`, four lines above it, which
is the arm withdrawing `PAD_SYNC_OUT_ENZ` before the setup. **The pad is away
for the right readings too**, inside the same sequence:

```
13.06  measure: start, TIMINGS change (rate), 626 lines, solved 627
13.06  sync pad: away
13.27  settle: spent after 138 ms
13.78  measured: 622 lines x 36.99 Hz ... clock 2038@37879 htotal 2038
14.05  settle: spent after 113 ms
14.50  measured: 623 lines x 60.31 Hz ... clock 2040@15625 htotal 989 UNLATCHED
14.63  installing: divider from line rate 37638, doubled 0 -> 1436
15.52  sync pad: driven
```

So the pad is not the variable, and the sequence shows what is: **the first
reading of a measure pass is taken through the divider the PREVIOUS solve left
in force**, and the readings after the reference clock is installed are correct.

## Refuted: the separator level

Every wrong reading in the console was taken at `printInfo`'s `S: 6`, the level
the recovery ladder had walked down to, and every frozen reading that read right
was at 12. Frozen, with the level held from outside -- which is the only state
it is judgeable in, the ladder otherwise rewriting it every 500 ms -- the rate
reads 36.99 Hz at **every** level:

| level | rate, 30 readings | count median |
|---|---|---|
| 12 | 36.99 in 24 | 1430 |
| 10 | 36.99 in 30 | 1183 |
| 8 | 36.99 in 29 | 898 |
| 6 | 36.99 in 25 | 729 |
| 4 | 36.99 in 29 | 119 |
| 12 | 36.99 in 29 | 171 |

## It is SP_H_PULSE_IGNOR, and both measurements turn on it together

The two frozen runs that disagreed differed in exactly one register. Frozen at a
solved composite state, `PLLAD_MD` 2038 latched against
`STATUS_SYNC_PROC_HTOTAL` 2038, separator level 9, coast 7/6, with nothing else
written:

| `SP_H_PULSE_IGNOR` | field rate | inside 59..61.5 Hz | count median | inside 615..635 |
|---|---|---|---|---|
| **99** | 60.32 | **25 of 25** | **622** | **25 of 25** |
| 16 | 36.99 | 0 of 25 | 1044 | 0 of 25 |
| 14 | 36.99 | 0 of 25 | 1430 | 0 of 25 |
| **99** | 60.31 | **25 of 25** | **622** | **25 of 25** |
| 50 | 36.99 | 0 of 25 | 1636 | 0 of 25 |
| **150** | 60.31 | **25 of 25** | **622** | **25 of 25** |
| **99** | 60.34 | 22 of 25 | 622 | 23 of 25 |

**One register decides whether the source can be measured at all**, both halves
of the measurement turning together, reversibly, four times.

`pulseIgnoreFor()` returns `ProgressiveCsyncPulseIgnore`, a constant **14**, for
any progressive composite source. It was measured on the Wii at 480p through a
**1446-sample** line and it is applied to the RISC PC through a **2038-sample**
one, where the source becomes unmeasurable -- the pattern `known-issues.md`
already records for this register, a byte that reads one divider ignoring the
source at another.

## The threshold reads through one wide plateau, not a scattered pair

The space is a single contiguous band whose floor is the same at every separator
level and whose ceiling is the source's own sync pulse. Measured on `vga`, the
RISC PC at 800x600@60 with `SYNC 1`, frozen with `PLLAD_MD` 2038 confirmed
latched against `STATUS_SYNC_PROC_HTOTAL` **after** the freeze, ten on-device
readings a threshold:

| level | never counts the source | counts it | collapses from |
|---|---|---|---|
| 9 | 1..84 | **86..236** | 245 |
| 12 | 1..84 | **86..110** (range walked) | -- |
| 12, descending from 130 | 1..85 | **86..130** | -- |

**151 of 255 thresholds read the source, and 86 is the floor in every walk** --
ascending, descending, and at both levels. The ceiling is where the threshold
reaches the source's own pulse, which `STATUS_SYNC_PROC_HLOW_LEN` measures at
**246 samples** on this source at this divider.

**The level is not the other half of a pair.** At threshold 99, the count reads
622 at 60.33 Hz at every level from 2 to 31; 0 and 1 are the starved end where
the separator slices almost nothing.

### The protocol decides whether the floor is visible

A walk that freezes on request lands on the reference clock about as often as
the solved one, and through a 982-sample line the same byte is a different
fraction. That is the whole of why the space reads as scattered: readings taken
at two different clocks are not readings of one surface.

**And a value poked after the lock is a different experiment from the value the
block locked on**, which is the only one the firmware performs. Re-issuing the
source's mode with automation frozen makes the separator re-lock with the
threshold in force, the divider unmoved. Four interleaved controls at 99:

| threshold | 2 | 14 | 50 | 70 | 86 | 99 (x4) |
|---|---|---|---|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 1226 | 213, 554 | 554 | 426 | **622** | **622** |
| field rate | 36.99 | 36.99 | 36.99 | 36.99 | **60.32** | **60.32** |

The floor holds under both protocols, and **36.99 Hz is the engine's own wrong
rate on this source** -- so the threshold the engine writes is what produces it.

## The Wii's bound was the old divider, and it is gone

`the-pulse-ignore-fraction-breaks-a-progressive-source.md` measured 70 marginal
and 107 black on the Wii at 480p, and sized `ProgressiveCsyncPulseIgnore` from
it. Those readings were taken through a **1446-sample** line, where the source's
pulse is 106 samples -- 70 is 66% of it and 107 is above it altogether.

The engine installs **2038** for that source now, where its pulse measures 148
samples. Frozen, one acquisition, 40 readings a threshold at step 1 from 10 to
146, with the emitted frame read off the USB dongle and a control at 14 between
treatments:

| | |
|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | **524 in every reading at every threshold** |
| field rate | 59.94 throughout |
| emitted mean luma | **194..196 throughout**, control steady |
| measured sync pulse | 148 samples, falling to 98 at 146 |

**So the source is indifferent across the whole range the RISC PC's plateau
lives in**, and nothing in it brackets the plateau from above.

**THE COUNT CANNOT SEE THAT SOURCE FAIL.** Its documented failure is a steady
524 beside an emitted frame at mean luma 0.00, so a predicate built on the count
accepts a black screen -- which is what a search over this space would have been
scored on.

## What the two sources together bound

| source | divider | pulse | bad | counts it |
|---|---|---|---|---|
| RISC PC 800x600@60 csync | 2038 | 246 | 2, 14, 50, 70 | **86..236** |
| RISC PC 800x600@60 csync | 1438 | ~174 | -- | 2 and 107 alike |
| Wii 480p SOG | 2038 | 148 | -- | 10..146 |
| Wii 480p SOG | 1446 | 106 | 70, 107 | 14 |
| Wii 576i SOG | 2200 | 158 | 2, 6, 8, 14, 51, 144, 255 | **107** |

The fraction of the line the engine already holds -- 49 per thousand -- is 99 at
2038 and lands inside both live sources' bands, and 107 at 2200 which is the
measured value for 576i. **The floor is a property of the divider rather than of
the level or the sync arrangement**: it is absent at 1438 and 86 at 2038 on one
source and one mode.

**What is not settled is the normalisation.** A floor absent at one divider and
present at another fits neither a fixed duration nor a fixed fraction of the
line nor a fixed fraction of the pulse, and three rows above are at dividers the
engine no longer installs. The measurement that would close it is the serrated
source's own floor, which lies between 51 and 107 at divider 2200 and needs the
Wii set to 576i.
