# The coast lengths have two owners, and the wrong one doubles the line count

On a PAL 576i source the sync processor counts 622 lines instead of 311 when
`SP_PRE_COAST`/`SP_POST_COAST` hold 4/7, and 314 when they hold 7/3. Both pairs
are written by the firmware, on different schedules, and neither writer knows
about the other.

The picture is scrambled — torn horizontal noise across the whole raster —
while every clock reading is self-consistent.

## The measurement

Wii on `ypbpr`, PAL 576i, sync on green. The pair written by hand against the
live state, nothing else touched:

```
before              coast 4/7   vt 622   md 1124   ht 1124
coast 7/3 by hand   coast 7/3   vt 315   md 1124   ht 1124
coast 7/3 by hand   coast 7/3   vt 314   md 1124   ht 1124
coast 4/7 back      coast 4/7   vt 622   md 1124   ht 1124
```

Immediate in both directions. The coast pair **causes** the count; it does not
merely correlate with it.

**This is not the unlocked-PLL fault.** `STATUS_SYNC_PROC_HTOTAL` equals
`PLLAD_MD` throughout, so the divider is latched and the ADC PLL is locked.
`docs/investigations/an-unlocked-adc-pll-is-invisible-in-the-registers.md` is a
different state, where the two disagree.

## Why 622

The coast is what stops the sync processor counting through the vertical
interval. A 576i source puts equalisation and serration pulses either side of
it, at twice line rate. Coasted far enough — 7 lines before, 3 after — those
are skipped and the count is the frame. Coasted 4 and 7 they are counted, and a
count of roughly twice the lines comes out.

So 622 is not a different source. It is the same source with the serrations
counted as lines.

## The two writers

| writer | pair | when |
|---|---|---|
| `SyncProcessor::applyForSyncType(true)` | 4 / 7 | whenever the sync type is applied — which the probe does per source mode change |
| `updateSpDynamic()`, the `videoStandardInput <= 2` arm | 7 / 3 | on its own schedule inside the escalation ladder |

The second writer is gone. What follows is what it cost while it was there, and
what closing it fixed.

Whichever ran last is in force. Neither consults the other, and nothing
reconciles them.

**The state they leave is self-consistent and stable.** Parked at 4/7 the
engine measures 622, solves a divider of 1124 for it, and `HTOTAL` echoes 1124
back — locked, agreeing, and describing a source that is not there. It held that
way for 75 s of continuous sampling.

**And it is visibly a mixture.** Caught with coast 4/7 in force, `SP_DLT_REG`
read 0xC0 and `SP_H_PULSE_IGNOR` 26 — both from the `<= 2` arm, which had
plainly run and then had two of its four registers overwritten. One
configuration, two authors.

## What it costs upstream of the picture

The engine re-solves off the count, so the count flipping flips the solve:
`PLLAD_MD` alternates 2250 and 1124 as the pair alternates, and the framing and
raster follow. Sampled over three minutes the count ran 310, 314, 315, 317, 318,
319 and 622 with the divider halving and doubling under it.

`Geometry::sourceState()` reports `absent` and `acquired` alternately through
this, correctly — the source really is failing its steadiness run. The run is
doing its job; what it is measuring is being moved underneath it.

## `VPERIOD_IF` survives the coast the sync processor's count does not

Sampled from `loop()` at 35 Hz with `/freeze?on=1` holding the engine off, so
the pair stays parked and the divider does not chase the count. `SP_PRE_COAST`
and `SP_POST_COAST` written by hand and read back, 12 s at the bad pair between
two good ones, `STATUS_SYNC_PROC_HTOTAL` 2250 in every sample of all three
windows:

| coast | `STATUS_SYNC_PROC_VTOTAL` | `VPERIOD_IF` |
|---|---|---|
| 7/3 | 310 in 265/265 | 624 in 265/265 |
| 4/7 | 310x229, **607x91**, 328/329x30, and 352, 354, 531, 630, 631, 632, **862** | 619..628 only |
| 7/3 | 310 in 320/320 | 624 in 320/320 |

**Both measurements are disturbed, and the difference in magnitude is the whole
point.** The sync processor's count goes bimodal at roughly twice the source and
reaches 862; the input formatter's stays within **five counts of 624**, and in
363 samples of the bad pair it never once reports a doubled value.

So `VPERIOD_IF` is not immune to the coast, and a rule that assumes it constant
is wrong. What it is, is unable to make the error that matters. The two agree on
a good coast within a fixed deficit -- 310 against 624/2 -- and disagree by
nearly 300 on a bad one, which is separation enough to test against despite the
dither.

**Recovery is immediate and complete in both directions**, with nothing else
touched: the first sample after the good pair goes back is already 310, and
320 of 320 follow it.

### The coast surface is not reproducible, so no rule can be fitted to it

Bad-rate per pair, each from a full sampling-log run of ~300 samples at 35 Hz
with the engine frozen, so the pair holds and the divider does not chase:

| pair | run 1 | run 2 | run 3 |
|---|---|---|---|
| 7/3 | 0.0% | — | 0.0%, three times |
| 7/7 | 0.0% | — | — |
| 12/8 | 0.0% | — | — |
| 5/3 | — | **0.0% then 32.4%** | 0.0%, three times |
| 4/3 | **32.2%** | — | **0.0%** |
| 4/7 | **24.5%** | — | — |

**The same pair gives 0% and 32% in one run**, and the pair that failed a third
of the time in run 1 is clean throughout run 3. So the serration count is not a
function of the coast value, and a threshold, a boundary or an axis cannot be
read off it. Run 1 alone reads as *pre-coast is the axis, with the boundary
between 4 and 7*; run 3 refutes it.

What survives every run is narrower. **7/3 has never produced a serration
count** -- 0 in roughly 1500 samples across every run and window taken -- and
neither has 7/7 or 12/8. Bad states are real when they occur and reach a quarter
to a third of samples. Nothing predicts which.

**That is the case for checking at runtime rather than tuning a constant.** A
static pair cannot cover a fault that is not a function of the configuration,
whereas `SourceMeasurement::countIsSerrations()` identifies it whenever it
occurs, from a witness that cannot make the same error.

An escalation that searches for the lowest working pre-coast is **not** supported
by this data and should not be written from it: a search would settle on a value
that measured clean in one run and fails in the next.

### Why the freeze is required

With automation running, a hand-written pair is overwritten inside 1.5 s --
`Geometry::poll()` applies the sync type, which is now the pair's only owner.
`/freeze?on=1` gates both the sketch's paths and the engine, since `loop()`
passes `engineMayRun` to `Geometry::useRunGate()`. It also holds the divider
still, which is what separates the coast's effect on the measurement from the
engine's response to it.

## The two owners form a closed loop, and it never settles

The fight is self-sustaining on any source the coast can act on. Each link is
measured:

1. the coast pair changes the count -- the sweep below,
2. a changed count is `source moved: count`, which arms a solve,
3. the solve runs the sync-type probe, which applies the sync type,
4. `applyForSyncType()` writes its pair, and the count changes again.

So the fault feeds itself. Watched on `ypbpr` with nothing touched, over 20 s:
coast 7/3 -> 4/7 -> 7/3, `SP_H_PULSE_IGNOR` 107 -> 44 -> 107, `PLLAD_MD` 2250 ->
1124.

**The pair does not stay written long enough to measure.** Sweeping
`SP_PRE_COAST` x `SP_POST_COAST` over 0..10 x 0..10 on the Wii, reading the pair
back twice at 0.35 s and 0.55 s after writing it, **7 of 121 writes survived
both reads**. The counts seen across the sweep fall into families rather than
values:

| family | samples |
|---|---|
| 310 | 113 |
| 317..319 | 64 |
| 605..611 | 22 |
| 0 | 18 |
| 97..99 | 6 |

One source, unchanged throughout.

**So no sweep can choose the value while two owners remain.** Single ownership
is not the tidy-up that follows the measurement, it is what makes the
measurement possible. Establish one owner, then sweep.

## The coast does nothing on a source without serrations

The same sweep on the RISC PC at 320x256@50, `SYNC 1`, so composite sync with a
progressive RGB source: **`STATUS_SYNC_PROC_VTOTAL` read 308 at all 242 samples
across all 121 pairs.** The coast pair has no effect on the count at all.

That is the first link of the loop missing, and it is why this source acquires
in about eight seconds while the Wii never converges. A progressive source's
vertical sync carries no equalisation or serration pulses, so there is nothing
for the coast to skip and nothing for its value to change.

**Two consequences.** A coast value cannot be validated against a progressive
source -- every value scores identically -- so the RISC PC is a control for the
loop, not a reference for the value. And a coast rule derived from anything the
board measures has to be derived from something that varies with the vertical
interval, which no progressive source exercises.

## Composite sync reclassifies the source, and the count loses three lines

Two further measurements from the same run, on one machine in one mode with only
`*Configure Sync` moving:

- On separate sync the RISC PC is RGBHV and reads `STATUS_SYNC_PROC_VTOTAL` 311,
  its true count. On composite sync it lands in `updateSpDynamic()`'s
  `videoStandardInput <= 2` arm -- coast 7/3, `SP_DLT_REG` 0xC0,
  `SP_H_PULSE_IGNOR` 107 -- so **the sync type decides the video-standard
  classification** of an unchanged 311-line source.
- On composite sync the count is **308 against a true 311**, and the sweep shows
  the deficit is flat across every coast pair. `../sync-type-selection.md` carries
  the three-line deficit as untested; it is not the coast.

## How it is closed

`SyncProcessor` owns `SP_PRE_COAST` and `SP_POST_COAST` alone. The per-standard
ladder in `updateSpDynamic()` no longer writes them, and the pair is named --
`SerratedPreCoastLines` 7 and `SerratedPostCoastLines` 3 -- for what it decides,
which is whether the equalisation pulses land inside the count.

The value is 7/3 rather than 4/7 on two measurements: on the serrated source 7/3
counts the lines and 4/7 counts the serrations, and on the only progressive
source that reaches this path the coast does nothing at all, so there is nothing
the change can cost there.

**It is still not a value derived from something measured**, and that stays
open: nothing on this board measures the vertical interval, so a source whose
interval is shorter than PAL's has no rule to fall back on. What has changed is
that the question can be asked -- with one owner the pair stays where it is
written, which is what the sweep above could not achieve.

Standards 3 to 7 are HD component and have no source here, so collapsing their
7/6, 7/7 and 9/18 onto the sync type's pair is untested.

### What it fixed

Same build, one input switch apart:

| | before | after |
|---|---|---|
| convergence | none in 60 s | `acquired` 9.3 s after the switch |
| count | five families | 310 |
| coast | 7/3 and 4/7 alternating, unasked | 7/3, unmoved |
| `PLLAD_MD` | 2250 and 1124 alternating | 2250 |
| picture | scrambled | clean, full screen |

**The steadiness is measured on-device, not over HTTP.** `/samplinglog?ms=25`
over 30 s puts `STATUS_SYNC_PROC_VTOTAL` at **310 in 1050 of 1050 samples**,
`VPERIOD_IF` at 624 in all of them, `HPERIOD_IF` at 431/430, and the latched
interrupt byte at 0 throughout — so nothing is arming a solve. The RISC PC
control reads 311 in 1043 of 1043.

That distinction matters here, because HTTP point reads of the same settled
source returned 149, 160, 230 and 299 among the 310s and were read as the source
failing to hold. The on-device log does not corroborate them.
`../../CLAUDE.md`, "an HTTP read is not a sample".

## What it is not

- **Not a regression from the sync-watcher retirement.** Both writers pre-date
  it. `SyncProcessor::applySeparationThresholds()` covers only the
  `videoStandardInput >= 13` arm and writes 4/7 exactly as the raw code it
  replaced did; the `<= 2` arm is untouched.
- **Not the ADC clock fault**, as above: the divider echoes.
- **Not the source settling.** The state held at 4/7 for 75 s of continuous
  sampling, and the hand-written pair moved it instantly in both directions. A
  source acquires in about eight seconds when the loop above is not running.
  `../bench-sources.md`.

## See also

- [../video-source-acquisition.md](../video-source-acquisition.md), step 5
- [the-sketch-hunts-while-the-engine-is-locked.md](the-sketch-hunts-while-the-engine-is-locked.md)
  — the same two-owner shape on `SP_H_PULSE_IGNOR`
