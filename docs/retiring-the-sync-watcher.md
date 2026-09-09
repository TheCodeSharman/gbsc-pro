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

- `SP_H_PULSE_IGNOR` had two owners. `SyncProcessor::applyForSyncType(false)`
  writes 255 on every mode change and the sketch's search wrote 2 on its own
  schedule; the register reads 2 on a locked source and the screen is black.
  Both writes are `SyncProcessor`'s now, so the class arbitrates them -- what
  remains is the sketch deciding WHEN to hunt.
  `docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`
- The **ADC PLL group had owners on both paths**. `Adc::applySampleRate()`
  derives `PLLAD_KS` from the measured line rate, and the 900 ms check inside
  the RGBHV block wrote the whole triple from a band index of its own. That
  index is `Adc`'s now, and so is the scaling path's own charge pump. What is
  left outside the class is `setResetParameters()` and the HD bypass switch. A
  PLL left unlocked shows as a scrambled picture with every config register
  correct.
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

### What the ladder does today, against that list

Read off `runSyncWatcher()`'s no-sync branch. Every rung already calls a named
owner -- that half of step 7 has landed -- so what is left is the POSITION.

| # | fires at | extra condition | operation |
|---|---|---|---|
| 0 | `== 1` | -- | nothing: returns, so one missed pass costs no escalation |
| 1 | `== 2` | `newVideoModeCounter == 0` and serrated sync | `SyncOnGreen::liftOffFloor` |
| 2 | `== 8` | -- | default coast window, widen if serrated, forget positions |
| 3 | `% 27` | -- | `updateSpDynamic(1)` |
| 4 | `% 32` | `STATUS_SYNC_PROC_HSACT` 1 | `FrameBuffer::releaseCapture()` |
| 5 | `== 34` | YPbPr, `Info_sate` 0 | hold clamp, forget positions |
| 6 | `== 38` | -- | `ModeDetect::nudge()` |
| 7 | `> 47`, `% 16` | csync | `toggleHsyncOverflowProtect()` |
| 8 | `% 150` | -- | clear overflow protect, default coast and clamp, `updateSpDynamic(1)`, nudge Mode Detect, re-acquire the SOG level, reset the sync processor, reset Mode Detect |
| 9 | `== 150` or `% 900` | -- | `Geometry::reacquireSyncType()`; parks at `0x07fe` if it finds V sync |
| 10 | `% 413` | `detectionMayChangeInput()` | `Adc::selectOtherInput()`, kept only if it locks within 210 ms |

**Four of the eleven repeat, and that is the behaviour the ordered list
changes**: 3, 4, 7, 8 and 10 come round again for as long as the source stays
absent, where the list tries each once. Three of the repeats are cheap and one
is not -- 10 moves the input mux, and rung 9's park at `0x07fe` exists to stop
the counter ever reaching it once a V sync has been seen.

**The positions are not an order.** 3 and 4 interleave with 2 and 5 by accident
of their moduli, and 7 only starts after 47 while repeating every 16, so which
recovery has been tried by a given count is not readable from the code. That is
the information the list restores, and it is why the gate cannot open in front
of the ladder as it stands: `sourceIsPresent()` lets the counter ADVANCE where
it used to sit pinned at 150, so rungs that never ran before start running.

### `0x07fe` is a signal, not a park, and its comment says otherwise

The two sites that write `rto->noSyncCounter = 0x07fe` are commented as stopping
the escalation before it reaches the input toggle. **That is not what it does.**
A block further down reads the value:

    if (rto->noSyncCounter >= 0x07fe) {
        rto->noSyncCounter = 0;
        printf("No Signal Out\n");
        rto->HdmiHoldDetection = true;
    }

So the write is a MESSAGE to that block, and its effect is: announce no signal,
set `HdmiHoldDetection`, and **restart the run from zero**. The ladder does not
stop -- it begins again, and reaches the input toggle in another 413 passes.

`0x07fe` has a second trigger nobody wrote: the counter reaches 2046 by counting,
which is about 41 seconds of no sync at the 20 ms tick. So the block is both "the
ladder gave up" and "one of these two rungs found something", on one path, and
the reason it works is that both want the same thing -- end the round.

**That is a cycle, which is what the ordered list already has.** The honest
replacement is `SyncRecovery::CycleLength` reaching its end, with the two rungs
ending the cycle early rather than jumping a counter to a number chosen to be
above a threshold. What it is NOT is a flag meaning "a source was seen here":
that reading was tried, and it is wrong because the counter reset is the point.

**The cadence changes if this is done naively.** Natural expiry is 2046 passes
today and the list's cycle is 451, so the no-signal announcement would arrive
four times sooner. Whether 41 s or 9 s is right is a judgement about what a
television should be told, not something the refactor can settle.

**What has to be decided before it moves, and neither is mechanical:**

- **What happens at the end of the list.** The moduli have no end; they cycle
  for ever, which is what a source that is genuinely unplugged needs. An ordered
  list tried once has to either stop, or restart, or hold at its last rung, and
  that choice is what the unplugged case is judged on.
- **Whether rung 0's early return survives.** It makes the first failed pass
  free, so a single dropped measurement costs nothing. Against a run counted in
  detection passes rather than 20 ms ticks -- step 2 -- one pass is a different
  amount of time, and the debounce may want to be the run's own.

**Maintenance**, while a source is acquired:

| operation | replaces |
|---|---|
| steer the deinterlacer | the `VPERIOD_IF` motion-adaptive and scanline state machine |
| steer the HD bypass vsync window | `steerHdBypassVsyncWindow()`, already extracted |
| steer the ADC PLL | the band index and its `PLLAD_KS`/`FS`/`ICP` writes |

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
| the PLL band and its steering | `Adc`, as the one owner of the group | yes |
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
| `updateSpDynamic()`'s decision of when to hunt | `Geometry`, off its own steadiness run | step 13, with the watcher |
| `lastVsyncLock` | FrameSync, which is the only thing that reads it | with the rate steer, once FrameSync has an owner |
| `rto->phaseIsSet` | `Adc` | step 6, with the sampling phase |
| `rto->coastPositionIsSet`, `rto->clampPositionIsSet` | `SyncProcessor` | step 5 |

`updateSpDynamic()` was not tidying. It was the second owner of
`SP_H_PULSE_IGNOR`, writing 2 where `applyForSyncType()` wrote 255, and a black
screen on a locked source is what that cost -- one of the faults this plan
exists for rather than a leftover to sweep up afterwards. Every register it
writes is now behind a named `SyncProcessor` operation and what is left of it is
the decision of whether the source is being hunted for or read.
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
`% 413`. **Step 4 waits on this**, because wiring the gate is what lets the
branch advance far enough to reach these.

What the ladder does, rung by rung, and which rungs have an owner:

| trigger | what it does | owner |
|---|---|---|
| `== 1` | one pass of grace, returns | no registers |
| `== 2` | lift the sync separator level off the floor, on a serrated source | `SyncOnGreen::liftOffFloor()` |
| `== 8` | put the coast window back, then widen it on a serrated source | `SyncProcessor::applyDefaultCoastWindow()`, `widenCoastForSerration()` |
| `% 27` | configure the separator to hunt | `SyncProcessor::applyForSearch()` |
| `% 32` | unfreeze if HSACT | **step 8's**, with `FrameBuffer` |
| `== 34` | YPbPr only: hold the clamp | `SyncProcessor::holdClamp()` |
| `== 38` | make mode detect re-latch | `ModeDetect::nudge()` |
| `> 47, % 16` | csync only: try the other overflow-protect setting | `SyncProcessor::toggleHsyncOverflowProtect()` |
| `% 150` | reacquire the sync type, put the coast and clamp windows back, `updateSpDynamic(1)`, nudge, re-acquire the sync separator level, reset the sync processor, reset mode detect | `Geometry::reacquireSyncType()`, the two window defaults, `ModeDetect::nudge()`, `SyncOnGreen::reacquire()`, `SyncProcessor::reset()`, `ModeDetect::reset()`, `SyncProcessor::applyForSearch()` |
| `% 413` | try the other ADC input, put it back if nothing locks | `Adc::selectOtherInput()` and `selectInput()`; the wait stays with the counter |

**Every rung now names an operation, and no rung writes a register itself.**
The one read still taken raw is `STATUS_SYNC_PROC_HSACT` in front of the
unfreeze, which travels with step 8.

**`Adc::bounceInput()` is NOT what `% 413` became**, and the two must not be
merged. The bounce takes the input away and puts the SAME one back, to clear a
railed `HPERIOD_IF`; the rung moves to the OTHER input and keeps it if the
source locks there. Nothing calls the bounce, and wiring it as an automatic
recovery puts a green screen on every solve that lands on a flagged counter.

**`% 150` is the compound one and it is where the harm was.** Its sync-type
correction used to be one-directional: it could move a held csync to separate
and never back, and it never reconciled the register with the held value, so a
source counted through the wrong path with the held type already right had no
route out. `Geometry::reacquireSyncType()` is that rung — it applies the probe's
answer to the chip whatever the held value says — and it also stops the recovery
probing an input whose connector settles the sync type.

**THE OLED MENU KEEPS ITS OWN COPIES OF TWO OF THESE.**
`OLEDMenuImplementation.cpp` has private `resetSyncProcessor()`,
`resetModeDetect()` and `resetSyncProcessor_yuv()` pulsing the same
`SFTRST_*_RSTZ` bits, the last two of them never called. They are a second
writer and they go with step 10, where the menu stops writing registers at all.
The live one bundles `LoadDefault()`, so it is not the same operation and cannot
be substituted blind.

**A rung that reads a register to judge a measurement needs a bus that can
move.** `SyncOnGreen::reacquire()` decides between walking the level and parking
it on whether `STATUS_SYNC_PROC_HLOW_LEN` changes across a run of reads, and a
fake register holding one value can only reach the frozen branch. `FakeTwoWire`
has `drift()` for that, and any rung judged on movement rather than on a value
will want it.

**`Adc::bounceInput()` is what `% 413` becomes, and nothing installs it**: it
turns the whole screen green for as long as the input is away, so wiring it as
an automatic recovery puts a visible flash on every solve that lands on a
flagged counter.

**8. Freeze and unfreeze**, to `FrameBuffer`, which owns capture already.

**9. Steer the deinterlacer**, to `Deinterlacer`.

**10. The RGBHV block**, to `PresetLoad` and `OutputChoice`, with the preset load
becoming an injected action. The largest single piece, and the one that carries
most of the standard byte.

**Its two entries are one call now.** Leaving bypass and crossing into another
preset's bucket ran thirty byte-identical lines each, so every register in that
sequence had two writers. The sync processor's share is
`SyncProcessor::applyForScalingRgbhv()` and the ADC's is
`Adc::applyScalingChargePump()`; what is still spelled out is the option bit,
the line counter's start, the standard byte's round trip through
`applyPresets()`, and the external clock generator. Those are the four this step
still has to place, and the standard byte's is step 12's.

**The second entry may not survive the step it is waiting on.** It exists to
reload a different preset when the source's line count crosses 280 or 380, which
is per-standard preset selection over tables that no longer exist -- so what it
still does is set an output resolution preference and take the byte round trip
again. Whether anything is left once `OutputChoice` answers instead is step 12's
question, not a sequence to preserve on the way there.

**11. Steer the ADC PLL.** The band index and its `PLLAD_KS`/`FS`/`ICP` writes
become `Adc`'s, so the group has one owner on every path. **The band moved; the
RATE did not**, and it cannot yet: `getPllRate()` drives the debug pin through
the test bus and counts pulse ticks with FrameSync, which the engine has no
route to. So the sketch measures and the class decides, and the measurement
lands wherever FrameSync does.

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
