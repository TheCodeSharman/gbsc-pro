# A doubled field rate agrees with itself

`rateSettled()` is what a reading must pass before it is promoted to the judged
rate and before a divider is sized from it, and it passes on a PAIR of readings
agreeing to `RateAgreementPerMille`. A reading taken off half a frame agrees
with the next one taken the same way, so the pair is no evidence at all.

`a-transient-becomes-the-judged-rate.md` is what put promotion behind
`rateSettled()`. This is the strength of that gate rather than its position.

## What arrives on an input change to the Wii at 480p

The console, one selection, `vga` to `ypbpr`:

```
sampling: 524 lines x  92.18 Hz -> line rate 48396
sampling: 524 lines x 174.20 Hz -> line rate 0
sampling: 524 lines x 119.90 Hz -> line rate 62948
sampling: 524 lines x 119.85 Hz -> line rate 62924
sampling: 524 lines x 119.90 Hz -> line rate 62948
sampling: rate 62948 doubled 0 -> divider 694
sampling: 268 lines x  59.91 Hz -> line rate 16116
sampling: 269 lines x  59.93 Hz -> line rate 16183
sampling: rate 16190 doubled 1 -> divider 2200
sampling: 524 lines x  59.86 Hz -> line rate 31428
sampling: rate 31428 doubled 0 -> divider 1448
sampling: 524 lines x  59.93 Hz -> line rate 31468
sampling: rate 31468 doubled 0 -> divider 1448
```

The source is 59.93 Hz. **119.90, 119.85 and 119.90 are three consecutive
readings agreeing to 0.4 per mille and every one of them is twice the source.**
The count beside them is 524 and steady from the first sample, so the steadiness
gate is no defence: it is the rate that settles, not the count.

The divider sized from the doubled rate is 694 against the 1448 the source
wants. Installing it moves the sync processor's count to 267, which is measured
next and sizes 2200, which moves it again. `vga` installs ONE divider per
acquisition; `ypbpr` installs three to six, measured the same way across eight
consecutive input changes.

## A longer run is the same wrong idea

Requiring `RateAgreementRun` consecutive agreeing readings instead of a pair is
refuted on the bench. Three was tried: the host suite went green on a case built
from the trace above, and the unit installed the same bogus dividers, because
the doubled reading is **stable and repeatable for about 200 ms** and supplies
three agreeing samples as readily as two.

No run length rejects it. Agreement rejects noise and cannot reject bias, and
every sample here carries the same bias. Only a check against something
independently known can refuse a stable measurement of the wrong thing.

The cost of the attempt is one vsync sample per acquisition, which
`test_video_path.cpp` counts: 3 readings per mode change becomes 4.

## Where the time actually goes

Timed from the selection, with the detection trace on:

| span | | cost |
|---|---|---|
| selection reaching `loop()` | | 0.56 s |
| a failed pass, then the teardown | `det hsact,0` -> `det low power,1` | 1.12 s |
| **sync found** | `det found,2` | at 1.7 s |
| nothing but `frame time lock: cleanup` | | 1.92 s |
| the sampling thrash above | four installs | 0.95 s |
| settling, two more installs of 1448 | | 1.46 s |

`ypbpr` finds sync in 1.7 s and spends 4.3 s afterwards. `vga` takes 3.7 s to
find it -- most of it the sync-type probe -- and 0.55 s afterwards, with its
first `sampling:` line 0.05 s after `det found`. **The thrash is under a second
of it**, so it is not on its own the reason an input change costs what it does.

## What the readings point at instead

Every bad reading lands within about 200 ms of `det found,2`, and from there on
every sample is 59.93 Hz exactly. That is a settle, not a corroboration problem:
the quantity is wrong while the vsync path is still coming up, and right
afterwards, without anything being installed to make it so.

`SourceMeasurement::settlePasses_` already discards readings for exactly this
reason, returning `ClockSettling` until the count drains. It is armed by
`samplingClockLatched()` and by nothing else, so the passes immediately after
acquisition -- where these readings are -- are not covered by it.
