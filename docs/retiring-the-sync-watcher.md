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
divider with no knowledge that the watcher is about to walk the sync separator. Inside
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
| acquire the sync type, PER SOURCE MODE CHANGE | `sourceHasOwnVsync()`, and the two places that guess |
| acquire the sync separator level | `optimizeSogLevel()`, `fastSogAdjust()`, `tuneSogLevelPreemptively()` and every ratchet |
| acquire the coast window | `updateCoastPosition()`, minus its writes to the ADC PLL |
| acquire the clamp window | `updateClampPosition()` |
| acquire the sampling phase | `optimizePhaseSP()` |

**The sync type is acquired per source MODE change, not per input.** A source
can change its sync type without the mux moving -- a RISC PC sets it from CMOS
-- so the mode change is the only signal there is. The cost does not argue
otherwise: reacquisition is 2-3 ms on a source with its own V sync, and the full
window is spent only on a genuinely composite source where the timeout is the
right answer. `docs/sync-type-selection.md`.

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
| the freeze gate | `poll()`'s own entry gate | yes |
| `getVideoMode()`, `getStatus16SpHsStable()` | `Geometry::sourceIsPresent()` | yes |
| HD bypass vsync window steering | `HdBypass` | yes |
| the source-disturbed interrupt | `Interrupts`, read by `poll()` | yes |
| the SOG sync separator level, all three routines | `Adc` | class yes, method no |
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

**THE ENTRY GATE IS THE FREEZE, AND NOT `rto->boardHasPower`.** The pairing
reads natural and the second half is a trap: that flag is a latched failure
rather than a live reading. `runSourceRecovery()` sets it false when
`checkBoardPower()` fails and its success branch never sets it back, so it holds
false for the whole recovery -- exactly when the engine has to solve. Gated on
it, detection probes every seven seconds against an engine that can never
answer, and the unit does not reacquire: sync processor counting 0, DAC down and
`/geometry` all zeroes, where the same source recovers at once without it.
A power fact the engine can trust would have to be measured, not read off that
flag.

**NOTHING STAYS IN THE SKETCH BECAUSE IT WAS AWKWARD TO MOVE.** A routine being
extracted usually reaches into two or three other subsystems, and the honest
intermediate is to take what belongs to the class and leave the rest where it
is. That is a step, not an end state: every side effect left behind has an owner
of its own, and the plan names it rather than letting it settle in the sketch by
default.

| left in the sketch by an earlier step | its owner | lands at |
|---|---|---|
| `updateSpDynamic()` | `SyncProcessor` -- it writes `SP_*` and nothing else | step 5, with the coast window |
| `lastVsyncLock` | FrameSync, which is the only thing that reads it | step 11, with the rate steer |
| `rto->phaseIsSet` | `Adc` | step 6, with the sampling phase |
| `rto->coastPositionIsSet`, `rto->clampPositionIsSet` | `SyncProcessor` | step 5 |

`updateSpDynamic()` is not tidying. It is the second owner of
`SP_H_PULSE_IGNOR`, writing 2 where `applyForSyncType()` wrote 255, and a black
screen on a locked source is what that costs -- so it is one of the faults this
plan exists for rather than a leftover to sweep up afterwards.
`docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`

Each step is a bounded commit plus its host test, cherry-pickable on its own.

## The order

Each step extracts one named operation, merges it into the idle pass, and
deletes the sketch's copy in the same commit.

**1. One owner for the sync separator level.** `Tv5725::SyncOnGreen` holds the level and
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

**3. Acquire the sync separator level.** `optimizeSogLevel()`, `fastSogAdjust()`,
`tuneSogLevelPreemptively()` and every ratchet become one operation on the idle
pass.

**AND IT ASKS WHETHER SYNC ON GREEN IS THE SYNC SOURCE, WHICH TWO OF THE FOUR DO
NOT.** The sync separator only reaches the sync processor with `SP_SOG_MODE` 1, which
follows the sync type, so on a separate-sync source the level is inert — and
`fastSogAdjust()` and the every-150 recovery block walk it anyway. The engine
holds that answer already, as `SyncType::isCsync()`, so the operation asks held
state rather than reading the register back. `SyncOnGreen::inSyncPath()` is that
question, and every site that moves the level asks it.

This is the step that has to land before the gate: the recovery is the only
thing that leaves the black state a round trip can produce, and until the
acquisition it is doing badly has an owner, a gate in front of it is a gate in
front of the only exit.

**THE OWNERSHIP LANDS HERE; THE CALL SITES TRAVEL WITH STEP 4.** All four
routines own their registers now, but moving the pre-emptive pass onto the idle
pass needs a gate the engine does not yet have. In the sketch it runs behind
`sourceDisconnected` and `syncWatcherEnabled`; on the idle pass the only
equivalent is `sourceIsPresent()`, which is step 4. Moving it without one runs
acquisition throughout a recovery, walking the level down on a source that
cannot answer -- the failure this step already produced twice, once from an
unconditional walk and once from a gate keyed on `rto->boardHasPower`.

And the gate is not a formality to pick: the pass is PRE-EMPTIVE, so it earns
its keep exactly as sync degrades, and `sourceIsPresent()` is a held steady
count that goes false at that moment. Whether it should run while the run is
broken is the question step 4 has to answer, not one to settle by whichever
gate happens to be reachable.

**What it leaves in the sketch, and where that goes.**
`tuneSogLevelPreemptively()` is the one of the three that reaches outside the
level: it calls `updateSpDynamic()`, stamps `lastVsyncLock` and clears
`rto->phaseIsSet`. The level's own window and ratchet come here; those three
stay put for now and are claimed by `SyncProcessor`, FrameSync and `Adc` at
steps 5, 11 and 6. Leaving them is the intermediate the table under *The rule
for every step* describes, not a decision that they belong to the sketch.
`docs/investigations/the-no-sync-branch-is-the-only-escape.md`

**THE INTERRUPT RE-ARMS ARE A RETRY LOOP, NOT WASTE.** One source mode change
produces three solves and three sync-type probes, and removing the repeats looks
like free speed -- the switch halves. It is not free: the latched disturbance is
what re-arms `establishSyncType()`, and the repeats are what let the probe
converge. Traced on a `SYNC 1` -> `SYNC 0` round trip, the return leg recovers
because `source moved: interrupt (281 lines, solved 308)` arms a re-probe 0.61 s
in. Consume the latch on a completed solve and the unit sits on the csync path
indefinitely -- measured at 74 s with `SP_VTOTAL` reading 97, which is a
separate-sync source counted through the wrong path.

**The waste is after convergence, and that is what to remove.** The same trace
keeps arming at 6.40 s and 11.62 s with `interrupt (311 lines, solved 311)` and
the rate measuring 50.08 both times -- the count equal to the solved count and
nothing moved. So the check is whether anything actually differs before
re-solving, not whether a solve has happened since the latch was set.

**And no host test reaches this.** A test pinning an interrupt after a solve
passes either way, and a mode soak of 36 changes across nine timings passes too,
because every one holds the sync type constant. The round trip is the only
reproduction that moves it, and it belongs in the acceptance criteria of
anything touching how the engine re-arms.

**AND THE WALK IS HANDED IN, NOT CALLED.** The handover, taken once the level
is too low to step, runs `optimizeSogLevel()` -- which REFUSES to walk under
`rgbhvBypass()` and parks the default instead. Calling `acquire()` from inside
the class looks equivalent and is not: during a detection sweep the handover is
reached faster than a source can lock, so an unconditional walk ratchets the
level to 2 and pins it there, `SP_SOG_MODE` 1 and `SP_VTOTAL` 0, where the
refusing version holds 12 and a clean picture on the same source state. No host
test separates them and no ESP restart recovers it.
`docs/investigations/refusing-to-walk-is-part-of-the-walk.md`

**4. One steadiness run, WHICH IS THE NO-SYNC GATE.** `noSyncCounter`,
`continousStableCounter` and `RGBHVNoSyncCounter` become reads of the engine's
own run, and `Geometry::sourceIsPresent()` replaces the classification at the
gate. Everything below is keyed on the run.

**AND STEP 7 HAS TO COME FIRST, WHICH THE ORDER ABOVE GETS WRONG.** The gate
itself is right -- measured, the engine calls the bench source present while
`getVideoMode()` calls it absent, and the engine is correct. What wiring it does
is let the no-sync branch ADVANCE where `noSyncCounter` used to sit pinned at
150, so it reaches recoveries that were never running. On the bench that ends
with `SP_SOG_MODE` 1 against a held sync type of separate, `SP_VTOTAL` 97, and
no way back. Until the ladder is a named list rather than a run of moduli,
opening the gate in front of it is what the step cannot do.
`docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md`

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
  the sync-type probe, the coast and clamp windows and the sync separator level
- `/input?src=rgbs` with nothing attached, which is the only "the signal really
  has gone" case reachable without a cable change, and is what a step that
  withholds recovery has to be checked against

**WHICH ONE A STEP MUST RUN IS NOT THE AUTHOR'S CHOICE.**

| a step that touches | must run |
|---|---|
| how the engine re-arms, or anything the sync type is derived from | the sync-type round trip |
| a recovery, a gate in front of one, or the no-sync branch | `/input?src=rgbs` with nothing attached |
| the sync separator level, coast, clamp or phase | the round trip, whose csync leg is the only thing here that uses them |
| geometry, the raster, the windows | a mode change, and the picture judged against `docs/bench-sources.md` |

**A MODE SOAK IS NOT A SUBSTITUTE FOR THE ROUND TRIP, and it looks like one.**
Cycling every mode the source offers holds the sync type constant throughout, so
36 changes across nine timings pass while the sync type is left on the wrong
path indefinitely. That is a measured miss, not a hypothetical: it is how a
change that consumed the latched disturbance reached the bench and sat on the
csync path for 74 s with `SP_VTOTAL` reading 97.

**And a host test that passes either way proves nothing about it.** The one
covering that change pinned an interrupt arriving AFTER a solve, which fires
whether the latch is consumed or not. Where a change alters WHEN something is
re-tried rather than what it does, the host layer cannot see it and the bench
reproduction is the whole of the evidence.

## Not in scope

- `detectAndSwitchToActiveInput()` and `inputAndSyncDetect()`. They run at boot
  and on an input change rather than periodically, so they are not the watcher;
  they come after it, if at all.
- The `HPERIOD_IF` railing state, which is its own investigation.
