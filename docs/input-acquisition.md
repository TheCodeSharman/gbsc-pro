# Input acquisition

Goal: `runSyncWatcher()` and `rto->videoStandardInput` are both deleted, and the
responsibility they share -- keep video coming, and know what is coming -- has
two owners instead of none.

**`InputAcquisition`** sits ABOVE `Tv5725::` and owns the escalation, the input
policy and the no-signal report. **`Tv5725::VideoPath`** measures the source and
solves the scaler. `loop()` ends up with one call where it has two:

```
inputAcquisition.poll(millis());
```

`VideoPath::poll()` is called by IT rather than by `loop()`.

## One job seen from two ends

This was two pages -- one for the watcher's order, one for the standard byte's
semantics -- and each cited the other on every substantive point. They are not
two jobs. The byte's surviving readers are the branches the watcher runs, and
the watcher's parallel model of the source is what the byte exists to feed, so a
step that moves one moves the other whether it means to or not.

Both questions are still here and they are still different:

- **what the byte conflates, and what measurement replaces each fact** -- up to
  *The rule for every step*
- **the order the code moves in** -- *The order*, thirteen steps, with the
  byte's stages folded onto the steps that carry them

## The split is measurement against policy

**The engine measures and the layer decides**, and the division is not a
preference. `sourceMoved()` compares the source against the count and rate the
last solve ran against, read through the divider the engine itself chose -- so
anything else taking that reading needs a second copy of the divider, which is
the parallel model this page exists to delete. What the engine does not own is
what to do about the answer.

| | owns |
|---|---|
| `Tv5725::VideoPath` | measures the source, solves the scaler, publishes `sourceState()` |
| `InputAcquisition` | reads that verdict, runs the ladder, decides the input, reports "no signal out" |

**And the tick belongs to the caller.** `poll()` self-gates on its own
`DetectionIntervalMs`, so a layer with a cadence of its own puts two clocks in
the loop -- the ladder's and detection's. That is how `noSyncCounter` and
`idleRun_` came to count runs of different lengths. Whether the escalation wants
a slower cadence than detection is a bench question, and asking it at all
requires one owner of the tick.

## Why the classification is the wrong shape, not just badly placed

gbs-control was written for retro consoles, where the source is one of a short
list of known standards, and `videoStandardInput` is that list. A machine that
programs arbitrary modes -- a RISC PC does, over a monitor definition -- does not
fit it, and the failures show up as a source filed under a standard whose branch
then configures the chip for something else.

So the direction is not to classify better. It is to derive each thing the engine
needs from what it measures. `Tv5725::SourceKey` -- the measured line count and a
bucketed field rate -- is what identifies a source, because it is what this chip
can see: it locks to sync edges and cannot know the pixel clock.

**A video standard is replaced by two concepts, and they do not meet.**

1. **What the input is**, measured: sync type, line rate, frame rate, interlace.
   `Tv5725::SourceKey` is the identity -- line count and bucketed field rate --
   with `SyncType` and the engine's scan mode solve carrying the rest.

   **The count alone does not separate two very different sources.** The sync
   processor counts FIELDS, so a 576i console reads 310 against the RISC PC's
   progressive 311 at the same 50 Hz -- one line apart, and `SourceKey` cannot
   tell them apart.

   **Nothing measures interlace on this board.** `VPERIOD_IF` counts half-lines,
   so a 311-line progressive source reads 623 against 576i's 624 -- one apart, at
   the same line rate and field rate. The dedicated status bits agree with each
   other and are wrong: `STATUS_IF_INP_INT` and `STATUS_IF_INP_PAL_INT` both read
   1 for the progressive RISC PC, exactly as for the Wii. On separate sync the
   whole group reads 0 with `STATUS_IF_VT_BAD` set.
   `docs/investigations/vperiod-if-on-rgbhv.md`.

   So interlace is **not** a third fact the key can carry today. Whether that is
   the chip's limit or an artefact of comparing two sources that differ in more
   than interlace is undecided, and an interlaced RISC PC mode is what would
   decide it -- see `docs/bench-sources.md`. What the bench does show is that both
   sources are displayed correctly under the same treatment, so the pipeline has
   not so far needed the distinction.

2. **What output was chosen**, by the user: an output resolution, or pass-through.
   `PresetPreference` already enumerates these, `OutputBypass` being pass-through,
   and `OutputChoice` carries the selection.

Nothing derives the second from the first. **PAL against NTSC survives only as an
option**, where the user asks for the output frame rate to be matched to the
source's -- `matchPresetSource` and `presetIsPalForce60` inside `OutputChoice`.
Aside from that one option, the concept of a video standard is gone from the core
logic, and no code branches on one.

The chip's own Mode Detect block is not the answer either. `MD_HD720P_CNTRL`,
`MD_SVGA_60HZ_CNTRL` and the rest are a fixed table of PC and broadcast
standards, and an arbitrary RISC OS raster matches none of them.

## What the byte conflates

Fifteen values carry five unrelated facts, which is why it has 134 references and
why one number reaching two subsystems means two owners.

| fact | concept it belongs to | replacement |
|---|---|---|
| which mode is on air | input, measured | `SourceKey` |
| line rate and frame rate | input, measured | `SourceMeasurement` |
| interlaced against progressive | input, measured | the engine's scan mode solve |
| colour space, YPbPr against RGB | input, selected | the input selection |
| scaling against bypass, 14 and 15 | output, chosen | `OutputChoice`, `OutputBypass` |
| an SD output resolution | output, chosen | `PresetPreference` |
| SD/HD input branching | neither | nothing -- one algorithm for every source |
| PAL against NTSC | output, chosen, optional | `matchPresetSource` rate matching |

`sourceIsRgbhv()`, `scalingRgbhv()` and `rgbhvBypass()` read the byte for the
third row, so they LOOK like questions about the output.

**`sourceIsRgbhv()` is not, and defining it over the output is circular.** It
also answers *is this source RGBHV at all*, which detection establishes before
any output has been chosen and which gates the block that sets
`rto->isValidForScalingRGBHV` -- the input to the flag the output half would be
read from. Rebased that way the bench source is classified as PAL SD within a
minute, with a picture that still looks right.
`docs/investigations/the-rgbhv-question-is-two-questions.md`.

So the input half needs a home of its own BEFORE the byte can stop carrying 14.

`PresetLoad::ScalingRgbhvStandard` is the sharpest case: it exists only to make a
source take another standard's branch for its side effects.

## What the classifier is asked, and what answers instead

| shape | sites | answers instead |
|---|---|---|
| `== 0` / `> 0`, is there a signal | ~11 | a VALIDATED measurement -- see the warning below |
| `== rto->videoStandardInput`, has it moved | 2 | `VideoPath::sourceMoved()`, which holds the solved count and rate |
| the held-standard fallback | 1, was 3 | `standardForPresetLoad()` |
| selects a preset | 3 | `SourceKey` and `OutputChoice` |
| a label to print | 5 | the measured pair |

On an RGBHV source the classifier is not a classifier at all: that branch reads
two `STATUS_16` bits and returns the held byte back unchanged, or 0. It is a
sync-present test wearing a classifier's return type, and when those two bits go
quiet under a source the sync processor is still counting, the sketch's no-sync
handling walks a locked source off its settings.
`docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`.

## A range check is not a signal-present test

`SourceMeasurement::countIsSource()` only asks whether a count falls in
200..1300. **An unlocked sync processor produces garbage counts inside that
range**, so it cannot stand in for "there is a signal" wherever the answer gates
a recovery path.

Measured: gating `runSyncWatcher()`'s no-sync branch on it suppressed the
sketch's recovery for 80 s while the source was genuinely unlocked and the
counts read 216, 271, 276, 312, 305 -- every one of them in range, every one of
them meaningless. Reverted. The narrower use in `updateSpDynamic()` stands,
because that gate only withholds a sweep of the sync processor's settings rather
than the whole recovery.

So each of the eleven sites needs its replacement chosen by what it gates:

- **withholding a sweep or a tweak** -- a live count is enough, because being
  wrong costs one pass
- **withholding recovery** -- needs a count that is steady AND agrees with what
  the engine last solved against, so a source that has genuinely gone is still
  found

That distinction is the whole difficulty of this stage. `getVideoMode()`
returning 0 is at least *stable* when the source is absent; a raw count is not.

### The engine already computes the validated answer

`VideoPath::sourceMoved()` works out, on every idle poll, exactly what a
signal-present test needs: a plausible count (`countIsSource`), held steady
(`countHeld`, a `SteadySamples` run over `idleLines_`), and compared against
`solvedLines_` -- what the last solve actually ran against. It also separates a
count no source runs from no count at all, via `unusableCountArmed_`.

So the replacement is an engine method, and the rule it must follow is the one
already recorded for `sampleSteady()`: **publish the answer, do not recompute
it.** `countHeld()` mutates `idleRun_`, so a second caller double-advances the
run and corrupts the steadiness the first one depends on.

`VideoPath::sourceIsPresent()` is that method and it has landed. **It has no
caller**, because the gate it is for cannot move yet -- see the ordering below.

That gives the sketch three states where it has two:

| the engine sees | the sketch should |
|---|---|
| a steady count matching the last solve | never run recovery |
| a count no source runs, 97..137 | re-probe the sync type, which it now does |
| no count at all, 0 | run recovery |

`sourceMoved()` only runs while the engine is idle, so the value is stale during
a solve -- which is the right answer there anyway, since a solve in progress is a
source being worked on.

## The threshold dither is not carried forward

`getVideoMode()` writes twelve Mode Detect threshold registers dithered by
`random(-2, 2)` around a static captured on its first call. It runs only where
Mode Detect has named no standard while sync is present, and the technique is
sound in principle: a measured period sitting exactly on a threshold never
latches, because the lock counters need consecutive agreeing frames, so moving
the threshold a little each poll lets the period fall clearly to one side.

The branch is unreachable on an RGBHV source, which returns at the top of the
function, so it is reached only from the other inputs -- and those do not all
look alike.

**The regeneration argument covers composite and S-Video only.** Schematic sheet
`AVSV2YPBPR` decodes those through the ADV7280 and re-encodes them through the
ADV7391, so they arrive as standard-conformant YPbPr and cannot sit on a
threshold boundary. They are the only two inputs the AV module routes with
`adv_sw` true.

**YPbPr, RGBs and RGsB are direct analog paths** -- `adv_sw` false -- so nothing
reconstructs their timings and the argument above does not reach them. A console
on YPbPr can carry whatever it carries.

The dither is deleted anyway, because a getter that writes twelve registers is a
second owner of them against `ModeDetect::init()`, and because it leaves the
threshold off-centre by up to two wherever a mode does latch. What it costs is
**testable rather than theoretical**: a YPbPr source exercises the branch
directly, and an interlaced one exercises the SD arm below it.

## Why the watcher has to go rather than be tidied

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
`VideoPath::sourceIsPresent()` had to be written around.

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

**What acts outside the engine is injected, not called.** Loading a preset and
driving the OLED live in the sketch and the engine cannot reach them. They
arrive as function pointers, the way `useSyncTypeProbe()` already does.

**BUT INJECTION IS THE WRONG TOOL FOR INPUT SELECTION, AND THIS PAGE USED TO SAY
OTHERWISE.** It is right where the engine needs an ACTION it cannot reach --
probing the sync type is a TV5725 operation that happens to be implemented in
the sketch. It is wrong where the DECISION belongs elsewhere. Handing `VideoPath`
a `selectInput` callback leaves `VideoPath` deciding to move a mux on another
chip; the dependency is disguised rather than removed.

`Tv5725::` has the chip as its boundary. The input path is two muxes in series
and only one of them is on it -- `ADC_INPUT_SEL` is the TV5725's, `ASW_01`..`04`
are the HC32F460's, write-only over a UART. `InputSource` already lives outside
`Tv5725::` for exactly that reason, so an escalation that decides to change
input crosses a line the tree has already drawn.

The discriminator is whether the engine needs to DECIDE or only to REPORT. It
needs only to report: not acquired, and out of what it can do alone. What that
means -- try another source, say nothing more, tell the display -- is policy
about the board.

So the loop is not `VideoPath::poll()` calling out. It is `InputAcquisition`,
above `Tv5725::`, owning the escalation and the input policy and calling into
the engine for the scaler's share. `poll()` keeps its ordering contract and its
single entry point; what changes is who calls it and who owns the decisions
around it.

**It is a collaborator, not the composition root.** The product is more than
acquisition -- OSD, audio, IR, the web UI -- so the class named for the whole
would accrete all of it. `InputAcquisition`'s responsibility is narrower and
stateable in one line: decide where video comes from, and keep it coming.

**`Tv5725::SyncRecovery` is in the wrong namespace by this argument**, and
naming the layer is what settles it: ten of its eleven steps are TV5725
operations, which is why it reads as marginal, but the eleventh changes the
input and the list as a whole is policy about the board. It moves out of
`Tv5725::` into `InputAcquisition` at step 7.

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
| 9 | `== 150` or `% 900` | -- | `VideoPath::reacquireSyncType()`; parks at `0x07fe` if it finds V sync |
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
| `getVideoMode()`, `getStatus16SpHsStable()` | `VideoPath::sourceIsPresent()` | yes |
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
| the new-mode debounce | `VideoPath::sourceMoved()` | yes |
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
| `updateSpDynamic()`'s decision of when to hunt | `VideoPath`, off its own steadiness run | step 13, with the watcher |
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

**Three stages landed before this list**, and they are what made the rest
possible: the engine could not be given the watcher's job while the sketch still
owned the scan mode and the mode-change event.

### The engine owns the scan mode

`SourceMeasurement::lineDoublingFor()` reads the line count the engine already
measures; `VideoPath::solveScanMode()` holds it and writes the four registers.

Derived **before** `poll()`'s measurement gates. The input formatter's own
measurements are meaningful only once its scan mode matches the source, so a scan
mode left wrong makes the gates fail and one derived after them is never reached.
The sync processor counts the source directly and is indifferent to the scan mode.

### The engine owns the mode-change event

`VideoPath::sourceMoved()` remembers the line count the last solve ran against and
re-arms when a settled count differs, so a solve that completed against a
mid-transition count is corrected rather than left.

**It keeps its own steadiness count, and must.** Reusing
`sampling_.sampleSteady()` also fills `unmeasurableRun_`, the gate holding
`recoverDivider()` back, which makes the recovery fire on the first poll of the
next mode change and infer a divider from a count that was never a measurement.

### The chip's own interrupt triggers it

`sourceMoved()` reports `interrupt` as well as `count` and `rate`. The line count
stays the confirmation of what the mode changed to and when it settled.

### Retiring the byte is spread across the list, not a step of its own

**Entirely, everywhere.** There is no second role it keeps. Every reference that
feeds geometry is a classification standing where a measurement belongs, and
each becomes a derivation from something the engine measures, as the scan mode
did -- so the references come out with the branches that read them rather than
being unpicked one at a time.

Landed so far:

- the sync-processor sweep no longer fires on the classification alone
  (`SyncSearch::shouldSweepSyncProcessor()`)
- the three copies of the held-standard fallback are one
  (`standardForPresetLoad()`)
- the Mode Detect threshold dither is deleted
- both of `updateSpDynamic()`'s hunt branches read one `searching` value, so
  neither fires on a source the sync processor is counting
- `VideoPath::sourceIsPresent()`, the measurement that replaces the
  classification at the no-sync gate -- the method, not yet the wiring

**The count is not the progress.** 86 occurrences on 76 lines is roughly
where it has sat, because what has landed so far is structural -- one
held-standard fallback where there were three, the threshold dither deleted,
`sourceIsPresent()` written. The references leave in two blocks, at steps 10 and
12: the byte is deleted LATE, once the RGBHV block has moved and nothing reads
it, rather than being unpicked reference by reference from inside a function
that is going anyway.

**Next is the SOG level.** Wiring `sourceIsPresent()` into that gate is measured
to fix the fault it is for and to leave the unit stuck behind a starved sync
separator, so the two have to land in that order -- steps 3 and 4 below.

Each step below extracts one named operation, merges it into the idle pass, and
deletes the sketch's copy in the same commit.

deletes the sketch's copy in the same commit.

**1. One owner for the sync separator level.** `Tv5725::SyncOnGreen` holds the level and
owns `ADC_SOGCTRL`; `rto->currentLevelSOG` and `setAndUpdateSogLevel()` go. No
policy moves. It separates the two facts that variable carried — the level
*chosen* for a source the ADC has not been brought up for, and the level *in
force* — which is why a straight substitution would have been wrong.

**2. The detection cadence.** `poll()` takes the clock and the idle detection
pass runs on `VideoPath::DetectionIntervalMs`. The steadiness run is counted in
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
own run, and `VideoPath::sourceIsPresent()` replaces the classification at the
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

**7. `InputAcquisition`, and the escalation list it holds.** The ordered set of
named recoveries replaces `% 27`, `% 32`, `== 38`, `% 150` and `% 413` -- and it
lands in a class of its own above `Tv5725::`, because a list with no owner is
what `SyncRecovery` is today. `SyncRecovery` moves out of `Tv5725::` with it.
**Step 4 waits on this**, because wiring the gate is what lets the branch
advance far enough to reach these.

**This is the step that inverts the call.** `InputAcquisition::poll()` takes the
tick, asks the engine for `sourceState()`, and runs a rung when the answer is
not acquired. `VideoPath::poll()` stops being called from `loop()` on the same
pass and becomes the engine's share of one sequence, which is what removes the
second clock.

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
| `% 150` | reacquire the sync type, put the coast and clamp windows back, `updateSpDynamic(1)`, nudge, re-acquire the sync separator level, reset the sync processor, reset mode detect | `VideoPath::reacquireSyncType()`, the two window defaults, `ModeDetect::nudge()`, `SyncOnGreen::reacquire()`, `SyncProcessor::reset()`, `ModeDetect::reset()`, `SyncProcessor::applyForSearch()` |
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
route out. `VideoPath::reacquireSyncType()` is that rung — it applies the probe's
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

**And the byte's two largest branches come out here**, because this is the
block that reads them.

### `applyPresets()` is an output selection wearing a preset's name

There are no preset tables. What the function does now is dispatch on the
standard byte to one of three outcomes -- compute an output and load it, name an
HD standard and return, or switch to RGBHV bypass -- and every one of those is a
statement about the OUTPUT. The byte is only how the caller says which.

    result 1,2,3,4,8,9,14   outputChoiceFor(result) -> loadComputedPreset()
    result 5,6,7,13         hold the standard, setOutModeHdBypass(false)
    result 15               bypassModeSwitch_RGBHV()

`OutputChoice` already carries the first row's answer and `OutputMode::isBypass()`
already names the sentinel the engine holds for the other two, so the dispatch
is a third spelling of a fact those two classes own. What the rows do not share
is WHICH bypass -- HD or RGBHV -- and that is the piece neither class holds.

So this function is not renamed, it is dissolved: once the caller passes an
output rather than a standard, the first row is `loadComputedPreset()` alone and
the other two are the bypass switches called directly. It goes with the byte
rather than before it, because the dispatch is the byte's last real reader.

### Bypass is ONE output mode, and it picks its own register path

**Bypass is a single concept: take the source and leave the scaler out of it.**
Whether that ends in the HD path or the RGBHV path is an implementation detail
of the bypass mode, decided from what the source measures -- not two output
modes for a caller to choose between.

**`Tv5725::HdBypass` is NOT that class, and renaming it to `Bypass` is refused.**
The HD bypass channel is a block RD-5725-1.1 names -- in the descriptions of
`SFTRST_HDBYPS_RSTZ`, `DAC_RGBS_BYPS2DAC`, `DIGOUT_BYPS2PAD`, `DIGOUT_ADC2PAD`,
`OUT_SYNC_SEL` and `OUT_BLANK_SEL_1` -- and it is named for what it is for:
carrying a high resolution around the scaler. The class owns s1 0x30..0x55,
which is that block, so it keeps the datasheet's name.

The two routes are different silicon, not two configurations of one block:

| | video path | the HD bypass channel |
|---|---|---|
| HD bypass | `DAC_RGBS_BYPS2DAC` 1, HD bypass channel to DAC | carries the video |
| RGBHV bypass | `DAC_RGBS_ADC2DAC` 1, "ADC (with decimation) to DAC" | **not in the video path** |

`bypassModeSwitch_RGBHV()` releases the block and sets `OUT_SYNC_SEL` to 1 all
the same, which selects "H/V sync output are from HD bypass" -- so on that route
the block is the output sync generator and nothing else.

So one output mode over two routes is the shape, and the class is one of the
routes' blocks. What made the class look general is `applyRgbhvPll()`, which
writes `PLLAD_KS` and `PLLAD_FS` -- ADC registers and nothing of this block --
so it belongs to `Adc`, and moving it leaves `HdBypass` holding only its own.

**It is NOT `Adc::postDividerFor()` under another name, and collapsing the two
would change the values.** `postDividerFor()` is RD-5725-1.1's KS crossover
table read against a frequency; `applyRgbhvPll()` picks a `(KS, FS)` pair off
the measured line count at 532 and 810. Against the divider standard 13
installs immediately before it, the two disagree across the whole 532..809 band
-- KS 2 against KS 3 -- and no divider maps those line counts onto the
datasheet's bands at all: 20 MHz would fall at 532 lines only with MD 627, and
40 MHz at 810 lines only with MD 823. `FS` is a VCO gain rather than a divider
and moves 1, 0, 1 across the three rows, which no frequency table produces.
Where the thresholds came from is unrecorded.

What is still split, and what has to go:

| split | today | belongs to |
|---|---|---|
| two entry points | `setOutModeHdBypass()`, `bypassModeSwitch_RGBHV()` | one `apply()` on the mode |
| two flags | `rto->outModeHdBypass`, `videoStandardInput == 15` | the resolved `OutputMode` |
| the path choice | `applyForStandard()` branching on the standard byte | the measured source |

**And the path choice can go entirely, because one route serves both.** The HD
bypass channel carries an arbitrary RGBHV source -- measured, RISC PC on `vga`
at 800x600@60 -- once its raster is derived from the divider the engine already
holds rather than frozen per standard. Entering that route on such a source
today gives no signal, and the cause is that `applyForStandard()` has no arm for
14 or 15, so the block keeps `enable()`'s resting timing.
`docs/investigations/one-bypass-route-carries-rgbhv.md`.

That makes `ADC2DAC` the one to retire rather than the one to generalise: the HD
channel is the only route with a matrix and a dynamic range converter in
circuit, so it is the only one that can carry a component source at all.

**The passthrough preference does not reach both halves today, which is the bug
this shape removes.** `presetPreference == OutputBypass` is read in the sync
watcher's new-mode block and calls `setOutModeHdBypass()` -- the HD path, always.
The RGBHV path is reached from the standard byte holding 15 and from nowhere
else, so a user asking for pass-through on an RGBHV source gets the other one.

**And it dissolves `sourceIsRgbhv()`.** That predicate exists mostly to decide
scaled against bypassed for a source with no preset. Once the user chooses
bypass and `bypassCanBeDisplayed()` says whether the display can show it, there
is nothing left for it to decide -- which is what
`docs/investigations/the-rgbhv-question-is-two-questions.md` found no way
through while the two questions stayed welded together.

**11. Steer the ADC PLL.** The band index and its `PLLAD_KS`/`FS`/`ICP` writes
become `Adc`'s, so the group has one owner on every path. **The band moved; the
RATE did not**, and it cannot yet: `getPllRate()` drives the debug pin through
the test bus and counts pulse ticks with FrameSync, which the engine has no
route to. So the sketch measures and the class decides, and the measurement
lands wherever FrameSync does.

**12. Delete `getVideoMode()` and `videoStandardInput`**, which by then have
no readers. *What the byte conflates* above has what each of its fifteen values
carried and what replaced it.

The byte goes with the function, and nothing holds a standard afterwards. Two
things come out with it.

**`SourceMeasurement::adopt()`** is the only place the engine reads `PLLAD_MD`
as an input. Custom presets are gone and bypass *chooses* 1856 as a literal, so
it becomes `hold(divider)` -- told, not read.

**`Tv5725::SourceStandard` has one caller**, and it is the one piece of this
step that is not mechanical. `doPostPresetLoadSteps()` constructs it from the byte and calls `apply()`, which
branches into an SD, progressive or HD arm. `apply()` also reads `PLLAD_KS` back
off the chip to pass as its own argument, which is the register-as-input
anti-pattern in miniature.

**Its SD arm is live on YPbPr, so it is not dead code**, and its progressive arm
is overwritten on RGBHV. Measured on a Wii at 576i against the RISC PC on the
same build:

| field | RISC PC | Wii | `applySd()` YPbPr branch writes |
|---|---|---|---|
| `IF_HS_Y_PDELAY` | 3 | **2** | 2 |
| `VDS_Y_DELAY` | 2 | **3** | 3 |
| `IF_HS_TAP11_BYPS` | -- | **0** | 0 |

The RISC PC holds the bring-up values and the Wii holds the arm's. On RGBHV the
progressive arm's writes are not in force at all -- `IF_SEL_WEN` reads 0 where it
writes 1, with no other writer -- so **it is dead on one path and live on the
other**, and deleting the class changes the component picture.

**And two more effects survive on every path.**

Everything else it writes has a later owner. `PLLAD_KS` is overwritten by
`Adc::applySampleRate()`, which `VideoPath::writeSampling()` calls on every mode
change and which derives the post divider from `divider x lineRate` -- the
measurement, correctly. The IF and VDS delays are overwritten by bring-up. Both
owners are the right ones, so those writes are already dead.

What is left:

| effect | who needs it | note |
|---|---|---|
| `rto->osr`, the returned oversample | `geometry.inputTimingsChanged(osr)` reads it | a real input to the engine |
| `ADC_FLTR` | nothing else writes it on this path | the analog corner, 40 MHz on both sources |
| the YPbPr luma/chroma delays | the component picture | live, measured above |

So deleting the class means giving those two an owner, and both are **policy
questions with picture consequences rather than derivations**:

- **The wanted oversample.** The arms ask for 4 on interlaced SD ("least
  horizontal detail, so the most room to oversample"), 2 on progressive, 2 by
  default. Keyed to the line rate instead, the bench source at 15.6 kHz would ask
  for 4 where the standard-3 branch currently gives it 2. That changes sampling
  density on the one path that can be judged.
- **The analog filter corner.** 40 MHz is the narrowest the part offers and
  110 MHz is what a line carrying HD detail needs. The corner properly follows
  the sample clock, but where it should step is a sharpness judgement.

Neither should be invented. `docs/capture-limits.md` covers the trade `PLLAD_MD`
makes between sampling density and reaching the end of the line, and the picture
is the instrument for both.

**13. Delete `runSyncWatcher()`**, and `loop()` calls
`inputAcquisition.poll(millis())` alone.

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

**It does not wait for `InputAcquisition`.** The call is one the sketch makes,
so `applyInputSelection()` can take the engine's entry point today -- and it has
a bug with a reproduction to answer for: nothing forgets the sync type on an
input change, so `ypbpr` and back to `vga` leaves `SP_SOG_MODE` 1 on a
separate-sync source counting 97 lines, with no route out because the count
never moves to arm a re-probe. `applyPresets()` stops being how the engine hears
about a source at step 10; the input event is earlier and cheaper than that.

### Most of the ladder is one operation, applied in fragments

The rungs look like eleven strategies. They are not. Rung 8, the `% 150` block,
does all of this in one pass:

    setHsyncOverflowProtect(false)   undoes rung 7
    applyDefaultCoastWindow()        rung 2
    applyDefaultClampWindow()
    updateSpDynamic(1)               rung 3
    ModeDetect::nudge()              rung 6
    SyncOnGreen::reacquire(...)      rungs 1 and 11
    SyncProcessor::reset()
    ModeDetect::reset()

**Six of the eleven rungs are strictly contained in one of the others.** So the
ladder tries fragments of the acquisition, one at a time, and eventually tries
all of it -- and the order the fragments come in is historical rather than
principled, because they are not alternatives to each other.

Only four rungs are a genuinely different act: `ReleaseCapture`, which is frame
buffer state rather than a measurement; `HoldClamp`, which is not the default
clamp window; `ReprobeSyncType`; and `ToggleInput`, which the section below
shows is an input event.

**The engine does not work in fragments.** A source event measures everything
from scratch, which is what makes it possible to say what the engine believes
and why. An escalation that re-does one measurement at a time is the older
shape -- nursing a part-configured chip back into lock -- and it survives only
because acquisition was never one operation to call.

So the ladder collapses to about three states rather than eleven: acquire;
acquire with the blocks reset first; change the input. `SyncRecovery` as it
stands preserves the eleven deliberately, because a list that reproduces today's
positions is reviewable against today's behaviour -- but it is an intermediate,
not the destination, and the destination is much smaller.

**The one argument for keeping cheap early rungs is not overreacting to a brief
dropout**, and it is weaker than it looks: the branch only runs when the source
is ALREADY unlocked, so there is no picture being protected, which is the usual
reason to prefer a nudge over a re-acquire. What survives of it is the free
first pass, which is one dropped measurement rather than a strategy.

### The input toggle is an input event, not the ladder's last rung

The ladder's rungs all mean *try harder to acquire the source on this input*.
`ToggleInput` does not: it means *give up on this input and look at the other
one*, which is a different kind of act and is literally an input change. Four
things follow from where it sits that stop making sense once that is said, and
all four are things this page has separately recorded as awkward:

- **It is the only rung gated on user intent.** `detectionMayChangeInput()` asks
  whether the user chose this input, which is a policy about commands, not about
  acquisition. No other rung asks anything of the kind.
- **It is the only rung that must stop the ladder when it SUCCEEDS**, and that
  is the whole reason a stop-escalating concept was needed. It stops because it
  changed the input, so everything the ladder had established is stale.
- **It is out of order.** `ReopenSogSeparator` sits after it at 450 -- so the
  mux moves before the cheaper, less destructive separator reopen is tried.
  Remove the toggle and the remaining list is cleanly cheapest to most
  expensive, ending on the reopen.
- **It is why `0x07fe` had to be a message.** The ladder had no way to say
  "promote this decision" except by writing a value another block reads.

So the shape is one level up, and it is the same collapse as the section above.
The engine already names three ways the problem moves, and only one of them is
missing:

    inputMuxChanged()      the input the source arrives on is now unknown
    inputTimingsChanged()  same input, the source moved
    outputModeChanged()    the user picked a different output resolution
    poll()                 runs the ladder while sourceIsPresent() is false

`VideoPath::inputMuxChanged()` is the entry point the sketch is missing --
called by `applyInputSelection()` from the menu, the remote and HTTP, and called
by `InputAcquisition` when the ladder exhausts. It forgets the sync type,
because a different input shares none of it, and the escalation position is
`InputAcquisition`'s to forget rather than the engine's.

**And it dissolves the cadence question. NOTHING TERMINATES:** a unit with no
detectable source keeps looking until it finds one, so there is no end state to
design and no sentinel to write.

    nothing chosen    cycle the ladder on this input; when it exhausts, move to
                      the next source and start again. Sweep for ever.
    input chosen      cycle the ladder on that input for ever. Never move.

The second row is the rule that an explicit selection is a command: a chosen
input is selected whether it has a signal or not, so there is nowhere to promote
to. `detectionMayChangeInput()` is already that distinction, which is why it is
the gate on the toggle and on nothing else.

**"No signal out" is a report of state, not a terminus.** Where nothing is
chosen it falls naturally at the end of a full sweep of the sources, which is
the longer timer the 2046-pass expiry approximated; where an input was chosen it
is simply true until it is not. Nothing stops either way.

**And the sweep is over SOURCES, not the two ADC inputs.** `selectOtherInput()`
moves `ADC_INPUT_SEL` alone, so it can only ever try the other half of one mux
-- the HC32's analog switches do not move with it, and five of the six inputs
need the frame sent to them. A sweep that means what it says goes through
`applyInputSelection()`, which is also what `applySavedInputSource()`'s silent
`default:` branch should do when nothing was ever stored.
*Input selection is the same collapse* below, and `applySavedInputSource()`.

**Not measured, and it is a design note rather than a finding.** What supports
it is that four separately-recorded awkwardnesses have one cause; what would
test it is the bench reproduction this page already names -- `/input?src=ypbpr`
with nothing plugged into it is a genuinely absent source, reachable without a
bench trip, and `/input?src=vga` recovers it.

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

The two cover different arms, and the SD one is not optional here: the RISC PC
over ModeServ covers arbitrary rasters, both sync types and progressive, while a
Wii on YPbPr covers sync on green, interlace and component colour -- which is
the arm `SourceStandard::applySd()` is live on and step 12 has to account for.

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
- `recoverDivider()` and its gates. Measured working, and the trap they escape
  is real. They come out only when a replacement is shown to clear the same
  trap.
