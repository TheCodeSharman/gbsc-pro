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

## Why this is step 5's, not a fix to make here

The coast lengths around the vertical interval are `applyForSyncType()`'s by
design — `SyncProcessor.h` says so, and they follow the sync type. The `<= 2`
arm writing them as well is the per-standard ladder that step 5 of
`docs/retiring-the-sync-watcher.md` exists to collapse.

**Picking a winner is not the fix.** 4/7 measures this source wrongly and 7/3
measures it correctly, but 7/3 is upstream's tuning for one standard and the
right answer is not "always use the SD pair" — a source whose vertical interval
is shorter wants less coast, and nothing here measures the interval. What the
step needs is one owner and a value derived from something measured, and this
is the evidence for how much that is worth.

## What it is not

- **Not a regression from the sync-watcher retirement.** Both writers pre-date
  it. `SyncProcessor::applySeparationThresholds()` covers only the
  `videoStandardInput >= 13` arm and writes 4/7 exactly as the raw code it
  replaced did; the `<= 2` arm is untouched.
- **Not the ADC clock fault**, as above: the divider echoes.
- **Not the source settling.** The state held at 4/7 for 75 s of continuous
  sampling, and the hand-written pair moved it instantly in both directions.

## See also

- [../retiring-the-sync-watcher.md](../retiring-the-sync-watcher.md), step 5
- [the-sketch-hunts-while-the-engine-is-locked.md](the-sketch-hunts-while-the-engine-is-locked.md)
  — the same two-owner shape on `SP_H_PULSE_IGNOR`
