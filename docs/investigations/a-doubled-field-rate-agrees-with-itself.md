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

## The reading is taken through the bring-up divider

`SamplingLog` across one input change, with the divider in the second column:

```
smp,ms,divider,pllad_lock,sp_vtotal,sp_htotal,hperiod_if,vperiod_if,hsact,ifbits,int
smp,  42,1438,1,627,1438,223,  48,1,12,0     on vga, locked and correct
evt,det low power,1
smp,2120,2506,0,  0,   0,214,   2,1, 1,138   the teardown's divider
evt,det found,2
smp,4657,2506,1,524,2506,214, 524,1, 3,64    the doubled rate is read HERE
smp,4720, 694,0,524,2506,214, 524,1, 3,64    and sizes this
smp,5871,1446,1,524,1446,214, 524,1, 3,0     settled
```

**2506 is `Adc::BringUpDivider`**, written by `applyResetParameters()` inside
`setResetParameters()` on the low-power teardown. Detection hands the source
over with it still in force, and the first field rate is measured through it.

The field rate is timed on `DEBUG_IN_PIN` off `TestBus::selectInputVsync()` --
`VideoSourceAcquisition` passes `useSyncProcessorBus` false, so the held sync
type does not reach this -- which is the INPUT FORMATTER's vsync. The IF's
vertical output is counted in its own line units, and its line counter is
`IF_HSYNC_RST`, sized from the divider. Sized for a line the source does not
run, it emits vsync more than once per frame.

**The error is exactly 2.000x, which is what rules out simple clock scaling**:
2506 against the 1448 the source wants is a ratio of 1.73, and no reading shows
1.73. A clean factor of two is the line doubler's, so what is wrong at that
moment is how many IF lines the counter believes a frame holds, not the clock
rate as such.

`VPERIOD_IF` does not discriminate it -- 524 at the doubled readings and 524
once settled -- so it is not available as the independent witness.

## What a self-consistent reference changed

`setResetParameters()` wrote `IF_HSYNC_RST` as the literal `0x3FF` beside a
divider of 2506, and `lineCounterFor(2506, false)` is 2506 against an
eleven-bit register, so the undoubled bring-up line truncates to 458 -- which is
what the bench reads during the window the doubled rate is measured in.
`Adc::BringUpLineDoubled` states the scan the pair is carried as, the counter
becomes 1253, and it fits.

**The doubled readings go.** Six input changes carry no reading at twice the
source's rate, where every capture before it carried 119.85 and 119.90 and
sized a divider of 694.

**It buys no acquisition time**, because the next transient takes the place the
doubled one had:

```
sampling: 524 lines x 92.18 Hz -> line rate 48396
sampling: rate 48396 doubled 0 -> divider 936
sampling: 524 lines x 59.95 Hz -> line rate 0      the true rate, refused
sampling: 524 lines x 59.95 Hz -> line rate 0
```

That is `a-transient-becomes-the-judged-rate.md` rather than this: 92.18 Hz is
accepted first, becomes what `rateFollowsCount()` judges against, and the
correct readings are refused until `HeldRateRejectionLimit` drains. **92.18 Hz
is the first reading of every capture taken on this source, to the hundredth**,
so it is deterministic and not jitter.

## What this is not

It is not `SourceMeasurement::settlePasses_` being unarmed.
`inputTimingsChanged()` calls `applySampling()`, which calls
`samplingClockLatched()`, so five settle passes ARE armed on this path and they
drain during the 1.92 s between `det found` and the first reading. Arming it
again on acquisition changes nothing.
