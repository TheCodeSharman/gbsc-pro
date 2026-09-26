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

## The cause: the input formatter is left describing two different scans

**The doubled samples land exactly in a window where the block's scan registers
disagree with each other.** Read against the console in one window, polling the
five registers that carry the scan across a `vga` -> `ypbpr` selection:

| when | `PRGRSV` | `LD_RAM_BYPS` | `LD_SEL_PROV` | `HS_DEC_FACTOR` | `HSYNC_RST` | `PLLAD_MD` | the rate read |
|---|---|---|---|---|---|---|---|
| settled, before | 1 | 1 | 1 | 0 | 1438 | 1438 | 60.31, right |
| reference line in | 1 | 1 | 1 | **1** | **1253** | 2506 | -- |
| the measurement | 1 | 1 | 1 | **1** | **1253** | 2506 | **119.87, twice** |
| solved | 1 | 1 | 1 | 0 | 1448 | 1448 | 59.93, right |

The three path registers say PROGRESSIVE while the decimation and the counter
say DOUBLED. The block is told an IF unit is two ADC samples with the
progressive path in circuit, and its vertical then asserts twice per frame.

**Two writers put it there, each writing part of the scan.**
`setResetParameters()` calls `writeLineCounter(BringUpDivider,
BringUpLineDoubled)`, which writes the counter and the decimation and nothing
else -- the reference line cannot be represented undoubled, 2506 truncating to
458 in an eleven-bit counter, so it has to be doubled. Then
`VideoPath::solveLineDoubling()` decides the arriving source is progressive and
calls `applyLineDoubling(false, ...)`, which writes the three path registers and
then asks `writeLineCounter(2506, false)` to re-size -- which **refuses**, 2506
not fitting, and leaves the decimation and the counter on the doubled line.

So each writer is individually defensible and the pair of them leaves a scan
that is half one thing and half the other. It is the same shape as
`two-owners-of-the-coast-lengths-double-the-count.md`: one configuration, two
authors, and a self-consistent-looking register dump.

**The input asymmetry follows from it.** On separate sync the vertical the test
bus carries comes from the dedicated VSync pin rather than from the input
formatter's own separator, so a mixed scan does not corrupt it -- which is the
same reason `VPERIOD_IF` is debris on that input.

## A fix has to apply the scan and the divider together, and that is the hard part

**Tried and reverted**, so the next attempt does not repeat it.

Making the reference line go in as a whole scan is correct and not sufficient.
`InputFormatter::carryReferenceLine()` -- `applyLineDoubling(true, component)`
followed by `writeLineCounter(divider, true)` -- does make the reference window
consistent, measured: the block reads `PRGRSV` 0, `LD_RAM_BYPS` 0, `LD_SEL_PROV`
0 with the doubled counter, where before it read the mixture above. But the
mixture is then **re-created** by `applyLineDoubling(false, ...)` a moment later,
and the doubled rate is read in that second window instead. Measured, the divider
churn is unchanged: still 694, then 2200, then 1448.

Refusing the scan when its line will not fit the counter deadlocks. The divider
install on the output path is gated on the scan having changed, so a refused
scan stops the divider moving and the scan can then never come to fit. Four host
suites catch it.

**What the shape has to be**: the scan is decided from the line count, the
divider is derived from the scan, and the two are then applied TOGETHER, so the
counter is always sized for the divider going in beside it. Today
`solveLineDoubling()` both decides and applies, and `applySampling()` applies the
divider separately -- and between them is the window this fault lives in.

**Do not judge an attempt on one run, and let the unit settle after a flash.**
`carryReferenceLine` measured 1 of 8 on the trial below immediately after its
flash and 8 of 8 on the identical build once settled, which read as a fix and was
not one. It also cost about a second of acquisition on `ypbpr`, 5.5..6.3 s
against 4.9..5.1 s.

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
hides it, the case that removes the guard, the cost in dividers, and the mixed
scan that causes it. Not established: the chip-internal reason a mixed scan makes
the vertical assert exactly twice rather than at some other ratio -- which is not
needed to fix it, the mixture being wrong whatever the ratio.

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
