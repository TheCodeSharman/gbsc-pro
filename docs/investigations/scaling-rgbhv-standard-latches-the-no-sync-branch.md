# `ScalingRgbhvStandard` latches the sketch into its no-sync branch

A scaling RGBHV source can reach a state where the picture is gone, every
register reads correct, and the geometry engine reports a correct solve every
few seconds for ever. Measured on the bench RiscPC, `vga`, 320x256@50.

## What it looks like

`/geometry` is stable and right — raster 1915, `lineRateHz` 15625 — and so is
every register the arithmetic uses: `PLLAD_MD` 2250, `STATUS_SYNC_PROC_HTOTAL`
2250, `VTOTAL` 311, `PLLAD_LOCK` 1, DACs powered. The television is dark.

The console is where it shows. Every ~5 s, indefinitely:

```
 8.60  own V sync: yes after 2ms
 8.80  source moved: interrupt (311 lines, solved 311)
 9.00  own V sync: yes after 3ms
 9.61  sampling: 311 lines x 77.55 Hz -> line rate 0
 9.61  sampling: 311 lines x 50.08 Hz -> line rate 15625
 9.81  externalClockGenSyncInOutRate()
```

`source moved: interrupt (311 lines, solved 311)` is the tell: the count the
engine solved for and the count it just measured are the same number.

## The chain

1. A preset load ends `rto->videoStandardInput = load.videoStandardInputAfterLoad()`,
   which is `PresetLoad::ScalingRgbhvStandard` — **3**.
2. `sourceIsRgbhv()` tests `>= 14`. At 3 it is false, so `getVideoMode()` takes
   the SD mode-detect path and reads `STATUS_00`, which is `0x00`. `STATUS_16`
   is `0x0f` at the same moment, and `(0x0f & 0x0a) > 0` — the RGBHV path would
   have classified it.
3. `getVideoMode()` returns 0, so `runSyncWatcher()` takes its no-sync branch on
   every pass. `continousStableCounter` stays 0 and `noSyncCounter` climbs.
4. **Nothing can put the value back.** The only assignment of 14 sits inside
   `if (steerableRgbhv())` — which is `sourceIsRgbhv()`, needing 14 already —
   *and* `rto->continousStableCounter >= 2`, which only the stable branch
   raises. Both gates need the value they are the sole writer of.
5. At `noSyncCounter == 150` the no-sync branch calls `sourceHasOwnVsync()`.
6. That probe writes `SP_EXT_SYNC_SEL` to 0 and restores it, and **the chip
   latches the write as a SOG switch**. `Interrupts::takeSourceDisturbed()`
   hands it to `Geometry::sourceInterrupted()`, `Geometry::sourceMoved()`
   returns true, and a full re-solve is armed on a source that did not move.
7. The solve sets `modePending_`, so `SyncOutput::poll()` blanks HSOUT/VSOUT for
   its whole duration.
8. `noSyncCounter` is set to `0x07fe` and reset to 0 on the next pass, climbs to
   150 again, and the cycle repeats.

The picture is lost at step 7: **the encoder is shown a sync dropout every few
seconds and never holds a lock long enough to paint.** `s0_49` bit 2 measured
going 0 -> 1 -> 0 at 9.24/9.58 s, 14.62/15.11 s, 24.40/24.73 s, 29.62 s.

## What each link was measured with

| link | evidence |
|---|---|
| 2, 3 | `STATUS_00` `0x00` against `STATUS_16` `0x0f`; `m:0` in every `printInfo()` line |
| 3, 4 | `s: 0` and `u: 96` — `continousStableCounter` 0 and `noSyncCounter` 150 — in every `printInfo()` line across 90 s |
| 5, 6 | 11 of 11 `own V sync:` lines followed within 40..200 ms by `STATUS_0F` bit 1 reading 1. Bit 1 was never observed set at any other time |
| 7 | `s0_49` sampled over 30 s, four blank/unblank pairs |

## Two things this is not

**It is not the `HPERIOD_IF` railing.** That register reads 350 against the 431
the mode is due, `STATUS_IF_HT_BAD` is 1, and the engine is correct beside it —
it holds 15625 Hz and a raster solved for it. Railing is the state the engine
was made indifferent to, and it is not what takes the picture away.

**It is not the interrupt misbehaving.** `STATUS_INT_SOG_SW` is reporting a real
write to `SP_EXT_SYNC_SEL`. The defect is that a probe the firmware itself runs
is read back as the source having moved.

## Where a fix goes

Step 4 is the root: one number carries both "which SD standard" and "is this
scaling RGBHV", and the two arms that maintain it each require the other's
output. Step 6 is the cheapest place to break the loop — a disturbance the
firmware caused is not the source moving — but it leaves the sketch in the
no-sync branch, so the recovery routines keep running underneath a good solve.

`docs/investigations/scaling-rgbhv-flag-is-not-the-standard.md` records the same
one-number-two-meanings shape from the register side.

## Recovering it, and why that is the proof

**An ESP restart (`/uc?a`) clears it.** The picture returns at once, `s0_49` bit
2 holds 0 across 40 s with no blanking, and the console goes quiet — no probe,
no `source moved`, no `sampling`, no `printInfo()` line at all, because the
stable branch does not call it.

**`STATUS_00` reads `0x00` and `STATUS_16` reads `0x0f` in BOTH states**, before
and after, unchanged. Nothing on the TV5725 distinguishes a unit that is
cycling and dark from one holding a clean full-screen picture: the whole
difference is `rto->videoStandardInput` in ESP RAM, 3 against 14. A register
dump cannot see this fault, and diffing one against a known-good will report
that nothing is wrong.
