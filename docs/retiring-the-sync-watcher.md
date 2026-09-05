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

**It is not a relocation, it is a merge.** The watcher and the engine are two
implementations of one idea — *notice that the sync changed, and configure the
registers for what is there now*. That idea is sane and it is the engine's; what
the watcher adds is the acquisition the engine does not do yet. So the watcher's
logic is folded into the mode-change detection that already exists, and the
ladder of counters and conditionals around it does not come with it.

**The engine currently DEPENDS on the watcher, which is why it cannot simply be
deleted.** What it relies on today:

| the engine needs | the sketch supplies |
|---|---|
| to be told a preset load happened | `applyPresets()` calls `modeChanged()` |
| the latched source disturbance | `runSyncWatcher()` calls `sourceInterrupted()` |
| a sync type probe | `useSyncTypeProbe(sourceHasOwnVsync)` |
| a source acquired well enough to measure | the SOG, coast and clamp routines |

Each row is a step: the engine takes the job over and the sketch's copy goes.

**ONE ENTRY POINT IS WHAT STOPS THEM CLOBBERING EACH OTHER.** Two callers in
`loop()` have no relationship: the watcher writes `SP_H_PULSE_IGNOR` on its own
20 ms tick with no knowledge that a solve is in flight, and a solve writes the
divider with no knowledge that the watcher is about to walk the slicer. Inside
one function the order is decided rather than raced. That is not available from
two entry points however well each behaves.

**NAMED OPERATIONS, NOT A LADDER.** The watcher's structure is a counter and a
run of conditionals against it — `% 27`, `% 32`, `== 38`, `% 150`, `% 413` — and
that structure is not worth carrying anywhere. Each distinct thing it does
becomes a method named for what it does, on the class that owns those registers,
and `poll()` decides which to call. Nothing is moved as a block.

**The cadence rides on the mode-change check.** `poll()`'s idle pass already
reads the source's line count to ask whether it moved; the same reading answers
whether the source is acquired, so both questions are asked once per pass off one
measurement. That is also what keeps a steadiness run honest — two callers each
advancing a run over the same count is the double-advance that
`Geometry::sourceIsPresent()` had to be written around.

**This is the lean, not a settled answer.** The watcher's own timers are 20 ms,
150 ticks and 900 ms, and whether the escalation steps want a slower cadence than
the detection check is something to try on the bench rather than to derive.

**So `poll()` takes the clock.** A `millis()` reached for inside the engine is a
hidden input the host tests cannot set, and every cadence above is a test case.
`poll(uint32_t nowMs)`, the same direction
`SourceMeasurement::sourceHasOwnVsync(uint32_t (*nowMs)())` already takes.

**`poll()`'s ordering contract is unchanged.** Raster, clock, windows, rate steer
last, and the solving branch still runs only while a mode change is pending.
Acquisition runs always, so it extends the idle branch.

**What acts outside the engine is injected, not called.** Loading a preset,
selecting an input and driving the OLED live in the sketch and the engine cannot
reach them. They arrive as function pointers, the way `useSyncTypeProbe()`
already does.

## The named operations

What the ladder does, as operations. This is the decomposition to review before
any of it moves — a step that cannot be stated as one of these rows is a step
that has not been understood yet.

**Detection**, once per idle pass, off one measurement:

| operation | replaces |
|---|---|
| `sourceIsPresent()` | `getVideoMode() == 0`, `getStatus16SpHsStable()` |
| the steadiness run | `noSyncCounter`, `continousStableCounter`, `RGBHVNoSyncCounter` |
| `sourceMoved()` | the `newVideoModeCounter` debounce |
| the latched disturbance | `takeSourceDisturbed()`, already one claimant |

**Acquisition**, when detection says the source is not yet usable:

| operation | replaces |
|---|---|
| acquire the sync type | `sourceHasOwnVsync()`, and the two places that guess |
| acquire the slicer level | `optimizeSogLevel()`, `fastSogAdjust()`, `tuneSogLevelPreemptively()` and every ratchet |
| acquire the coast window | `updateCoastPosition()`, minus its writes to the ADC PLL |
| acquire the clamp window | `updateClampPosition()` |
| acquire the sampling phase | `optimizePhaseSP()` |

**Escalation**, when acquisition keeps failing. The counter ladder becomes an
ordered list of named recoveries, each tried once before the next: widen the
coast, reset the sync processor, reset mode detect, re-probe the sync type,
toggle the ADC input. The magic moduli carry no information that a position in
that list does not.

**Maintenance**, while a source is acquired:

| operation | replaces |
|---|---|
| steer the deinterlacer | the `VPERIOD_IF` motion-adaptive and scanline state machine |
| steer the HD bypass vsync window | `steerHdBypassVsyncWindow()`, already extracted |
| steer the ADC PLL | `HPLLState` and its `PLLAD_KS`/`FS`/`ICP` writes |

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

**And a step is stated as one of the named operations above.** One that cannot
be is a step that has not been understood yet, and moving it will carry the
ladder's shape across with it.

Each step is a bounded commit plus its host test, cherry-pickable on its own.

## The order

Each step extracts one named operation, merges it into the idle pass, and
deletes the sketch's copy in the same commit.

**1. One owner for the slicer level.** `Tv5725::SyncOnGreen` holds the level and
owns `ADC_SOGCTRL`; `rto->currentLevelSOG` and `setAndUpdateSogLevel()` go. No
policy moves. It separates the two facts that variable carried — the level
*chosen* for a source the ADC has not been brought up for, and the level *in
force* — which is why a straight substitution would have been wrong.

**2. The detection cadence.** `poll()` takes the clock and the idle detection
pass runs on `Geometry::DetectionIntervalMs`. The steadiness run is counted in
detection passes and `loop()` goes round far faster than the sync watcher's
20 ms tick, so without this a run counted per pass is not the same length as one
counted per tick and every threshold keyed on it means something different.
Nothing else can read the engine's run until it counts in the units the sketch's
counters did.

**3. Acquire the slicer level.** `optimizeSogLevel()`, `fastSogAdjust()`,
`tuneSogLevelPreemptively()` and every ratchet become one operation on the idle
pass.

**AND IT ASKS WHETHER SYNC ON GREEN IS THE SYNC SOURCE, WHICH TWO OF THE FOUR DO
NOT.** The slicer only reaches the sync processor with `SP_SOG_MODE` 1, which
follows the sync type, so on a separate-sync source the level is inert — and
`fastSogAdjust()` and the every-150 recovery block walk it anyway. The engine
holds that answer already, as `SyncType::isCsync()`, so the operation asks held
state rather than reading the register back. `SyncOnGreen::inSyncPath()` is that
question, and every site that moves the level asks it.

This is the step that has to land before the gate: the recovery is the only
thing that leaves the black state a round trip can produce, and until the
acquisition it is doing badly has an owner, a gate in front of it is a gate in
front of the only exit.
`docs/investigations/the-no-sync-branch-is-the-only-escape.md`

**4. One steadiness run, WHICH IS THE NO-SYNC GATE.** `noSyncCounter`,
`continousStableCounter` and `RGBHVNoSyncCounter` become reads of the engine's
own run, and `Geometry::sourceIsPresent()` replaces the classification at the
gate. Everything below is keyed on the run.

**THESE ARE ONE CHANGE, NOT TWO, AND THAT IS WHY NEITHER CAN COME FIRST.** The
run is over `sourceIsPresent()` — the same measurement the mode-change check
takes — so a `noSyncCounter` that reads it never advances on a source the engine
calls present, and the escalation ladder is withheld exactly as wiring the gate
withholds it. Splitting them buys nothing: the sketch's counter would still be
gated on the engine's answer, which is the whole risk. Step 3 is what makes
either safe.

The ladder's moduli are NOT the run. `noSyncCounter` also carries an escalation
position and a control latch the sketch writes — `0x07fe`, `0x05ff`, `63`, `1` —
and those stay a local index in the watcher until step 7 replaces them with the
named recoveries. What moves here is the run.

**5. Acquire the coast and clamp windows**, to `SyncProcessor`, with
`updateCoastPosition()`'s writes to the ADC PLL group deleted rather than moved —
`Adc` owns that group.

**6. Acquire the sampling phase**, to `Adc`.

**7. The escalation list** replaces the counter ladder: an ordered set of named
recoveries tried in turn, in place of `% 27`, `% 32`, `== 38`, `% 150` and
`% 413`.

**8. Freeze and unfreeze**, to `FrameBuffer`, which owns capture already.

**9. Steer the deinterlacer**, to `Deinterlacer`.

**10. The RGBHV block**, to `PresetLoad` and `OutputChoice`, with the preset load
becoming an injected action. The largest single piece, and the one that carries
most of the standard byte.

**11. Steer the ADC PLL.** `HPLLState` and its `PLLAD_KS`/`FS`/`ICP` writes
become `Adc`'s, so the group has one owner on every path.

**12. Delete `getVideoMode()` and `videoStandardInput`**, which by then have no
readers. `docs/retiring-mode-detect.md` has what each of their fifteen values
carried and what replaced it.

**13. Delete `runSyncWatcher()`**, and `loop()` calls `poll(millis())` alone.

## Input selection is the same collapse, one level up

`applyInputSelection()` is already the single path the OLED menu and
`/input?src=` share. What it is missing is the engine: it writes the input
registers, resets the sync processor, raises `rto->sourceDisconnected` and
returns, so the engine learns the source moved only when detection eventually
runs and something calls `applyPresets()`.

**Selecting an input is a source event, so it belongs to the same entry point as
every other source event.** The engine is told, it re-acquires and it solves --
rather than the sketch poking registers and leaving the engine to notice. That
is what makes the code tractable to reason about: one path in, whether the
request came from the menu, the remote or HTTP.

It lands with step 10, where the preset load becomes an injected action: the
same commit that stops `applyPresets()` being how the engine hears about a
source is the one that gives input selection somewhere better to call.

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
