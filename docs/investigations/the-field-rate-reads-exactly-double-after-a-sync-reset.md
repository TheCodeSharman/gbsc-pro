# The field rate reads exactly double after a sync reset

`SourceMeasurement::sampleFieldRateHz()` returns **exactly twice the source's
field rate** in the window after the sync processor is reset -- two or three
consecutive samples, clearing in a few hundred milliseconds. Measured on both
bench inputs:

| source | sync | count | settled | the samples in that window |
|---|---|---|---|---|
| Wii 480p on `ypbpr` | sync on green | 524 | 59.93 Hz | 119.87, 119.87, 119.87 |
| RiscPC 320x256@50 on `vga` | separate H/V | 311 | 50.08 Hz | 100.16, 100.16, 100.16 |

The rate is timed off the input formatter's vertical on the test bus
(`TestBusRateMeasurement::sourceFieldRateHz(false)` selects it), so what is
doubled is the number of vertical assertions the pin carries rather than
anything arithmetic in the engine: the period is counted on the ESP's own clock
and the divider does not enter it.

## `rateFollowsCount()` hides it, and a change of input removes the guard

Every doubled sample on a source whose count has not moved reports `line rate
0`. The count had not moved, so `rateFollowsCount()` refused the rate against
the held one and nothing acted on it. **That is why the fault is invisible on a
re-detect.**

A change of input is the case the guard is deliberately off for -- a moved count
IS a mode change, so any rate is admitted. Measured over three `vga` -> `ypbpr`
selections, every one of them:

```
sampling: 524 lines x 119.87 Hz -> line rate 62936
sampling: rate 62936 doubled 0 -> divider 694
sampling: 268 lines x 60.05 Hz  -> line rate 16125
sampling: rate 16125 doubled 1 -> divider 2200
sampling: 524 lines x 59.93 Hz  -> line rate 31468
sampling: rate 31468 doubled 0 -> divider 1448
```

62936 is exactly twice 31468. So the engine solves **three dividers and three
ADC PLL re-latches** before it lands, on every `ypbpr` selection, against one on
`vga`.

## Four candidates, all refuted

Each of these was the leading explanation at some point and each is now closed
by measurement. Do not reinstate one without new evidence.

**NOT the input formatter's scan being half applied.** The scan is five
registers -- three routing the line doubler, plus the counter and the decimation
that size the line for it -- and they used to be written by two owners: the
selection installed the reference line's counter and decimation while the solve
wrote the three path registers for the arriving source, its own counter write
refusing. The doubled samples landed in that window, which read `PRGRSV` 1,
`LD_RAM_BYPS` 1, `LD_SEL_PROV` 1 against `HS_DEC_FACTOR` 1 and a counter of
1253. With `InputFormatter::applyScan()` making the five one write, a register
poll interleaved with the console across a selection reads a consistent scan on
every one of hundreds of samples -- and **three consecutive samples still read
119.87**, taken at `MD 2506 RST 1253 DEC 1 path 000`, wholly agreeing.

**NOT the doubled scan itself.** The everyday bench source is 311 lines and is
captured line-doubled all day, reading 50.08 Hz. A doubled scan is not enough.

**NOT the divider.** Held at the reference divider with
`/sampleclock?hold=2506` -- the engine solving around it, so the state is
`MD 2506`, `STATUS_SYNC_PROC_HTOTAL` 2506, counter 1253, decimation 1, the exact
register state the doubled samples are read in -- a source mode change forces a
fresh measurement and it reads **50.08 Hz, twice over**. The divider the doubled
samples share is a coincidence of when the reference clock is in force, not a
cause.

**NOT the sync arrangement, and not the Wii.** The asymmetry the earlier trials
showed -- 8 of 8 on `ypbpr` against 0 of 8 on `vga` -- does not survive a
`vga` source that is line-doubled: the RiscPC at 320x256@50 on separate sync
reads three consecutive samples at 100.16 Hz. The earlier `vga` trials were
taken at 800x600@60, which is progressive, so what they varied was the scan and
not the connector.

**NOT the coast.** `SP_PRE_COAST`/`SP_POST_COAST` read 7/6 unchanged through the
doubled samples and the correct ones that follow, with `SP_SOG_MODE` 1,
`SP_EXT_SYNC_SEL` 1, `SP_H_PULSE_IGNOR` 2 and `SP_DLT_REG` 0xC0 beside them, and
the sync processor's line count is 524 and correct in the same samples. The
serration mechanism of `two-owners-of-the-coast-lengths-double-the-count.md`
does not explain this one.

## What is left

The window itself: after the sync processor is reset and before the source has
settled, with the reference clock in force. In every tape the doubled samples
arrive within a second of `source absent` or `DETECT` and are followed by a
correct one with nothing in the register set having moved that any of the
refutations above leaves standing.

**Do not read it as ordinary settling.** A first sample of 60..160 Hz followed by
a good one is documented as `getSourceFieldRate()` settling and 119.87 sits
inside that band, which is exactly what has let this pass as normal. A settling
analog path gives arbitrary values; three consecutive samples at 2.0000x do not.

The next instrument is the one a 0.4 s HTTP poll cannot be: the doubled samples
and the correct ones are 300 ms apart, and what separates them has to be sampled
from inside `loop()`. `Tv5725::SamplingLog` sweeping the sync processor and the
input formatter's status bits through that window is the measurement, not more
point reads.

## Two refuted attempts at a fix, before the cause was known

Both were aimed at the mixed scan, which is now refuted as the cause -- but the
shapes are worth not repeating.

Making the reference line go in as a whole scan is correct and not sufficient.
`applyLineDoubling(true, component)` followed by `writeLineCounter(divider,
true)` does make the reference window consistent, measured, and the mixture is
then **re-created** by `applyLineDoubling(false, ...)` a moment later. The
divider churn was unchanged: still 694, then 2200, then 1448. It also cost about
a second of acquisition on `ypbpr`, 5.5..6.3 s against 4.9..5.1 s.

Refusing the scan when its line will not fit the counter deadlocks. The divider
install on the output path is gated on the scan having changed, so a refused
scan stops the divider moving and the scan can then never come to fit. Four host
suites catch it.

**Do not judge an attempt on one run, and let the unit settle after a flash.**
The first of those measured 1 of 8 immediately after its flash and 8 of 8 on the
identical build once settled, which read as a fix and was not one.

## The scan has one owner now, and that stands on its own

`InputFormatter::applyScan(divider, lineDoubled, component)` writes all five
registers or none of them, and it is the only way in. `VideoPath` applies it
beside the divider it was sized for; `solveLineDoubling()` decides the scan and
applies it against the clock already in force, so the block is still corrected
while the source is unmeasurable -- its own measurements only mean something
once its scan matches the source, and a scan derived after that gate is never
reached. The three callers outside the engine -- the selection edge, the reset
parameters and the `/sampleclock` debug route -- each install a consistent pair.

What that bought, measured across three `vga` -> `ypbpr` cycles either side:
the refused-counter lines per selection fell from four to two, acquisition was
unchanged (`vga` 3.8 s, `ypbpr` 5.7..6.2 s), and the scan never half-describes
the line. What it did not buy is the doubled rate or the divider churn, which
are unchanged.

`tools/gbsc-pro-hwtest/test_selection_measures_one_line.py` guards it: polled
across one selection, the pre-fix build reads the mixture in 13 of 266 samples
and the fixed build in none.

## The selection path installs the clock and the line together now

`Adc::BringUpLineDoubled` exists because the reference divider cannot be
represented undoubled -- 2506 truncates to 458 in an eleven-bit counter.
`applyInputSelection()` used to install the clock alone, leaving the previous
source's line beside it, and `VideoPath::inputTimingsChanged()` then re-applied
the divider in force carrying the previous source's scan. Both install the
reference pair whole now, and `inputTimingsChanged()` re-asserts the clock
alone -- the divider being the one already in force, the line beside it already
describes it.
