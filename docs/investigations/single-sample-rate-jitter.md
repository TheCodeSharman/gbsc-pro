# One sample of the field rate is one source line out, about one in eighty

The source's field rate is timed off the debug pin: one vsync period, measured
between two edge timestamps taken from the CPU's cycle counter. The period
itself resolves to about one cycle in three million, and `sampleVsyncPeriod()`
goes to some trouble to keep it there -- `yield()` before the measurement rather
than inside it, WiFi in light sleep, and the `delay(7)` parked in the ~20 ms of
slack between the two edges, well away from the one about to be timed.

**The resolution is not the problem. A single pulse timed on a CPU that takes
interrupts is.**

## The distribution

1000 consecutive raw samples through `/samplinglog?rates=1000`, on a settled
800x600@60 RGBHV source on separate sync, with `STATUS_SYNC_PROC_VTOTAL` 627 in
1000 of 1000:

| reading | samples | what it is |
|---|---|---|
| 60.222 Hz | 9 | the period one line LONG |
| 60.317 Hz | 988 | the source |
| 60.414 Hz | 3 | the period one line SHORT |

**The excursions are exactly one source line**, not a smear: `627/626` of 60.317
is 60.4134 and `627/628` is 60.2210, each within about a millihertz of what was
read. A period that took in a line too many or too few is what that shape means,
rather than a timestamp displaced by interrupt latency -- and **1.2% of samples
are one of the two.**

A larger excursion is on record and did not recur in this window: 51.14 among
50.08s at 311 lines is 2.1%, six and a half lines' worth.

## What one line costs

One line is `rate / lines` -- 0.096 Hz at 800x600@60, 0.161 Hz at 320x256@50.
The key carries a whole number of hertz, so at both of those the key is
unchanged and the sample is harmless.

**It is a mode sitting near a half hertz that pays.** Of the 63 modes the bench
monitor definition carries, three come within 0.15 Hz of one -- 54.4833, 69.5398
and 71.4286 -- and one line at any of them is wider than that, so the whole
hertz flips on which sample happened to be taken. `VideoPath::solveRaster()`
reads the key rather than the measurement, so the raster is then sized for the
wrong rate for the LIFE of the source: identity is deliberately wider than the
rounding -- `RateTolerancePerMille` is 50 -- so the two compare EQUAL,
`adoptSourceKey()` never replaces the key, and every later correct reading is
absorbed.

**The divider is right while the raster is wrong**, which is what makes this
hard to see from the registers: `solveSampling()` reads the last GOOD line rate
and the key reads the field rate, so `PLLAD_MD` and `STATUS_SYNC_PROC_HTOTAL`
agree with each other and with the source while the output raster is out.

## Neither agreement band is a filter for it

| band | width | against one line |
|---|---|---|
| `SourceMeasurement::RateAgreementPerMille` = 1 | 0.1% | narrower at 627 lines (0.16%) and at 311 (0.32%); **wider at 1125** (0.089%) |
| `Clock::RateAgreement` -- 0.833% relative, 0.5 Hz absolute | 0.833% | wider everywhere |

So on an SD or 800x600 source a one-line sample disagrees and costs a settling
attempt rather than being taken; on a 1125-line source it agrees, and agreement
is what the reading is held on. The clock path's band cannot reject one at all.
**Neither band can be tightened to do this job** -- a source whose period
genuinely wanders has to settle eventually, which is what
`RateAgreementAttempts` is for.

## The guard is the median, on every reading

`SourceMeasurement::sampleFieldRateHz()` times the pin three times and returns
the median. Two of the three have to be out the same way before the median is:
at the rates above that is about one reading in 3700, against one sample in 83.

It guards **whichever** reading the source settles on, which is usually the
second rather than the eighth -- a median confined to the attempt the agreement
runs out on leaves the common path unguarded.

A sample that reports no ticks has already waited out `FS_SAMPLE_TIMEOUT_MS`
twice, and the second and third have the same nothing to time, so a reading that
opens on one returns 0 without taking them. A silent source costs what it always
did.

## The retiming was blamed for a wobble it does not cause

A 1-2 px whole-picture vertical shift at ~25 Hz was attributed to
`SP_HS_LOOP_SEL`, on three builds with **one acquisition each**. Both halves are
refuted by controlled measurement:

| test | result |
|---|---|
| `SP_HS_LOOP_SEL` toggled 1 to 0 and back, field rate re-sampled 40x each way | 60.317 Hz both, 0.000% difference |
| the build re-acquired 8 times | 8/8 at 60.317, line rate 37879 |

So the retiming does not disturb the measurement and the build does not
reliably misacquire. **Three trials of one observation each are three coin
flips, not a controlled comparison**, and every conclusion drawn that way about
this wobble was wrong. Where a build difference is suspected, re-acquire N times
first -- it costs a source mode round trip rather than a flash.

The wobble's cause is not established. The field rate is the first thing to
read if it returns: `/framesync`'s `fieldRateHz` is what the acquisition
measured, and a value that is not the source's is the signature.

## What is not covered

Two consecutive readings that are both out the same way AND agree with each
other settle without the attempts running out. The median is what makes that
improbable rather than the agreement.

## Measuring it

Ten source mode round trips reading `VDS_HSYNC_RST` after each is **not** a test
of this. `/samplinglog?rates=N` is: it prints one line per raw sample with the
sync processor's count beside it, so the distribution is read directly rather
than inferred from what the engine did with it. The console is the other
instrument -- every reading the engine takes is printed as it is taken:

```
sampling: 628 lines x 60.51 Hz -> line rate 38060
sampling: 627 lines x 60.31 Hz -> line rate 37879
sampling: 627 lines x 60.31 Hz -> line rate 37879
```

The outliers are **accepted** -- a nonzero line rate -- because
`rateFollowsCount()` allows 5% against the held rate. Acceptance is not
agreement, and neither is stability.
