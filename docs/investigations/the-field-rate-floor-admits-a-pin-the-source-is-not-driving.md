# The field-rate floor admits a pin the source is not driving

A composite-sync source does not acquire in pass-through. The ADC PLL is found
running well below the divider it was given, and the reason is neither the
route, nor the divider, nor the charge pump: the ADC PLL's crossover row was
sized from a field rate of 15.32 Hz, which is what the board reads at a pin a
composite-sync source does not drive.

Bench source throughout: RiscPC on `vga` at 640x480@60, `SYNC 1`, pass-through
entered with `preferScalingRgbhv` off.

## The signature

`PLLAD_MD` 2039 with `STATUS_SYNC_PROC_HTOTAL` reading 1700..1729, `VTOTAL` 522
against 524, `state: absent` for as long as it is left. `HPERIOD_IF` reads the
212 the mode is due throughout, so the source's line period is not in question.

The sync processor counts in ADC clocks, so `HTOTAL` short of `PLLAD_MD` is the
ADC clock running below the frequency asked for.
`VideoSourceAcquisition::sourceMoved()` takes `Adc::dividerLatched()` as the
horizontal half of the acquired test, on every route, so the state pins at
`SourceUnlocked` and never advances.

## Three explanations, all refuted

**The divider.** Walked 2039 down to 900 through `/sampleclock`, which in
pass-through writes the whole group and moves `HD_HSYNC_RST` with it. `HTOTAL`
is short at every value. At 1566 -- the divider the scaling path picks for this
mode -- pass-through reads 1407..1520 where the scaling path reads 1566. The
comparison is controlled and it is not the rate.

**The route as such.** Entering pass-through with composite sync already
established fails identically, `HTOTAL` 1726 against 2039, so the fault is not a
sync type moving underneath a route that maintains nothing.

**The charge pump.** `Adc::applyForBypassRgbhv()` writes `PLLAD_ICP` 4 where the
scaling path runs 6, and the pump moves the clock:

| `PLLAD_ICP` | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|
| `HTOTAL` at `PLLAD_MD` 2039 | 1558 | 1603 | 1700..1729 | 1835 | 1938..1948 | 1995..2004 |

`STATUS_SYNC_PROC_HLOW_LEN` tracks it proportionally, its ratio to `HTOTAL`
pinned at 0.117 across the whole table, so what moves is the clock and not the
counter. A locked loop's output frequency cannot depend on its charge pump, so
this is evidence that the loop is not locked -- not a knob that fixes it. The
pump is a symptom.

## What it is

`PLLAD_KS`, the crossover row, reads **3** where the 65.7 MHz this divider and
rate imply needs **1**, so the VCO is divided by eight instead of two. Writing
the row by hand is the whole fix:

| | `HTOTAL` | `STATUS_MISC_PLLAD_LOCK` |
|---|---|---|
| `PLLAD_KS` 3 | 809..810 | 0 |
| `PLLAD_KS` 1 | **2039**, exactly | **1** |

The ADC PLL locks a composite-sync source in pass-through at the full divider,
at `PLLAD_ICP` 4, with the separator in the path.

## Where the row comes from

`Adc::applySampleRate()` reads the crossover row against CKO, which is the
divider times the line rate the caller hands it. The console carries the moment
the wrong row was installed:

```
sampling: 524 lines x 15.32 Hz -> line rate 8043
sampling: rate 8043 doubled 0 -> divider 2046
```

2046 x 8043 is 16.5 MHz, which is below the table's 20 MHz row, and that row is
`PLLAD_KS` 3.

15.32 Hz is not a field rate. It is what `TestBusRateMeasurement` counts at the
input V pin when the source is sending composite sync and driving no V pin at
all. `VideoSignal::FieldRateMinHz` was 15.0, so the reading cleared the gross-
error net by two tenths of a hertz and everything downstream treated it as a
measurement. `SourceMeasurement::measureLineRate()` then converts it,
`rateFollowsCount()` rejects it -- and after `HeldRateRejectionLimit` rejections
the escape hatch that stops a genuinely changed source being refused for ever
lets it through.

The hatch is not the defect. It bypasses `rateFollowsCount()` and not
`VideoSignal::isVideo()`, so a rate that reaches it has already been called
video. The floor is the defect.

## The floor

`FieldRateMinHz` is 30.0. The band it has to clear on both sides is measured
rather than assumed: no analog source this board takes sends fields slower than
about 47 Hz, and the readings the board produces at a pin nothing is driving
measure 15.31..21.92 Hz on this source.

A rate below the floor is refused, which leaves the engine measuring nothing
rather than clocking the ADC from a number that is not a rate. That is the right
direction to fail in: a refused measurement keeps the last good crossover row,
and a wrong one is indistinguishable from a working unit in a register dump.

## What it fixes, measured

Pass-through, composite sync, 640x480@60, after the change:

```
PLLAD_MD 2039   PLLAD_KS 1   STATUS_SYNC_PROC_HTOTAL 2040
STATUS_MISC_PLLAD_LOCK 1     HPERIOD_IF 212
```

`HTOTAL` is inside `Adc::LatchedSamplesTolerance` of the divider and the PLL
reports lock, where before the change the same state read 1700..1729 and 0.

## What it does not fix

`STATUS_SYNC_PROC_VTOTAL` still reads 522 against 524 and the source still does
not acquire. The vertical half is a separate fault and is now the only one left
in this state: with the clock right, the field-rate measurement reads 3937.58 Hz
against a line rate of 31500, which is the line rate over eight -- a line-derived
signal reaching a measurement that wants vertical sync.

Two findings belong to that half and are recorded here because they were taken
in the same window:

- `SP_H_PULSE_IGNOR` is found at 255 -- `OwnVsyncPulseIgnore`, the separate-sync
  value -- inside a `SP_SOG_MODE` 1 configuration. Two writers disagree. Moving
  it off 255 takes `VTOTAL` from 498 to 521 at once, and 107, 51, 16 and 2 are
  indistinguishable from each other.
- The separator level matters more than the coast window. `ADC_SOGCTRL` 1 gives
  a `VTOTAL` wandering 534, 537, 538, 540, 542, 543; anything from 4 upward gives
  a steady 522. The recovery ladder walks that level down, so it manufactures the
  instability it is climbing to fix. Coast pairs of 7/3, 9/9, 3/3, 1/1 and 0/0
  move it by at most one line; 16/16 destabilises it.

## What not to reinstate

`STATUS_MISC_PLLAD_LOCK` reading 0 is not proof the PLL is unlocked on this
board -- it reads 0 in the working pass-through separate-sync state. It reading
**1** is worth something, and it goes to 1 when the row is right. `HTOTAL`
against `PLLAD_MD` remains the witness that the divider reached the PLL.

A divider sweep cannot diagnose this. The row is a function of divider times
rate, so a sweep that moves the divider moves the row with it and every point is
short by a different amount, which reads as a loop that cannot hold high
frequencies.
