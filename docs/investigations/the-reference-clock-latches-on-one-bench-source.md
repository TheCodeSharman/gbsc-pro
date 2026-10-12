# The reference clock latches on one bench source and not the other

The reference sampling clock is the clock every measurement of an arriving
source is taken through. It latches on `vga` and not on `ypbpr`, so whether a
measurement has a usable time base depends on which source arrived. Measured
2026-10-12 on both inputs.

## What a latch is, and what reports it

`STATUS_SYNC_PROC_HTOTAL` counts real ADC clocks per line, so on a locked PLL it
reports the divider back. `Adc::dividerLatched()` compares the two within
`LatchedSamplesTolerance`, and nothing else on the board can see a missing
latch: `PLLAD_MD` reads back the value written whether or not `PLLAD_LAT` loaded
it, and `STATUS_MISC_PLLAD_LOCK` reads 0 on states whose count matches the
divider exactly.

`SourceMeasurement::measureLineRate()` says which it got with every reading, and
names the clock as the pair it is:

```
measured: 311 lines x 50.08 Hz -> line rate 15625, clock 2040@15625 htotal 2040
measured: 524 lines x 59.93 Hz -> line rate 31468, clock 2040@15625 htotal 1670 UNLATCHED
```

## The readings

| clock | source | `HTOTAL` against the divider |
|---|---|---|
| 2040 @ 15625, the reference | `vga` 320x256@50 | **2040**, exact, every reading |
| 2040 @ 15625, the reference | `ypbpr` 480p | 1670, 1670, 225 |
| 1446 @ 15625, as a trial reference | `vga` | **1446** |
| 1446, as a trial reference | `ypbpr` 480p | 877, 1199, 871, 197 |
| 2200, solved | `vga` | **2200** |
| 1446, solved | `ypbpr` 480p | **1446** |

So every divider a measurement CHOOSES latches on the source it was chosen for,
and the reference latches on `vga` only. Trialling 1446 as the reference moves
nothing: it latches on `vga` as 2040 does, and on `ypbpr` it is worse — the count
collapses from 524 to 213.

**A transient reads like a failure and is not one.** Two readings taken
immediately after the sync-type probe gave `HTOTAL` 2073 then 2264 on `vga`, on a
source whose settled readings are exactly 2040. The probe writes
`SP_EXT_SYNC_SEL`, which the chip latches as a SOG switch, so a reading inside
that window is a disturbance the probe caused. Judge the latch on settled
readings.

## What survives an unlatched clock, and what does not

**The line count and the field rate both survive it.** At 2040 on `ypbpr` the
sync processor counts a correct 524 and the field rate reads 59.93 Hz twice
running with `HTOTAL` 1670 throughout. The sync processor counts the source's
vertical directly and the field rate is timed by the ESP off `DEBUG_IN_PIN`, so
neither needs the ADC clock to be the one that was written. **A reading can
therefore look entirely healthy through a pipeline the PLL is not driving**, and
before the witness was printed nothing said so.

**The duty does not survive it.** `readSource()` divides
`STATUS_SYNC_PROC_HLOW_LEN`, counted in real ADC clocks, by `PLLAD_MD`, which is
a value the firmware wrote — the same unit only while the PLL runs what was
written. Unlatched, `HLOW_LEN` reads 1, 2, 3, 5 and 8; latched on the same source
it reads a steady 106 against a divider of 1446, which is 7.33% against 480p's
own 7.2%.

**And the count's correction does not survive it.**
`SourceMeasurement::linesPerCount()` recovers a PLL locked to every kth hsync by
comparing `HTOTAL` against `divider x k`. Unlatched that comparison is made
against a line length the divider never set, so a correction can be applied to a
count that needed none.

**So the duty is the reading that decides where the measurement can happen.** It
is dimensionless and portable across dividers, so what it needs is a latched
clock rather than the final one — and on `ypbpr` the reference is not one.

## The row is half the clock

The post-divider row and the VCO gain are computed from the line rate handed to
`Adc::applySampleRate()`, and for a reference that rate is the clock's own band
rather than a measurement. Moving the divider moves the row with it, so a sweep
of dividers cannot change what the row is premised on:

- swept 1200..2600 in steps of 100 with the row read against the held rate,
  `STATUS_SYNC_PROC_HTOTAL` pins at about 846 and 429 — which are 27 MHz and
  13.5 MHz over the line rate. That is the chip's own reference, so the part is
  clocked by its crystal at every point and the sweep reads a free-running
  pipeline rather than a PLL. **A divider sweep is not a test of the row.**
- the same divider at two rows settles it in one step. 1446 at row 1 reads
  `HTOTAL` 1446 and a duty of 106; 1446 at row 2 reads 412 and a duty of 3.

`Adc`'s reference clocks are therefore `(divider, line rate)` pairs, and the same
divider at two rates is two clocks. A list of dividers read against one rate
lands every entry on one row.

## The VCO gain crossover, which explains one value and predicts none

`vcoGainFor()` raises `PLLAD_FS` above `HighVcoGainAboveHz`, 130 MHz.

| clock | cko | row | VCO | `FS` |
|---|---|---|---|---|
| 2040 @ 15625 | 31.9 MHz | 2 | 127.5 MHz | 0 |
| 2200 solved, `vga` | 34.4 MHz | 2 | 137.5 MHz | 1 |
| 1446 @ 15625 | 22.6 MHz | 2 | 90.4 MHz | 0 |
| 1446 solved, `ypbpr` | 45.5 MHz | 1 | 91.0 MHz | 0 |

2040's VCO sits 2.5 MHz under the crossover and so runs the low gain at the top
of its range. **It is not the explanation**, because 2040 latches on `vga` at
exactly that configuration. Which clock a source can be measured through is not
derivable from the gain band, which is the argument for searching rather than for
a better single value.

## Two instruments that cannot answer this

**A frozen poke cannot.** With automation frozen on `ypbpr` at 480p,
`STATUS_SYNC_PROC_VTOTAL` falls to 97..135 and both test buses time nothing at
all — `/samplinglog?rates=` returns 0 in 60 of 60 readings on each bus. A
reference install forgets the sync processor's coast and clamp placements and
only the acquisition pass puts them back, so a frozen unit loses the source and
every clock then reads unlatched. Two latch tables taken that way report nothing
latching, including clocks the live engine latches on seconds earlier. **Read the
witness off the live engine's own log instead.**

**`/refclock` could not, until it installed the whole clock.** A reference is the
PLL group, the scan its line implies and the capture counter's origin; the probe
wrote the group alone, so it measured a clock no production path installs. It
takes `rate` now, because a divider alone does not name a clock.

## What a reference install is

One act, `VideoPath::installReferenceClock()`, for the selection, the exit from
pass-through, the bring-up and the retry alike:

- the PLL group, through `Adc`, with the row read against the clock's own band
- the scan that clock's line implies, because the line counter counts IF units
  and the decimation is what an IF unit is
- `SP_RT_HS_SP`, the capture counter's origin, because a sync-processor reset
  leaves it 0 and a block that will not retime never counts
- `SourceMeasurement::samplingClockLatched()`, because the install re-latched the
  PLL like any other

**The sync-processor reset comes before it, not after.** Three of the four paths
reset the block after installing, which takes away the origin the install wrote.
