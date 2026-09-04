# Retiring the standard byte

Goal: the geometry engine derives every input it solves against from what it
measures, and the sketch stops being a second owner of that decision.
`getVideoMode()` and `rto->videoStandardInput` are deleted, not fixed.

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
third row, so they are questions about the output, not the source.

`PresetLoad::ScalingRgbhvStandard` is the sharpest case: it exists only to make a
source take another standard's branch for its side effects.

## What the classifier is asked, and what answers instead

| shape | sites | answers instead |
|---|---|---|
| `== 0` / `> 0`, is there a signal | ~11 | a VALIDATED measurement -- see the warning below |
| `== rto->videoStandardInput`, has it moved | 2 | `Geometry::sourceMoved()`, which holds the solved count and rate |
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

`Geometry::sourceMoved()` works out, on every idle poll, exactly what a
signal-present test needs: a plausible count (`countIsSource`), held steady
(`countHeld`, a `SteadySamples` run over `idleLines_`), and compared against
`solvedLines_` -- what the last solve actually ran against. It also separates a
count no source runs from no count at all, via `unusableCountArmed_`.

So the replacement is an engine method, and the rule it must follow is the one
already recorded for `sampleSteady()`: **publish the answer, do not recompute
it.** `countHeld()` mutates `idleRun_`, so a second caller double-advances the
run and corrupts the steadiness the first one depends on.

```
bool Geometry::sourceIsPresent() const;   // set by sourceMoved(), read by the sketch
```

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


## Stages

Each is a bounded commit plus its test, cherry-pickable on its own.

### 1. The engine owns the scan mode -- DONE

`SourceMeasurement::lineDoublingFor()` reads the line count the engine already
measures; `Geometry::solveScanMode()` holds it and writes the four registers.

Derived **before** `poll()`'s measurement gates. The input formatter's own
measurements are meaningful only once its scan mode matches the source, so a scan
mode left wrong makes the gates fail and one derived after them is never reached.
The sync processor counts the source directly and is indifferent to the scan mode.

### 2. The engine owns the mode-change event -- DONE

`Geometry::sourceMoved()` remembers the line count the last solve ran against and
re-arms when a settled count differs, so a solve that completed against a
mid-transition count is corrected rather than left.

**It keeps its own steadiness count, and must.** Reusing
`sampling_.sampleSteady()` also fills `unmeasurableRun_`, the gate holding
`recoverDivider()` back, which makes the recovery fire on the first poll of the
next mode change and infer a divider from a count that was never a measurement.

### 2b. Trigger on the chip's own interrupt -- DONE

`sourceMoved()` reports `interrupt` as well as `count` and `rate`. The line count
stays the confirmation of what the mode changed to and when it settled.

### 3. Retire `videoStandardInput` -- IN PROGRESS

**Entirely, everywhere.** Not "on the input side": there is no second role it
keeps.

Every reference that feeds geometry is a classification standing where a
measurement belongs. One group at a time, each becoming a derivation from
something the engine measures, as the scan mode did.

Landed so far:

- the sync-processor sweep no longer fires on the classification alone
  (`SyncSearch::shouldSweepSyncProcessor()`)
- the three copies of the held-standard fallback are one
  (`standardForPresetLoad()`)
- the Mode Detect threshold dither is deleted
- both of `updateSpDynamic()`'s hunt branches read one `searching` value, so
  neither fires on a source the sync processor is counting

**Next is `Geometry::sourceIsPresent()`**, replacing the classification at the
no-sync gate as described above. It is the first step of moving
`runSyncWatcher()` into the engine, which the last section sets out.

**`Tv5725::SourceStandard` also has to go, and it has one caller.**
`doPostPresetLoadSteps()` constructs it from the byte and calls `apply()`, which
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
`Adc::applySampleRate()`, which `Geometry::writeSampling()` calls on every mode
change and which derives the post divider from `divider x lineRate` -- the
measurement, correctly. The IF and VDS delays are overwritten by bring-up. Both
owners are the right ones, so those writes are already dead.

What is left:

| effect | who needs it | note |
|---|---|---|
| `rto->osr`, the returned oversample | `geometry.modeChanged(choice, osr)` reads it | a real input to the engine |
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

### 4. Delete `SourceMeasurement::adopt()`

The only place the engine reads `PLLAD_MD` as an input. Custom presets are gone
and bypass *chooses* 1856 as a literal, so it becomes `hold(divider)` -- told,
not read.

### 5. Delete `getVideoMode()` and `videoStandardInput`

The byte goes with the function. Nothing holds a standard afterwards.

## The bar for each step

**Observable picture behaviour, on the paths the bench can exercise.** Not binary
equivalence and not code equivalence -- code with no observable effect on the
picture is deleted rather than preserved.

**Which branch a change is judged against is decided by which source is plugged
in**, so check `docs/bench-sources.md` before calling one untestable. The RISC PC
over ModeServ covers arbitrary rasters, both sync types and progressive; a Wii on
YPbPr covers sync on green, interlace and component colour, which is the SD arm
this plan has to account for. The picture is photographable either way.

## Where this is going: runSyncWatcher() belongs in the engine

`runSyncWatcher()` is 889 lines keeping a **parallel model of the source**, and
it calls into the engine exactly once (`geometry.sourceInterrupted()`). What it
touches most is what the engine already computes:

| it touches | times | the engine's version |
|---|---|---|
| `noSyncCounter` | 23 | `idleRun_`, a `SteadySamples` run |
| `continousStableCounter` | 15 | the same run, read the other way |
| `videoStandardInput` | 14 | being retired |
| `currentLevelSOG` and the three SOG functions | 13 | nothing yet -- and it is a measurement |

Two owners of one model, which is the register problem one level up.

**The split is by responsibility, and it is the two-concept model again.**
Anything that *measures the input* moves in: sync presence, the steadiness
counters, the SOG slicer level, the coast and clamp positions. The engine already
owns the sync type, the scan mode and the divider. What *acts* on the answers
stays in the sketch: selecting an input, loading a preset, entering bypass,
driving the OLED.

Order, each landable on its own:

1. `Geometry::sourceIsPresent()`, replacing the classification at the no-sync
   gate.
2. The steadiness counters -- 38 references become reads of engine state instead
   of a parallel count.
3. The SOG level, which is the slicer for the source's own sync and the last
   piece of acquiring a source that the engine does not own.
4. What remains is a thin policy loop.

**A structural constraint on all of it:** `poll()`'s measuring branch runs only
when `modePending_`. A sync watcher has to run always, so this work extends the
idle branch, not the pending one.

## Not in scope

- `recoverDivider()` and its gates. Measured working, and the trap they escape is
  real. They come out only when a replacement is shown to clear the same trap.
- The `HPERIOD_IF` railing state itself, which is a separate investigation.
