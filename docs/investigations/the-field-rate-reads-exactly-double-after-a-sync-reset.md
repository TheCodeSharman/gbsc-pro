# The field rate reads exactly double after a sync reset, on the component path

`SourceMeasurement::sampleFieldRateHz()` returns **119.87 Hz against a real
59.93** in the window after the sync processor is reset on `ypbpr` -- exactly
twice, two or three consecutive samples, clearing in about 0.3 s. On `vga` it
does not happen.

The rate is timed off the input formatter's vertical on the test bus
(`TestBusRateMeasurement::sourceFieldRateHz(false)` selects it), so what is
doubled is the number of vertical assertions per frame rather than anything
arithmetic in the engine.

## The measurement

`/sc?~` on a settled source, eight trials each, same build, same command. The
count is the engine's own `sampling:` line.

| input | trials showing an exactly doubled rate | what the samples read |
|---|---|---|
| `ypbpr`, Wii 480p, sync on green | **8 of 8** | 119.87, 119.87, 119.87 then 59.93 |
| `vga`, RiscPC 800x600@60, separate sync | **0 of 8** | 60.31 throughout |

It is not strictly exclusive to the component path -- one earlier `vga` trial in
eleven read `627 lines x 120.63 Hz`, which is the same exact doubling. So the
asymmetry is overwhelming rather than absolute, and an explanation resting on
component video alone has that one reading to account for.

## `rateFollowsCount()` hides it, and a change of input removes the guard

Every doubled sample above reported `line rate 0`. The count had not moved, so
`rateFollowsCount()` refused the rate against the held one, and nothing acted on
it. **That is why the fault is invisible on a re-detect.**

A change of input is the case the guard is deliberately off for -- a moved count
IS a mode change, so any rate is admitted. Measured over four `vga` -> `ypbpr`
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
ADC PLL re-latches** before it lands, on every `ypbpr` selection, 4 of 4. The
same four `vga` selections solved one divider each and showed no wrong rate.

## The coast is not the cause

The obvious candidate is the mechanism of
`two-owners-of-the-coast-lengths-double-the-count.md`: an insufficient coast
lets the equalisation and serration pulses either side of the vertical interval
be counted, and roughly twice comes out. **Refuted here.** Read against the
console in one window, `SP_PRE_COAST`/`SP_POST_COAST` hold **7/6 unchanged**
through the doubled samples and through the correct ones that follow 0.3 s
later, as do `SP_SOG_MODE` 1, `SP_EXT_SYNC_SEL` 1, `SP_H_PULSE_IGNOR` 2 and
`SP_DLT_REG` 0xC0.

So whatever clears it is not one of those six, and the doubling is not the
serration count wearing another name -- the sync processor's line count is
**524 and correct** in the same samples that report 119.87 Hz.

What does clear it is somewhere in the recovery ladder: the last doubled sample
lands 90 ms after `recovery: coast window at pass 8` and the first correct one
100 ms after that. A 0.4 s register poll cannot resolve which write did it, so
the next instrument is a `SamplingLog`-style sweep of the sync processor through
that 300 ms rather than more point reads.

## What is established, and what is not

Established: the phenomenon, its exactness, its input asymmetry, the guard that
hides it, the case that removes the guard, and the cost in dividers. Not
established: why the input formatter's vertical asserts twice, and why the
component path and not the RGBHV one.

**Do not read the divider churn as settling.** A first sample of 60..160 Hz
followed by a good one is documented as `getSourceFieldRate()` settling, and
119.87 sits inside that band -- which is exactly what has let this pass as
normal. A settling analog path gives arbitrary values; three consecutive samples
at exactly 2.0000x do not.

## The selection path installs the clock without the line it implies

Found in the same tapes and separate from the above. `Adc::BringUpLineDoubled`
exists because the reference divider cannot be represented undoubled -- 2506
truncates to 458 in an eleven-bit counter. `setResetParameters()` honours it,
writing `writeLineCounter(BringUpDivider, BringUpLineDoubled)` beside the clock.
`applyInputSelection()` installs the clock alone, and
`VideoPath::inputTimingsChanged()` then re-applies the divider in force carrying
`lineDoubled_`, the PREVIOUS source's scan.

The visible result is two to four `if line counter: 2506 does not fit, holding
N` lines per selection. Nothing is corrupted, because `writeLineCounter()`
refuses a counter that does not fit and leaves both registers alone -- the pair
stays agreeing. But the refusal is the block's line being sized by a scan that
was never the reference clock's, and the correct value arrives only on the paths
that happen to run the teardown.
