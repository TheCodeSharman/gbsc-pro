# The field rate measurement is unreliable, and only during acquisition

`TestBusRateMeasurement::sourceFieldRateHz()` returns the line rate, twice the
field rate, and values in the thousands, on a source running a steady 60 Hz.
Measured from the console over 150 s with the engine re-acquiring:

```
samples                 307
field rate in 55-65 Hz  241  (78.5%)
line rate 0 (rejected)  123  (40.1%)
min / median / max      60.31 / 60.31 / 37878.79 Hz
bands   {'55-65': 241, '100-140': 61, '>1000': 5}
```

The maximum is **37878.79 Hz against a line rate of 37879** — the pin was
carrying a line-rate signal when it was sampled. The 100–140 Hz band is twice
the field rate, which is one edge from each side of a change in what the pin
carries.

**In steady state it does not run at all.** The same capture on a settled unit
that nobody disturbed produced **zero** sampling lines in 150 s. So this is a
fault in acquiring a picture, not in holding one, and it cannot be reproduced
without provoking re-acquisition.

## What it costs

`rateFollowsCount()` rejects the bad readings, so they do not reach the picture
directly — they cost acquisition attempts. Where one is accepted it becomes the
held rate, and a held rate that is wrong strands the engine: measured at
`lineRateHz` 69686 and 82982 against a real 37879, both held, with the count
reading 627 correctly beside them.

## Two candidates, neither settled

**A second writer of the test bus.** `TEST_BUS_SEL` has twelve call sites.
`SyncOnGreen::SeparatorBus`, FrameSync's sweep and most sketch sites save and
restore the selector; `TestBusRateMeasurement` writes it and leaves it. So what
the pin carries depends on which caller ran last. This is the better fit for a
reading that is wrong but stable.

**Settling.** The selector is a mux written over I²C, and
`sourceFieldRateHz()` has no settle where `pllRateHz()` has 200 µs. Adding one
to both was measured and **refuted**: the absolute count of bad readings did
not move (61 -> 62 in the 100–140 band, 5 -> 4 above 1000), only the percentage,
because the sample count rose.

## What is NOT the cause

Interrupt latency from WiFi is not established. Both rate paths share
`FrameSync::sampleVsyncPeriod()`, which already drops the radio to
`WIFI_LIGHT_SLEEP` for the measurement window, and the `noInterrupts()` calls in
`framesync.h` are inside the shared edge ISRs rather than around a window one
path has and the other lacks.

HTTP polling from a host **does** add WiFi traffic during the window, so a
capture taken while polling registers is measuring its own observer. Read the
console alone.

## The ADC sampling phase is never searched

Worth recording beside this, because it is the other thing that varies per
acquisition. `Adc::acquirePhase()` walks every phase and scores it, but in both
of its branches the ADC phase is a constant:

```cpp
choosePhaseAdc(oversample == 4 ? halfSampleOn(MidField) : MidField);
```

The sweep only ever chooses the **sync processor** phase, and it chooses it as
half a turn away from the worst phase found. On the bench every phase scored at
most 1 sample off (`phase far` all zeros), so the argmax is taken over ±1
dither and the chosen phase moves between acquisitions although the algorithm
is deterministic.

## The Acorn modes against VESA

`SourceTiming::lookUp()` matches a measured source to a published raster on line
count, field rate and sync duty. The RISC PC's modes match VESA on the totals
but distribute them differently, so a match does not mean the active window
agrees:

| mode | pixel clock | htotal | vtotal | display starts | VESA says |
|---|---|---|---|---|---|
| 640x480@60 | 25.175 ✓ | 800 ✓ | 525 ✓ | 138 | 144 — 6 px out |
| 800x600@60 | 40.000 ✓ | 1056 ✓ | 628 ✓ | 216 | 216 — exact |
| 1024x768@60 | 65.000 ✓ | 1344 ✓ | 806 ✓ | 224 | 296 — 72 px out |
| 1280x1024@60 | 110.000 ✗ | 1728 ✗ | 1062 ✗ | 352 | 108.000 / 1688 / 1066 |

Sync duties: 640x480 0.1175 against VESA's 0.1200, 800x600 0.12121 against
0.12121, 1024x768 0.09524 against 0.10119, 1280x1024 0.09606 against 0.06635.
`SyncDutyTolerance` is 0.015, so the first three match and 1280x1024 does not —
and it fails the line-count test first anyway.

**So blanking a pass-through source to the matched standard's active window
crops real picture wherever the mode carves its border differently**: 6 px at
640x480 and 72 px at 1024x768. Only 800x600 agrees exactly.
