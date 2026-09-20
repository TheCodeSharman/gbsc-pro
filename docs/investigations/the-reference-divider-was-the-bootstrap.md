# The reference divider was the bootstrap, not a reference

The engine used to install two sampling clocks per mode change: a fixed
**reference** divider before the measurement, and the **operating** divider
after it. The reference was assessed as obsolete -- the count, the field rate and
`HPERIOD_IF` all survive a divider the ADC PLL cannot lock to, so the only thing
left needing it was that *some* divider be latched when the duty is read, and the
operating divider is equally latched and slightly finer.

Removing it found four things: a defect the two clocks were causing, two jobs
nobody had noticed the reference was doing, and a trap the removal opened.

## The duty was counted in the wrong units

`STATUS_SYNC_PROC_HLOW_LEN` counts ADC samples, so a duty is that count divided
by the divider in force. With two clocks per mode change the duty was read
through the **reference** and then spent on a capture window sized in the
**operating** divider's units.

On the bench source, 320x256@50 line-doubled into 1080p:

| | divider | duty from 178 samples |
|---|---|---|
| reference | 2250 | 7.91% |
| operating | 2506 | 7.10% |
| the mode file states | 36 of 512 | 7.03% |

An 11% error in the quantity the capture window's head is placed from. The
settled picture recovers, because the source is re-measured on later passes and
converges; what does not recover is the framing during and just after a
transition, which is where the capturable region was measured wandering
1109..1147 on a source that never moved.

**The fix is ordering.** The measurement is two halves with a hardware write
between them: `measureRate()` reads the count and the field rate through
whatever clock is in force, the caller installs the divider that rate asks for,
and `measureDuty()` reads the pulse through it. The caller owns the install
because the divider depends on the chosen output mode, which
`SourceMeasurement` knows nothing about.

It costs one extra field-rate sample per mode change -- the install arms the
latch settle, and the pass after that settle takes its own reading on the way to
the duty.

## The count has to be corrected, everywhere

The sync processor counts in ADC clocks, so a divider far from the source's line
puts the PLL outside its lock range and it locks to every kth hsync instead.
Measured on the bench, a 311-line source reads **155** with the divider left at
another mode's 1124, and the samples per line read twice the divider as the
evidence of the multiple.

The count was read three different ways: corrected against the divider in force
for the scan mode, and raw in both the steadiness gate and the rate measurement.
With a reference clock in front of the measurement that difference never showed,
because the reference had already replaced the offending divider. Without one it
is a deadlock: the divider is sized from the rate, the rate from the count, and
the count is wrong because of the divider.

All three now go through `SourceMeasurement::readSourceLines()`.

## A parked PLL can be measured through by nothing

`setResetParameters()` used to park `PLLAD_MD` at 0x700 and hold the PLL in
reset, so the ADC was not clocking at all. Measured in that state on the
bench source:

```
PLLAD_MD                     1792     (the parked value)
STATUS_SYNC_PROC_VTOTAL       167     (a 311-line source)
STATUS_SYNC_PROC_HTOTAL      3829     no whole multiple of 1792
STATUS_SYNC_PROC_HLOW_LEN    3368     88% of the line
HPERIOD_IF                    511     railed
```

3829 is 2.14 times 1792, so the correction above finds no multiple and the count
stands at 167, which is no source. Nothing is measured, so no divider is chosen,
so nothing is ever measured. The recovery ladder cycled every ~7 seconds
indefinitely with the picture dark, `own V sync: yes after 2ms` on every pass.

**This is what the reference divider was really for.** It was sized from
`lineCount x NominalFieldRateHz` where no rate had been measured, and that guess
is what got the ADC clocking in the first place.

### The fix is a reset state, not a guess

A guess about the source is the wrong tool, and measuring how wrong settles it.
`recommendedDivider()` parks CKO at `RecommendedPercent` of a crossover row --
2% under the 40 MHz edge at oversample 4 -- and `applySampleRate()` then reads
the post divider and the VCO gain off that same assumed CKO. The actual CKO is
`divider x trueRate`, so the row survives a true rate that is LOWER than assumed
and is wrong the moment it is more than 2% higher. At bootstrap the count is
wrong too: the parked clock reported 167 against 311, so the assumed rate was
`167 x 60 = 10020` against a true 15625, **56% high**. The guess reliably lands
on the wrong row, and only the ladder's retries recover it.

So the guess is gone and the state the chip is RESET INTO is a working clock
instead. `Adc::applyResetParameters()` applies a stated pair rather than parking
`PLLAD_MD` and declaring nothing in force:

```
BringUpDivider     2506
BringUpLineRateHz  15625      CKO 39.2 MHz -> post divider 2 -> VCO 156.6 MHz
```

Both halves of the pair are measured rather than nominal. That VCO on high gain
is the state the bench unit locks in; the arithmetically tidier 1792 puts it at
112 MHz on low gain, where nothing has measured whether the PLL holds. And the
rate is the LOWEST line the part is expected to carry, so every faster source
needs the PLL to divide rather than multiply -- asked for a frequency under its
lock range it locks to every kth hsync, and the count correction above recovers
k up to `LinesPerCountMax`, which reaches 62.5 kHz.

**It belongs there rather than in a bring-up block**, which was the first
proposal: `BringUp::init()` does run after the last `setResetParameters()` at
boot, deliberately, so a value written there would survive -- but
`goLowPowerWithInputDetection()` calls `setResetParameters()` again with no
bring-up after it, and that is what `/sc?~` and a failed detection both run. One
owner, at the point the parking happened, needs no ordering rule to remember.

`VideoPath::inputTimingsChanged()` was already re-applying
`Adc::dividerInForce()` on every mode change, for exactly this reason -- its
comment says a load leaves the PLL on the bring-up's crossover row. With the
reset leaving nothing in force that re-apply was a no-op, which is what starved
the state.

## A quantised divider oscillates under a jittering rate

Every install re-latches the ADC PLL and restarts the settle, and the duty is
read after that settle -- so a clock re-installed on every pass leaves a source
that never finishes measuring. The divider is quantised, so a field rate
wandering by hundredths lands either side of a step:

```
sampling: 311 lines x 50.08 Hz -> line rate 15625
sampling: rate 15625 doubled 1 -> divider 2506
sampling: 311 lines x 50.06 Hz -> line rate 15619
sampling: rate 15619 doubled 1 -> divider 2508
sampling: 311 lines x 50.08 Hz -> line rate 15625
sampling: rate 15625 doubled 1 -> divider 2506
...
duty: 230 pulse / 2506 divider, htotal 3252, positive, UNLOCKED
```

Measured on the composite-sync source, for as long as it was left.

A guard comparing the recomputed divider for **equality** does not catch this,
because the recomputed value is what moves. `VideoPath::installSampling()`
therefore compares both quantities within a tolerance: the divider against the
one in force, and the rate against the one that chose it. The rate is compared
as well as the divider because what the rate is for is the post divider row and
the VCO gain, which are a function of the divider times the rate -- so a divider
that did not move can still want a different row.

## What it measures, against the two-clock build

Same bench, same source, same cable; the baseline is one build back.

| | two clocks | one clock |
|---|---|---|
| restarts acquiring, `ch` 1145 | 3 of 3 | **8 of 8** |
| composite sync settles | 39-40 s | **10-11 s** |
| a parked ADC recovers | never, without a restart | **yes, ~22 s** |
| the Wii at 480p on `ypbpr` | 15.2 s | **7.2 s**, `ch` 1493 in 3 of 3 |
| separate sync after composite | never returns | never returns |

With the bring-up clock in place of the guess, the same bench gives **29
acquisitions of 32 restarts**, `ch` 1145 in every one of them, median 8 s and a
range of 4.2 to 18.3 s. The three misses are not the deadlock and not the
divider: measured on one, `PLLAD_MD` 2506 against `STATUS_SYNC_PROC_HTOTAL` 2506
and `STATUS_SYNC_PROC_VTOTAL` 311 -- the clock right and the source counted
exactly -- with the held rate at 0, which is the field rate failing to measure.
The ladder's `RestartSamplingClock` rung is the designed answer to that and
fires at pass 60, about 1.2 s in.

**The miss is pre-existing, and measuring it settled a worry rather than
confirming one.** The same 16-restart protocol run against the build before any
of this work:

| | before | after |
|---|---|---|
| acquired | 14 of 16 | 29 of 32 |
| median, worst | 11.2 s, 33.3 s | 8.0 s, 18.3 s |
| what a failure looks like | `PLLAD_MD` **1792**, parked | `PLLAD_MD` 2506, `HTOTAL` 2506, `VTOTAL` 311 |

The rates are indistinguishable at these sample sizes -- 12.5% against 9.4% --
but the failure KIND is not. Before, both misses were the parked-divider
deadlock, which needs a restart and which a sync-type round trip can leave
standing indefinitely. After, the clock is correct and the source is counted
exactly, and only the field rate is missing, which is a state the ladder's
`RestartSamplingClock` rung can act on.

The settled state is the same state: a register diff across the change, on a
working picture either side, differs in **four fields** -- `ADC_SOGCTRL`,
`PA_ADC_BYPSZ`, `PA_SP_BYPSZ`, `PA_SP_S` -- all of them separator and phase
values the sync-on-green tuning walks. No geometry, colour, clamp or divider
register moves. The change is to the transient, not to the solve.

## The last guess: an unmeasured source neither enters pass-through nor leaves it

`HdBypass::suitsSource()` stood a nominal 60 Hz in for a field rate that had
not been measured. It looks like it decides entry from a guess, and the read
that made it look load-bearing is that the same predicate also decides whether
to STAY: bypass measures nothing of its own, so the held line rate names the
mode bypass was entered on, and what notices a source slowing underneath is the
LIVE count times the LIVE field rate. Refusing there reads as abandoning a
working picture on one dropped sample.

**Both halves are answered by where the decision is taken, not by what the
values are.** The rule is that an unmeasured source neither enters pass-through
nor leaves it -- "no measurement" means "change nothing", in both directions --
and `runPass()` already enforces it: the enter branch and the leave branch both
sit after `measureSource()` returned true. That gate is strict about the field
rate in particular, because `measureLineRate()` only succeeds when
`VideoSignal::fieldRateIsSource()` holds.

So the stand-in was unreachable, and a refusal is what the function now answers
if a caller ever arrives without a reading. Guarded at both ends:
`test_video_source_acquisition.cpp` has "pass-through is refused until the
source has been measured" and "a pass-through source is not dropped because a
measurement failed", so moving the route decision above the measurement gate
fails a test rather than a panel.

**No guess is left in the engine.** Every divider comes from a measured rate,
the reset state is a stated clock, and the route is decided on measurements
alone.

## What is still open

**The return from composite sync to separate sync does not work on either
build**, and it is not this change's. `../sync-type-selection.md` is the
mechanism; `../known-issues.md` carries it.
