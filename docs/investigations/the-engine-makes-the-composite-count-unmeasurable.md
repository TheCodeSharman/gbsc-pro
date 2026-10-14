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

## And no single rule fits both sources yet

`the-pulse-ignore-fraction-breaks-a-progressive-source.md` measured **70 bad on
the Wii** -- the ADC PLL locked 60% of the time with the picture shearing in
runs of frames -- against 14 good. So the two sources bracket from opposite
sides:

| | sync pulse | bad | good |
|---|---|---|---|
| Wii 480p, divider 1446 | 106 samples, 2.33 us | 70 (66%, 1.54 us), 107 (101%) | **14** (13%, 0.31 us) |
| RISC PC 800x600@60 composite, divider 2038 | 245 samples, 3.17 us | 14, 16, 50 (20%, 0.65 us) | **99** (40%, 1.28 us), 150 (61%) |

A fraction of the LINE gives the Wii 70, which is measured bad. A fraction of
the PULSE is refuted on that page already, and the pair here refutes it again
from the other side: 61% is good on the RISC PC where 66% is bad on the Wii, and
13% is good on the Wii where 20% is bad on the RISC PC.

**In absolute TIME the windows overlap.** The Wii is good at 0.31 us and bad at
1.54 us; the RISC PC is bad at 0.65 us and good at 1.28 us, so a fixed duration
of roughly 0.7 to 1.4 us satisfies both -- and a fixed duration is a fixed count
of the 27 MHz reference, which is neither of the normalisations tried. It is not
a fraction of anything, which is why both fractions failed.

**It is a hypothesis and not a rule**, because it lands in the untested gap on
both sources: 50..99 on the RISC PC and 14..70 on the Wii. Those two sweeps are
what would settle it, and a value chosen without them repeats the mistake that
put a Wii measurement on every progressive source.

The interlaced bound stays whatever happens: 576i needs 107 at divider 2200,
3.11 us, above its equalising pulse, which is outside the progressive overlap
entirely. One rule for progressive and one for serrated is the shape the
evidence supports.
