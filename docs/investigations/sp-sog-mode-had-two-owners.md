# SP_SOG_MODE had two owners, and a separate-sync source never acquired

A separate-sync RGBHV source can sit for ever in the recovery ladder with the
sync processor counting it perfectly. `unmeasuredPasses()` climbs through the
whole ladder and restarts, `acquiredPasses()` stays 0, `/geometry` reports
`state: absent`, and no register is wrong to look at.

## The chain

`SP_SOG_MODE` is written from two places. `SyncProcessor::applyForSyncType()`
follows the measured sync type and writes 0 on a separate-sync source;
`prepareSyncProcessor()` wrote 1 unconditionally on every preset load. The
second ran last, and the first only ran inside `if (scalingRgbhv())` — so on
every other route nothing applied the sync type at all.

With SOG mode set on a source that carries its own H and V:

1. `STATUS_SYNC_PROC_VTOTAL` reads 0.
2. Detection's line-count wait (`DetectCountWaitMs`, 600 ms) cannot pass, and
   reports `sync found but no line count, not taking it`.
3. No input is taken, so no preset loads, so `VideoPath::outputMode()` stays
   null.
4. `VideoSourceAcquisition::sourceMoved()` returns at its first gate,
   `mode == 0`, so nothing ever arms a solve.
5. `runPass()` takes its early return every pass and `SourceMeasurement::measureRate()`
   is never called.

Step 4 is the part no dump shows. The engine's only other armer is a preset
load, and `solvedLines_` — the gate behind `mode == 0` — is written only by a
solve that an arm has to open. An engine that has never solved cannot arm
itself, so a single refused detection commits the whole boot.

## The measurement

800x600@60 on `vga`, separate sync, one window, nothing else touched:

| | `VTOTAL` | `HTOTAL` | `SP_PRE_COAST` | `SP_POST_COAST` | `PLLAD_MD` | `PLLAD_LOCK` | `SP_SOG_MODE` |
|---|---|---|---|---|---|---|---|
| before a preset load | 627 | 1004 | 0 | 0 | 2506 | 0 | 0 |
| after it | **0** | 0 | 9 | 18 | 2506 | 0 | **1** |

Writing `SP_SOG_MODE` 0 by hand recovers the whole path in about three seconds:
detection reports `syncFound 1`, the DACs come up, `measureRate()` runs,
`sampling: 627 lines x 60.20 Hz -> line rate 37809`, and the duty reads
176/1436 = 0.1226 against VESA's 0.1212.

Over 2549 detection passes in the failing state, an instrumented build read
`changingMode()` 0, `solveDeferred()` 0, `solvedLines_` 0 and `outputMode()`
null on every single pass, and the polarity normalisation inside `measureRate()`
logged nothing at all.

## Three models this refutes

**The hsync polarity.** `STATUS_SYNC_PROC_HSPOL` is 1 on this source and
`SourceTiming::lookUp()` cannot match the complement's 0.879 duty, so an
un-normalised polarity looks like the cause. It is not: measured in one window,
`SP_HS_INV_REG` 0 gives `VTOTAL` 627 and setting it to 1 gives 627 across twelve
samples over 48 s. The register that misleads here is the pairing, not the
polarity — a reading taken before a preset load and one taken after it are two
states, not two instruments, and `VTOTAL` recovers on its own once the ladder
reaches its sync-processor rung.

**The coast pair.** An unconfigured sync processor misreads `STATUS_SYNC_PROC_HTOTAL`,
so `SP_PRE_COAST`/`SP_POST_COAST` at 0/0 look like the fault. The direction is
backwards: 0/0 is what `applyForSyncType()` writes for separate sync and is the
state that counts 627. The 9/18 that replaces it is the csync configuration.

**The sampling clock.** `PLLAD_MD` is 2506 and `STATUS_MISC_PLLAD_LOCK` 0 on
both sides of the transition, so neither moved when the count died.

## What keeps it fixed

`SyncProcessor::prepare()` holds the per-load setup that does not follow the
sync type, and writes neither `SP_SOG_MODE` nor `SP_NO_COAST_REG`.
`applyForSyncType()` is called on every route rather than only the
scaling-RGBHV one, and runs before `prepare()`.

Two host tests pin it, each failing first: after `applyForSyncType(false)`,
`prepare()` leaves `SP_SOG_MODE` 0 and `SP_NO_COAST_REG` 1.

`SP_SOG_MODE` is one of 79 fields with both a sketch writer and an engine-class
writer. Nothing detects a new one, and a second owner is invisible to every
check that compares names rather than bits.
