# One sample of the field rate is wrong by percent, about one in ten

The source's field rate is timed off the debug pin: one vsync period, measured
between two edge timestamps taken from the CPU's cycle counter. The period
itself resolves to about one cycle in three million, and `sampleVsyncPeriod()`
goes to some trouble to keep it there -- `yield()` before the measurement rather
than inside it, WiFi in light sleep, and the `delay(7)` parked in the ~20 ms of
slack between the two edges, well away from the one about to be timed.

**The resolution is not the problem. A single pulse timed on a CPU that takes
interrupts is.** On a settled 320x256@50 source `getSourceFieldRate()` returns
**51.14 among 50.08s, about one sample in ten**. That is 2.1% high, far outside
anything the resolution accounts for, and no bounds check can see it: it sits
comfortably inside the 47..86 Hz band a field rate is accepted in.

There is one measurement, not two. `TestBusRateMeasurement::sourceFieldRateHz()`
and `FrameSync::getPulseTicks()` both call `sampleVsyncPeriod()`, so nothing is
gained by preferring one reader's number to the other's. What differs is what
each does with it.

## Where it reaches

| reader | what one bad sample does | what guards it |
|---|---|---|
| the clock generator's seed | the output runs a hertz out until the frame time lock walks it back -- tens of seconds of dropped frames and an encoder locked to the wrong rate | `Clock::RateAgreement::agree()` over two samples, 0.83% relative and 0.5 Hz absolute, both binding |
| `SourceKey`, and the raster generated from it | the raster is sized for the wrong rate for the LIFE of the source | the median of three, on the one pass that acts on a single reading |

The key is the expensive one, because it does not recover. It carries a whole
number of hertz and `VideoPath::solveRaster()` reads that rather than the
measurement, so a 51.14 sample keys **51** and the raster solves **1882** where
1920 is due. Identity is deliberately wider than the rounding --
`RateTolerancePerMille` is 50, i.e. ±2.5 Hz at 50 Hz -- so 50 and 51 compare
EQUAL, `adoptSourceKey()` never replaces the key, and every later correct
reading is absorbed.

**The divider is right while the raster is wrong**, which is what makes this
hard to see from the registers: `solveSampling()` reads the last GOOD line rate
and the key reads the last RAW field rate, so `PLLAD_MD` and
`STATUS_SYNC_PROC_HTOTAL` agree with each other and with the source while the
output raster is 2% short.

## Why the pass that acts on one reading exists

`rateSettled()` reports settled once two readings agree, and otherwise once
`RateAgreementAttempts` is spent. The second branch is deliberate: the capture
stays frozen while a mode change is outstanding, so a source whose period
genuinely wanders would sit on a frozen picture for ever.

**That branch always promotes a reading that disagreed**, by construction --
agreement would have returned on the first branch. So it is the one place a
single sample decides the key, and it takes the median of three instead. Two of
the three have to be wrong before the median is.

## What is not covered

Two consecutive readings that are both outliers AND agree with each other
within `RateAgreementPerMille` settle on the first branch and reach the key
unmedianed. Nothing measures how often that happens.

A bucket boundary is a separate hazard and is not this one: the rounding is to
nearest, and of the 63 modes the bench monitor definition carries, three come
within 0.15 Hz of a half hertz -- 54.4833, 69.5398 and 71.4286 -- none of them a
mode this bench runs.

## Measuring it

Ten source mode round trips reading `VDS_HSYNC_RST` after each is **not** a test
of this. At a one-in-fourteen base rate a clean run of ten is unremarkable, and
one was measured before the median went in. The console is the instrument: every
reading is printed as it is taken, so the outliers are visible directly.

```
sampling: 311 lines x 51.25 Hz -> line rate 15992
sampling: 311 lines x 53.78 Hz -> line rate 16781
sampling: 311 lines x 51.91 Hz -> line rate 16196
sampling: 311 lines x 50.08 Hz -> line rate 15625
```

Those four are consecutive, on one settled source, inside one second. Note that
the outliers are **accepted** -- a nonzero line rate -- because
`rateFollowsCount()` allows 5% against the held rate and 51.25 is 2.3% off.
Acceptance is not agreement, and neither is stability.
