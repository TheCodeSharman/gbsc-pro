# The rate is re-read through the clock it chose

A measurement of the source is two halves with a hardware write between them: the
count and the field rate, then the divider derived from them, then the hsync duty
through that divider. `Tv5725::SourceMeasurement` states that and
`VideoSourceAcquisition::resolveFromSource()` carries it out in that order.

What it also does is run the whole sequence again on the next pass, because the
pass that installs the clock cannot complete.

## Why the pass cannot complete

`VideoPath::applySamplingClock()` ends in `sampling_.samplingClockLatched()`,
which sets `settlePasses_ = LatchSettlePasses`. `measureDuty()` is called in the
same pass and returns `ClockSettling` immediately, so the solve does not land and
`modePending_` stays true. The settle then drains through `measureRate()`, one
pass each, and the pass after that runs `measureRate()` for real -- against a
clock the rate it is re-reading was itself used to choose.

Measured on the Wii at 480p, `/bootlog`:

```
measured: 524 lines x 59.93 Hz -> line rate 31468
installing: divider from line rate 31468, doubled 0 -> 1446
sampling: 215 lines x 30.88 Hz -> line rate 6670
sampling: 277 lines x  0.00 Hz -> line rate 0
```

Five passes separate the install from the re-read. `rateFollowsCount()` judges the
re-read against the rate the engine holds, refuses it, and the pass answers
`Unmeasurable` -- so a completed acquisition is staked on one reading of a value
the engine put there itself.

## What the halves actually need

**The duty is the half that cannot survive the install**, and it is the only one.
It is a ratio of the sync pulse to the LINE counted in ADC samples, so a duty
measured through one divider and spent on a window sized in another's units is
out by the ratio between them.

**The rate and the count both survive a divider the ADC PLL cannot lock to** --
measured, 311 lines and 50.08 Hz on both test buses at four such dividers -- which
is why the first half is taken through whatever is in force.

So the pass after a latch has exactly one thing left to do.

## The rule

The rate is read once per rate/duty pair. `measureRate()` answers from the held
value while `rateTaken_` is set, and `measureDuty()` releases it.

**Released on the duty PASS RUNNING, not on the duty succeeding.** A source whose
duty cannot be read at all -- no divider latched, no pulse -- would otherwise
never let the rate be read again. `takeDuty()` returning false still ends the
pair.

**And only until then.** A suppression lasting the whole mode change suppresses
the refusal `rateFollowsCount()` exists for: a source may change rate at an
unchanged count, a RISC PC does, and that reading has to be able to fail.

## What it costs

One fewer field-rate reading per mode change. On the bench RISC PC a mode change
took three readings and takes two.

## Why the measurement cannot simply all happen under the reference clock

Because the duty needs a LATCHED clock, whichever one.
`SourceMeasurement::readSource()` divides `STATUS_SYNC_PROC_HLOW_LEN`, counted in
real ADC clocks, by `PLLAD_MD`, which is a value the firmware wrote. Those are the
same unit only while the PLL is running what was written -- `PLLAD_MD` is loaded
on a rising edge of `PLLAD_LAT`, so between the write and that edge the register
reports the new value while the chip still clocks at the old one.
`Adc::dividerLatched()` is checked against `STATUS_SYNC_PROC_HTOTAL` for that
reason: it counts real ADC clocks per line and so reports the LATCHED divider,
which is the only witness on the board that a write reached the PLL.

Unlatched, both sides of the ratio are unusable rather than one: measured on the
collapsed ypbpr state, `HLOW_LEN` dithers 1..88 where a latched state reads a
steady 161, and `HTOTAL` holds 427 against dividers from 640 to 2040.

The ratio itself is dimensionless and IS portable across dividers -- `VideoPath`
spends a stored duty at whatever divider is in force. So the requirement is a
latched clock, not the final one. On this source the reference divider is not a
latched clock, which is what keeps the duty downstream of the install.

**A reference the PLL latches on would let the whole measurement happen in one
place**, which is the design option this leaves open.
