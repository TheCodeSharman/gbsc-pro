# The ADC PLL comes out of lock with every config register unchanged

The state the no-sync gate lands in when the sketch's escalation stops running
against a source the engine can measure. The ADC PLL's loop is open and its VCO
free-runs, so the sample clock is unrelated to `PLLAD_MD` while every register
that configures it reads back correct.

## The measurement

Bench RiscPC on `vga`, 320x256@50, after a sync-type round trip, with both
branches of `runSyncWatcher()` keyed on `Geometry::sourceIsPresent()`. Every
field read by name, bad state and good state, the good one reached with `/sc?~`:

| | bad | good |
|---|---|---|
| `PLLAD_MD` / `KS` / `ICP` / `CKOS` | 2250 / 2 / 5 / 0 | **identical** |
| `PLLAD_FS` | 1 here, 0 in a later instance | 1 -- see below |
| `PLLAD_VCORST` / `PDZ` / `LEN` | 0 / 1 / 1 | **identical** |
| `ADC_CLK_ICLK1X` / `ICLK2X` / `DEC1_BYPS` / `DEC2_BYPS` | 1 / 1 / 0 / 0 | **identical** |
| `ADC_POWDZ` | 1 | **identical** |
| `IF_HSYNC_RST` / `SP_RT_HS_SP` | 1125 / 2092 | **identical** |
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 311 |
| **`STATUS_SYNC_PROC_HTOTAL`** | **3234** | **2250** |
| **`STATUS_MISC_PLLAD_LOCK`** | **0** | **1** |

The picture is scrambled -- torn horizontal noise across the whole raster.

**Only the two status readings differ, and they are the two the chip measures
rather than the ones the firmware writes.** So the fault is a sequence, not a
state, and a snapshot diff against a known-good dump reports nothing.

## Both sync-processor measurements scale by ONE factor

Read against the live bad state, four samples. The factor here is 1.444; a later
instance settles at 1.058, and `PLLAD_FS` is what separates them -- see below,
and do not read either number as the signature:

```
ht=3248  hlow=230  ratio=1.444  vt=311  md=2250  sog=0 ext=0 loop=1 rths=2092 hsact=1
ht=3239  hlow=204  ratio=1.440  vt=311  md=2250  ...
ht=3249  hlow=230  ratio=1.444  vt=311  md=2250  ...
ht=3241  hlow=210  ratio=1.440  vt=311  md=2250  ...
```

**Both sync-processor measurements scale by ONE factor.** The line is 1.444x the
divider, and `STATUS_SYNC_PROC_HLOW_LEN` reads 230 where a healthy unit reads
159 -- and 159 x 1.444 = 230. The sync pulse has not changed duration; it is
being counted in faster clocks.

So this is a clock-rate fault and not a sync fault. `STATUS_SYNC_PROC_HSACT` is
1, the vertical count is a correct 311, and every register in the sync path
reads what a healthy unit reads.

**The line rate does not change across the round trip**: the csync leg is 308
lines at 50.56 Hz and the return leg 311 at 50.08, both 15625 Hz. So the samples
per line should be unchanged, and the divider the engine solves is the same 2250
either side.

## What it is not

- **Not a missing latch.** `PLLAD_LAT` pulsed low then high by hand against the
  live bad state: `HTOTAL` 3195 -> 3235 -> 3224 over ten seconds, lock still 0.
- **Not the phase sweep.** `optimizePhaseSP()` withheld from the maintenance
  branch entirely: three of three round trips still settled at ~3250.
- **Not the VCO reset.** `resetPLLAD()`'s own sequence by hand against the live
  bad state -- `PLLAD_VCORST` 1, `PLLAD_PDZ` 1, latch, `PLLAD_VCORST` 0, latch --
  left the ratio at 1.416, 1.439, 1.433 over the following sixteen seconds.
- **Not `HPERIOD_IF` railing**, which is its own fault and reads 511 in the
  *good* state here, with a clean full-screen picture.
- **Not `PLLAD_FS`**, which differs between the states and only sets where the
  free-running VCO sits. FS=0 on a locked unit does not open the loop.
- **Not the sync-type probe.** Probe durations are drawn from the same
  distribution in passing and failing runs -- 2 ms, 3 ms, and a 1000 ms timeout
  on the csync leg, in both.
- **Not how busy the return leg is.** A passing run and a failing run both show
  ~25 probes, ~10 solves and ~100 sampling lines over the same window.
- **Not the engine's rate measurement.** One failing run rejected 11 readings
  and accepted 20, all at 15625; a passing run rejected 70 and accepted 39
  spread over 15580..15685.

## What reaches it

`/sc?~` -- `goLowPowerWithInputDetection()` -- clears it every time.

## How it is provoked

It appears only when the sketch's escalation stops running against a source the
engine can measure. On a build where `runSyncWatcher()`'s no-sync branch is
keyed on `getVideoMode()`, that branch runs continuously on a scaling RGBHV
source -- the classification returns 0 there for ever -- and `HTOTAL` settles at
2250 in five of five round trips. Keyed on the engine, the branch stops and
`HTOTAL` settles near 3250 in three of three.

**So something the escalation does on a cadence keeps the ADC PLL at the right
ratio**, and which rung is unknown. The obvious candidate is removed: putting a
sync separator level in force latches the ADC PLL, and the `% 150` rung does
that through `optimizeSogLevel()` -- but a latch by hand does not recover the
state.

**The `% 413` ADC input toggle is NOT the candidate, and cannot be**, on this
bench or any unit with an input selected: it is gated on
`detectionMayChangeInput()`, which is `!VideoSourceSelection::chosen(Info)`. Measured over
a 125 s hunt with `ADC_INPUT_SEL` read 35 times, it never moved.

**AND IT IS NOT THE PLL FAILING TO LOCK EITHER.**
`docs/investigations/adc-pll-lock-range.md` swept the VCO from 100 to 180 MHz
across two `PLLAD_KS` bands and records that `STATUS_SYNC_PROC_HTOTAL` echoes
the divider **at every point in both, locked or not**. This configuration --
`PLLAD_MD` 2250 at 15625 Hz, so CKO 35.2 MHz, `KS` 2, VCO 140.6 MHz -- sits at
the row measured at 98% lock. So the echo does not depend on lock, and losing it
is a different fault from anything that sweep produced.

**The write-after-latch trap is ruled out by reading the code.**
`Adc::applySampleRate()` writes `PLLAD_MD`, then `PLLAD_KS`, then the
oversampling, and calls `latch()` last. The order is right on the engine's path.

## The ADC PLL is free-running: the divider does not reach it

Measured with the fault standing, `/samplinglog?low=1800&high=2900&step=200&dwell=500`
walking `PLLAD_MD` through `Adc::applySampleRate()` so every step latches, with
the same walk on a healthy unit as the control. The settled median of
`STATUS_SYNC_PROC_HTOTAL` at each step:

| divider | faulted `HTOTAL` | ratio | healthy `HTOTAL` | ratio |
|---|---|---|---|---|
| 1800 | 2379 | 1.322 | 1800 | 1.000 |
| 2000 | 2379 | 1.190 | 2000 | 1.000 |
| 2200 | 2380 | 1.082 | 2200 | 1.000 |
| 2400 | 2380 | 0.992 | 2400 | 1.000 |
| 2800 | noise | -- | 2800 | 1.000 |
| 2900 | noise | -- | 2900 | 1.000 |

**Faulted, the counter does not move at all across a 33% span of the divider.**
It sits on the value it held at 2250 before the walk started, and the ratio
column only changes because the denominator does -- at 2400 it passes through
1.000 while nothing about the state has changed. Above 2400 it goes to noise
spanning 15..4095, which is the walk driving the VCO out of range rather than
anything tracking.

Healthy it is 1.000 at every step, **including 2800 and 2900 where
`STATUS_MISC_PLLAD_LOCK` reads 0** -- which is `docs/investigations/adc-pll-lock-range.md`'s
finding reproduced here, and what makes the faulted column mean something: the
echo does not need the lock bit, so losing the echo is not the lock bit going
out.

So the ADC PLL is not running on a wrong divider. It is not running on the
divider at all: the loop is open and the VCO free-runs. `PLLAD_MD` reaches the
register, `PLLAD_LAT` latches it, and the clock the sync processor counts is
indifferent to both. The walk restores the entry divider at the end, and the
fault stands through the whole of it unchanged.

## `PLLAD_FS` sets where it free-runs, and is not the cause

`PLLAD_FS` -- the VCO gain selection -- is **1 in the healthy state and 0 in the
faulted one**, which is the one configuration difference between them. It does
not survive as a cause, and what it explains instead is the fault's magnitude.

Written to 1 and latched against the live fault, with a latch pulse alone first
as the control:

```
before                FS=0  ht=2379  hlow=168  vt=311  lock=0  ratio=1.0573
latch alone +3s       FS=0  ht= 404  hlow=265  vt=311  lock=0
latch alone +8s       FS=0  ht=2379  hlow=168  vt=311  lock=0  ratio=1.0573
FS=1 + latch          FS=1  ht=3251  hlow=213  vt=311  lock=0  ratio=1.4449
FS=1 + latch  +10s    FS=1  ht=3244  hlow=232  vt=311  lock=0  ratio=1.4418
```

**Putting the healthy value in makes the number worse, not better**, and the
lock bit never comes back. A free-running VCO's frequency is set by its gain
band and not by the feedback divider, so this is the same fault at two settings
of that band -- 1.058 at FS 0 and 1.444 at FS 1.

**So the magnitude is not a signature and must not be read as one.** Two
reproductions on separate runs both settle on exactly 2380 against 2250 at FS 0,
and the earlier records at ~3250 are the same state at FS 1. What identifies the
fault is that both sync-processor measurements scale by one factor and the
divider does not move it.

The healthy control closes FS as a cause outright:

```
healthy, FS=1         ht=2250  hlow=159  vt=311  lock=1  ratio=1.0000
healthy + FS=0 +2s    ht=2250  hlow=159  vt=311  lock=0  ratio=1.0000
healthy + FS=0 +20s   ht=2250  hlow=159  vt=311  lock=0  ratio=1.0000
healthy + FS=1 back   ht=2250  hlow=159  vt=311  lock=1  ratio=1.0000
```

**FS=0 on a locked unit does not open the loop.** The lock bit follows FS and
the echo does not, which is one more reason not to judge the state on that bit.

`setResetParameters()` is the one writer on the scaling path that puts FS to 0,
alongside `PLLAD_ICP` 0 and `PLLAD_MD` 0x700; the faulted state has ICP back at
5 and MD back at 2250 with FS still 0, so whatever restores the group after a
reset does not restore FS. The remaining writers are out of the path -- the
`updateCoastPosition()` pair is gated on a csync source under 322 lines, and the
`HPLLState` ladder on `rgbhvBypass()`.

## What the walk needs to be a walk

`SamplingLog::sweep()` picked the ADC post divider from `HPERIOD_IF`, which
reads 10 in the faulted state and 511 in the healthy one on this bench. Derived
from 10 that is a 613 kHz line, which puts `PLLAD_KS` at 0 and collapses the
oversampling from 4 to 1, so every step would have rewritten the whole clock
group and the walk could not have answered a question about `PLLAD_MD`. It takes
the engine's held line rate instead.

## What does NOT correlate with it, across nine runs

Every run with the gate wired, anchored on each file's own `SYNC 0` so the
windows match. Seven failed, two did not.

| run | outcome | interrupts | solves | probes |
|---|---|---|---|---|
| gate build x3 | FAILED | 1 | 1..3 | 2..5 |
| three-state build x3 | FAILED | 9..11 | 9..12 | 23..27 |
| console trace only | FAILED | 10 | 10 | 27 |
| sampling log running | ok | 1 | 1 | 2 |
| fast HTTP poll | ok | 0 | 1 | 3 |

**A run that failed with one interrupt, one solve and two probes is
indistinguishable from a run that passed with one interrupt, one solve and two
probes.** So the re-solve churn does not drive it, and neither does the probe
count -- both were leading candidates, and both are dead.

**Two instruments appear to suppress it and neither is understood.** Reading
`STATUS_0F` in the sampling log does NOT consume the source-disturbed latch --
`Interrupts::takeSourceDisturbed()` acknowledges explicitly, and a latch cleared
within one 20 ms watcher pass is missed by a 25 ms sampler anyway, which is why
the log reads 0 in all 3139 samples. With seven failures in nine runs, two
passes is not yet evidence of suppression rather than of chance.

## The instrument, because the obvious one does not work

**A register poll fast enough to catch this changes the outcome.** Polling two
fields at ~50 Hz through a round trip turned three failures in three runs into a
pass, and `STATUS_MISC_PLLAD_LOCK` was seen flickering 0/1 within 50 ms in the
HEALTHY state -- so it is a weak witness read as single samples, and the
sustained `HTOTAL` divergence is the reliable one.

The engine's idle pass already reads both halves of the presence answer, so it
logs the transition and the numbers behind it and costs no extra bus traffic:

```
source UNLOCKED: 311 lines, 3251 samples against divider 2250
```

That line lands in the console beside `source moved` and `own V sync`, which is
what made the sequence legible at all.

## Why it blocks the no-sync gate

`Geometry::sourceState()` reports this correctly -- `unlocked`, with `present`
false -- so the gate does let the escalation run once the state is reached. The
escalation then cannot recover it. Withholding the escalation causes a fault the
escalation cannot repair, which is why the gate cannot land on this alone.
`docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md`
