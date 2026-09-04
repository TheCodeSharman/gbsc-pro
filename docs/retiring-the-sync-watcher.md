# Retiring runSyncWatcher()

Goal: `runSyncWatcher()` is deleted, and everything periodic the firmware does
to a source happens inside `Geometry::poll()` — one entry point, calling a named
method on whichever class owns the registers involved.

`loop()` ends up with one call where it has two:

```
geometry.poll(millis());
```

This is the plan for the order. `docs/retiring-mode-detect.md` is the plan for
the standard byte, whose surviving references are inside this function and which
goes out with it.

## Why it has to go rather than be tidied

`runSyncWatcher()` keeps a **parallel model of the source**, and calls into the
engine once (`geometry.sourceInterrupted()`). Two owners of one model is the register problem one level up, and
every symptom this fork has chased is a case of it:

- `SP_H_PULSE_IGNOR` has two owners. `SyncProcessor::applyForSyncType(false)`
  writes 255 on every mode change and `updateSpDynamic()` writes 2 on its own
  schedule. The register reads 2 on a locked source and the screen is black.
  `docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`
- The **ADC PLL group has two owners on both paths**.
  `Adc::applySampleRate()` derives `PLLAD_KS` from the measured line rate;
  `updateCoastPosition()` writes `PLLAD_ICP` and `PLLAD_FS` and latches, for a
  csync source under 322 lines, and the 900 ms check inside the RGBHV block
  writes `PLLAD_KS`, `PLLAD_FS` and `PLLAD_ICP` and latches, keyed on
  `rto->HPLLState`. A PLL left unlocked shows as a scrambled picture with every
  config register correct.
  `docs/investigations/the-no-sync-branch-is-the-only-escape.md`
- The steadiness of the source is counted twice, as `rto->noSyncCounter` and
  `rto->continousStableCounter` in the sketch and as `idleRun_` in the engine,
  and the two disagree about whether a source is there.

So the fault is not that the code is old. It is that a second owner of a
register cannot be made correct by improving either owner.

## The shape

**ONE ENTRY POINT IS WHAT STOPS THEM CLOBBERING EACH OTHER.** Two callers in
`loop()` have no relationship: the watcher writes `SP_H_PULSE_IGNOR` on its own
20 ms tick with no knowledge that a solve is in flight, and a solve writes the
divider with no knowledge that the watcher is about to walk the slicer. Inside
one function the order is decided rather than raced — acquisition is withheld
while a mode change is pending, and a solve is not begun while the slicer is
being moved. That is the whole reason for the shape, and it is not available
from two entry points however well each behaves.

**`poll()` determines the cadence.** It decides *when* each thing runs; each
class decides *what* it writes. So the watcher's 20 ms tick, its 150-tick
escalation and its 900 ms check stop being three private timers and become one
schedule in one place. Nothing new lives in `Geometry` that belongs to `Adc`,
`SyncProcessor`, `Deinterlacer` or `FrameBuffer` — those classes exist and own
those registers already.

**So `poll()` takes the clock.** A `millis()` reached for inside the engine is a
hidden input the host tests cannot set, and every cadence above is a test case.
`poll(uint32_t nowMs)`, the same direction
`SourceMeasurement::sourceHasOwnVsync(uint32_t (*nowMs)())` already takes.

**`poll()`'s ordering contract is unchanged.** Raster, clock, windows, rate steer
last, and the measuring branch still runs only while a mode change is pending.
Acquisition is periodic work that runs always, so it extends the idle branch.

**What acts outside the engine is injected, not called.** Loading a preset,
selecting an input and driving the OLED live in the sketch and the engine cannot
reach them. They arrive as function pointers, the way `useSyncTypeProbe()`
already does.

## What is in it, and who owns each piece

| what it does | owner | exists |
|---|---|---|
| freeze and power gates | `poll()`'s own entry gate | — |
| `getVideoMode()`, `getStatus16SpHsStable()` | `Geometry::sourceIsPresent()` | yes |
| HD bypass vsync window steering | `HdBypass` | yes |
| the source-disturbed interrupt | `Interrupts`, read by `poll()` | yes |
| the SOG slicer level, all three routines | `Adc` | class yes, method no |
| coast position and window | `SyncProcessor` | class yes, method no |
| clamp position and window | `SyncProcessor` | class yes, method no |
| the sync-search sweep | `SyncSearch` | yes |
| `nudgeMD()`, mode-detect reset | `ModeDetect` | yes |
| freeze and unfreeze | `FrameBuffer` | yes |
| ADC phase optimisation | `Adc` | class yes, method no |
| `noSyncCounter`, `continousStableCounter`, `RGBHVNoSyncCounter` | one steadiness run in the engine | partly |
| the new-mode debounce | `Geometry::sourceMoved()` | yes |
| motion-adaptive deinterlace and scanlines, by `VPERIOD_IF` | `Deinterlacer` | yes |
| scaling-RGBHV entry, exit and preset choice | `PresetLoad`, `OutputChoice`, `SourceKey` | yes |
| `HPLLState` and the ADC PLL steering | `Adc`, as the one owner of the group | class yes |
| the ADC input toggle while hunting | injected action, sketch | — |
| `applyPresets()` | injected action, sketch | — |
| the no-signal latch at `0x07fe` | presence, published | yes |

Most of the owners already exist. This is mostly a move, not a design.

## The rule for every step

**A step MOVES an owner. It never adds a second one.** The sketch's copy is
deleted in the same commit that puts the register under a class, or the fault
this whole plan is about is what the step introduces. That is the one thing to
check in review, and it is checkable: after the step, exactly one place writes
the field.

Each step is a bounded commit plus its host test, cherry-pickable on its own.

## The order

**1. The SOG slicer level.** Measured as the constraint rather than chosen: the
no-sync branch is the only thing that repairs a slicer the pre-emptive tuning
has walked below what the source needs — `ADC_SOGCTRL` 12 to 5 in one step,
after which the ADC PLL falls out of lock, the engine cannot finish the solve it
has armed, capture stays frozen and the screen stays black. Nothing else can
land in front of it.
`docs/investigations/the-no-sync-branch-is-the-only-escape.md`

**2. Freeze and unfreeze**, to `FrameBuffer`, which owns capture already.

**3. Coast and clamp**, to `SyncProcessor`, with `updateCoastPosition()`'s writes
to the ADC PLL group deleted rather than moved — `Adc` owns that group.

**4. The steadiness runs.** `noSyncCounter`, `continousStableCounter` and
`RGBHVNoSyncCounter` become reads of the engine's own run. This is what makes
every ladder above keyed on engine state rather than on a parallel count.

**5. The no-sync gate**, to `Geometry::sourceIsPresent()`. Written and tested
already; it is the wiring that waits for step 1.

**6. Deinterlace and scanlines**, to `Deinterlacer`.

**7. The RGBHV block**, to `PresetLoad` and `OutputChoice`. The largest single
piece, and the one that carries most of the standard byte.

**8. The ADC PLL steering.** `HPLLState` and its `PLLAD_KS`/`FS`/`ICP` writes
become `Adc`'s, so the group has one owner on every path.

**9. Delete `getVideoMode()` and `videoStandardInput`**, which by then have no
readers. `docs/retiring-mode-detect.md` has what each of their fifteen values
carried and what replaced it.

**10. Delete `runSyncWatcher()`**, and `loop()` calls `poll()` alone.

## The bar

**Observable picture behaviour, on the paths the bench can exercise** — not
binary equivalence and not code equivalence. Code with no observable effect on
the picture is deleted rather than preserved, and which branch a step is judged
against is decided by which source is plugged in: `docs/bench-sources.md`.

Two reproductions reach most of this and are scriptable from a session:

- a sync-type round trip, `SYNC 1` then `SYNC 0` over ModeServ, which exercises
  the sync-type probe, the coast and clamp windows and the SOG level
- `/input?src=rgbs` with nothing attached, which is the only "the signal really
  has gone" case reachable without a cable change, and is what a step that
  withholds recovery has to be checked against

## Not in scope

- `detectAndSwitchToActiveInput()` and `inputAndSyncDetect()`. They run at boot
  and on an input change rather than periodically, so they are not the watcher;
  they come after it, if at all.
- The `HPERIOD_IF` railing state, which is its own investigation.
