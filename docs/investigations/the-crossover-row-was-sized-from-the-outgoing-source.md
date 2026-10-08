# The crossover row was sized from the outgoing source

A selection installs the reference sampling clock so the arriving source can be
measured through a known clock. The pass that follows rebuilt the ADC PLL's
group from the rate a measurement was holding — which belongs to the source that
has just left — and the row it picked was for a CKO the arriving source never
produces.

`Adc::applySampleRate()` takes a divider and a line rate. The rate alone picks
`PLLAD_KS`, the VCO post divider read off RD-5725-1.1's crossover table, and
`PLLAD_FS`, the VCO gain. `VideoPath::inputTimingsChanged()` re-asserted the
divider in force and read the rate off `SourceMeasurement`, so the two halves of
one group came from two different sources.

## The measurement

Wii in 480i selected on `ypbpr`, predecessor the RISC PC at 800x600@60 on `vga`,
in a stall 330 s old:

| | in force | the arriving source wants |
|---|---|---|
| `PLLAD_MD` | 1400, the reference divider | 1400 |
| CKO the row was read against | 1400 x 37879 = 53.0 MHz | 1400 x 15734 = 22.0 MHz |
| `PLLAD_KS` | **1**, the 80..40 MHz row | **2**, the 40..20 MHz row |
| `/geometry` `lineRateHz` | 37879, throughout | 15584 |

`Tv5725::SamplingLog` at 25 ms across the whole of one failing leg, 1057 samples:

| | reading |
|---|---|
| `STATUS_MISC_PLLAD_LOCK` | **0 in 1057 of 1057** |
| `STATUS_SYNC_PROC_HTOTAL` within 2 of the divider | **0 of 1057** — 1422..1442 for ten seconds, then 1105..1250 |
| `STATUS_SYNC_PROC_VTOTAL` | 97..102, with excursions to 121 and 248 |
| `VPERIOD_IF` | **524 throughout**, a correct 525-line frame from 2 s on |

So the input formatter counted the source's vertical perfectly while the sync
processor, which counts in ADC clocks, had no clock to count. The field rate is
timed off `DEBUG_IN_PIN`, and in that state the pin carries nothing: timed 120
times from `loop()` with `/samplinglog?rates=120`, **98 of 120 readings fell in
16.1..16.5 Hz**, median 16.43, where 59.94 is due — the band `VideoSignal.h`
records for a pin the source is not driving. The remainder were 1144..8438 Hz.

A healthy state reads the other way: on `vga` settled, the same log over 1182
samples has `STATUS_SYNC_PROC_HTOTAL` within 2 of the divider in **1182 of
1182** — 1436 against 1436 — and the lock bit set in 1107 of them.

## What it costs

`VideoSignal`'s nets admit the noise. A count of 200..1300 is a source and a
field rate of 30..150 Hz is a source, so a 16 Hz distribution's tail and a
wandering counter between them produce readings the engine pays for and
occasionally accepts. One failing leg's readings, in order:

    265 lines x 15.36 Hz      342 lines x 23.65 Hz      264 lines x 16.98 Hz
    262 lines x 193.05 Hz     307 lines x 43.46 Hz -> 13387
    286 lines x 131.28 Hz -> 37680                  284 lines x 32.77 Hz -> 9342
    267 lines x 48.07 Hz -> 12883   <- accepted, and the leg dies here

A leg that acquires measures the same noise and is luckier: a rate low enough
clamps the divider to `SamplingClock::DoubledLineSampleLimit`, which is 2200 and
is what a 480i line wants, so a garbage rate installs the right group and the
real rate follows. 13 of 20 legs win that.

## Restoring the group recovers the stall, at the divider already in force

Measured twice, with `/sampleclock`, which writes the whole group through the
call the bypass switch makes and restarts the PLL afterwards:

| | stall age | to `state: acquired` |
|---|---|---|
| `md=2200&os=4` | 330 s | 5.4 s |
| `md=1400&os=4`, the divider already in force | 26 s | **1.9 s** |

**The second is the one that localises it.** Nothing about the divider changed,
so the stall is not a divider the PLL cannot lock to. Both landed on
`STATUS_SYNC_PROC_VTOTAL` 259/260, `STATUS_SYNC_PROC_HTOTAL` 2200 against a
2200 divider, and a measured 59.93 Hz.

**WHAT IT DOES NOT ISOLATE IS THE ROW.** `/sampleclock` writes the group through
`VideoPath::applySampling()`, which took the rate off the measurement, so the
recovery ran with the stale row still in force — and it is followed by
`restartAfterBypassSwitch()`, which resets the video blocks, the sync processor
and the memory bus, restarts the PLL and re-latches both phase adjusters. So
what these two runs establish is that **the stall is recoverable from the ESP in
about two seconds by resetting the blocks and restarting the PLL**, which no act
of the recovery ladder achieves in 330 s. That the row is wrong is arithmetic
and a register read; that the row is why the PLL does not lock is not shown
here, and the `vga` path argues against it being the whole story — a selection
there runs the same reference row against a 53 MHz CKO and acquires 20 of 20.

## What the firmware does now

`Adc` holds the rate the group in force was built from, beside the divider it
already held, and `Adc::rateInForce()` hands it back. `applySamplingClock()`
takes the rate as an argument, so each caller states which one it means: an
install from a measurement passes that measurement, and the two re-assert sites
— `inputTimingsChanged()` and `restartSamplingClock()` — pass the rate the group
they are re-asserting was built from. No site reads a measurement that belongs
to a source that is no longer there.

## What it bought

Twenty trips from an 800x600@60 `vga` predecessor, timed to `sync pad: driven`:

| | before | after |
|---|---|---|
| `ypbpr` shown a picture | 13 of 20 | **20 of 20** |
| of those, on the correct count | -- | **20 of 20**, `cv` 518/520/522 |
| inside 10 s | 2 of 20 | 4 of 20 |
| spread | 6.1..24.4 s, median 13.9 | 6.8..25.7 s, median 15.5 |
| `vga` | 20 of 20, median 6.1 s | 19 of 20, median 6.3 s |

**The reliability moved and the clock did not.** The seven legs that never
acquired are now slow successes, so they land in the tail and carry the median
up with them. Not one leg acquired on a wrong count, which is the failure the
entry in `known-issues.md` warns scores as a success on time alone.

The times cluster on the recovery act intervals, which is where the remaining
cost is: **4 legs on the selection alone** at 6.8..8.9 s, **15 after one
`Reconfigure` act** at 11.8..19.3 s, and one after two at 25.7 s. Three quarters
of legs wait out a ten-second timer for something `reconfigureForSource()` does
that a selection does not, and its calls are the list to bisect --
`reacquireSyncType()`, `applySyncProcessorDynamic()`,
`applyDefaultClampWindow()`, `holdClamp()`, `ModeDetect::nudge()`,
`reacquireSeparator()`, `restartSamplingClock()`.

## What this does NOT close

**A garbage rate is still accepted, and it reaches the same stall by a second
route.** `SourceMeasurement::rateSettled()` returns true once
`RateAgreementAttempts` readings have been taken whether any two of them agree
or not, and at `RateAgreementPerThousand` of 1 two noise readings never agree —
so on a source whose pin carries nothing the attempt cap is always what accepts,
and what it accepts is an arbitrary sample of the noise. Measured on the boot
after this fix landed: `ypbpr`, `PLLAD_MD` 1940 and `PLLAD_KS` 0 against
`/geometry` `lineRateHz` 41808, which is a group built for 81 MHz on a source
producing 22 MHz. `/sc?~` cleared it in 15 s.

**The corroboration is on the chip and nothing asks for it.** `VPERIOD_IF`
holds 524 — a correct 525-line frame — through every stall measured here, beside
a sync processor counting 97..105. A count the input formatter's own frame
contradicts by a factor of five is not a count, and refusing it would close both
routes at once: nothing would pay for a field rate, so no noise reading could be
accepted.

**`reconciledFrame()` IS NOT THAT GATE, and this is the trap.** It would refuse
the healthy state too. It tries frame totals of `VPERIOD_IF + 1` divided by 1
and by 2 and takes the one landing within `VerticalSyncMaxLines` of the count;
on 480i the total is 525, which is ODD, so the factor-2 branch is skipped
outright and the factor-1 difference is 525 - 261 = 264 against a bound of 8. It
returns 0 on an acquired Wii exactly as it does in the stall, so
`holdVerticalSync()` restores nothing there and `verticalSyncLines_` stays 0.
A gate needs a comparison that admits a half frame — the count is about half the
frame on an interlaced source and about the whole of it on a progressive one —
and it has to be reached only where the input formatter completed a vertical
measurement at all: `VPERIOD_IF` reads 31..66 on a perfectly healthy `vga`
source, which is separate sync, and `STATUS_IF_VT_OK` is what reports that.

## Refuted, and recorded on the page that says it

`the-ladder-never-restarts-the-adc-pll.md` states that on `ypbpr` *"the ADC PLL
locks at the reference divider inside two seconds"*. It does not, on this build:
1057 samples across a failing leg read the lock bit 0 every time and
`STATUS_SYNC_PROC_HTOTAL` never came within 2 of the divider. The row written
after the reference install is what the entry above describes.
