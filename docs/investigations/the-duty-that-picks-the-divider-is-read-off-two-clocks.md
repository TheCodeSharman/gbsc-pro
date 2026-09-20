# The duty that picks the divider is read off two clocks

One source in one mode solves to `PLLAD_MD` 2206 on some acquisitions and
2278/2280 on others, and the whole difference is one measurement: the hsync
duty, read in the millisecond after the reference divider is latched, when the
sync processor is not yet counting that divider's samples.

Measured on the bench RiscPC at 320x256@50 on `vga`, output 1080p, six source
mode round trips through 800x600@60.

## The divider is not the ADC's rating, it is the duty

`SamplingClock::recommendedDivider()` backs off the ADC rating and then caps
against what one capture window can span, and on this source the cap is what
binds. `VideoSourceLine::framableIfLine()` computes that cap as

    units = (CaptureWidthLimitUnits + 2 + lag) / (1 - syncDuty)

so the divider is a function of the duty alone, doubled for the doubled line:

| duty read | framable | divider |
|---|---|---|
| 0.018, below `DutyMin` 0.041, so `FallbackDuty` 0.07 stands in | 1103 | **2206** |
| 0.100, inside the guard, so it is believed | 1139/1140 | **2278/2280** |

The source's true duty is **0.0707** -- `HLOW_LEN` 156 against `HTOTAL` 2206
once settled, and 161 against 2280 in the other outcome, which is the same
0.0707. So the settled chip agrees with itself and with the mode file's
`36/512` = 7.03% either way.

**Neither reading is the source's duty.** 2206 is right only by accident: the
bad reading falls outside the guard, and the fallback happens to be the truth to
three figures.

## What the two sides of the ratio are

`SourceMeasurement::readSource()` forms `HLOW_LEN / Adc::dividerInForce()`.
Logging both sides at every read, with the sync processor's own line length
beside them, over six solves:

```
 10.79  duty: 42 low / 2250 divider, htotal 2102
 10.79  duty: 225 low / 2250 divider, htotal 2358
 10.79  duty: 42 low / 2250 divider, htotal 2102
 10.79  sampling: duty 19/1000 head 1 framable 1103 -> divider 2206
 77.33  duty: 225 low / 2250 divider, htotal 2400
 77.33  sampling: duty 100/1000 head 1 framable 1140 -> divider 2280
143.90  duty: 41 low / 2250 divider, htotal 2400
143.91  sampling: duty 18/1000 head 1 framable 1103 -> divider 2206
210.66  duty: 41 low / 2250 divider, htotal 2400
210.68  sampling: duty 18/1000 head 1 framable 1103 -> divider 2206
276.97  duty: 41 low / 2250 divider, htotal 2401
276.98  duty: 41 low / 2250 divider, htotal 2401
276.99  duty: 225 low / 2250 divider, htotal 2099
276.99  sampling: duty 100/1000 head 1 framable 1140 -> divider 2280
343.48  duty: 225 low / 2250 divider, htotal 2099
343.50  sampling: duty 100/1000 head 1 framable 1140 -> divider 2280
```

Three solves each way out of six. **`HLOW_LEN` takes one of two values, 41/42 or
225, where 159 is due**, and the solve uses whichever sample it lands on -- at
276.99 the value flips between the second read and the third, inside three
milliseconds, and the third is the one taken.

**The two states are correlated, not noise.** `htotal` 2400/2401 always comes
with `low` 41/42, and `htotal` 2099/2102 always with 225. 2400 is the value
RD-5725-1.1's counter reads with the coast pair and `SP_DLT_REG` unconfigured.
`htotal` never reads the 2250 the duty is being divided by, at any point before
the solve, and reads it exactly afterwards.

## Dividing by the counter instead does not fix it

The tempting repair is to form the ratio from one block on one clock --
`HLOW_LEN / STATUS_SYNC_PROC_HTOTAL` rather than `HLOW_LEN / PLLAD_MD` -- so
both sides are counted the same way whatever the lock state. The measured pairs
refute it: `41/2400` = 0.017 and `225/2099` = 0.107, against a true 0.0707.
**The counts themselves are wrong, not just the divisor**, so a self-consistent
ratio of two wrong counts is still wrong, and now inside the guard in both
outcomes rather than one.

## Settled, the register also latches the COMPLEMENT

On one acquisition of this same source at this same divider, `HLOW_LEN` read a
steady **2050/2206 = 0.9293** over ten samples 0.7 s apart -- exactly
`1 - 0.0707`, the pulse inverted -- where earlier acquisitions read a steady 156.
So the register carries either the pulse or its complement, per acquisition,
with nothing in the registers saying which.

A complement always lands outside `DutyMax` 0.152 and so always takes the
fallback, which is why it cannot be told apart from a correct reading by the
divider it produces: both give 2206.

It arrived alongside a railing `HPERIOD_IF` -- 511, 271, 12, 34, 263, 275 across
the same samples, with a clean picture on the panel -- and **one source mode
round trip cleared both at once**, back to `HLOW_LEN` 156 and `HPERIOD_IF` 431
steady. One instance of the pairing, so it is a correlation and not yet a
mechanism.

Neither regime accounts for the solve-time readings. At the reference divider
2250 the pulse is 159 and its complement 2091; the solve reads 41 or 225, which
is a third thing again.

## What the wrong divider costs

2278 against 2206 is 3.3% more ADC samples to the line, so the capture window,
both scales and the framing all differ: `IF_HB_SP2` 165 against 129,
`VDS_HSCALE` 539 against 546. The picture is solved consistently for whichever
was chosen and looks correct; the sampling density is simply not the one the
source is due, and it changes between acquisitions of the same mode.

## It is NOT the pan, and that is measured

Filtering the same six round trips by divider, with the picture photographed
after each:

| divider | pan against the first frame |
|---|---|
| 2206 | +0, **+49**, **+49** |
| 2280 | +26, +25, +26 |

The 2206 group is itself bimodal, two states 49 columns apart, which is the
displacement
[the-pan-is-downstream-of-the-scaler.md](the-pan-is-downstream-of-the-scaler.md)
records at one divider and one capture window. So there are two independent
nondeterminisms in one solve, and closing this one leaves that one standing.

## A gate on the counter is parked, not rejected

Withholding `Measured` until the counter agrees with the divider is written and
host-tested, and is parked outside the repo rather than landed. On the bench the
counter never agrees during a solve -- 2400 or 2099 against 2250, right up to
the reading -- so the gate may spend its whole budget and fall through every
time, buying the delay and not the measurement. It also needs the fake bus to
model `STATUS_SYNC_PROC_HTOTAL` echoing `PLLAD_MD`, which other cases
deliberately seed apart.

## Method

`GBS_TRACE_WRITES=1` for the write trace, and two console lines: the duty's two
sides at every read in `SourceMeasurement::readSource()`, and what the divider
was chosen from in `VideoPath::solveSampling()`. Both are in the build. The
round trip is `MODE X800 Y600 C256 F60` and back, driven from ModeServ, with a
`PAD_SYNC_OUT_ENZ` toggle after the return so the encoder re-acquires -- which
is free of the pan, measured at 0 columns over eight forced re-locks.
