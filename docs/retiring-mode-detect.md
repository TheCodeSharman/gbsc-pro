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

**No standard survives anywhere.** The output is whatever output mode class the
user selected, and `PresetPreference` already enumerates those -- `Output480P`,
`Output960P`, `Output1080P` and the rest, with `OutputBypass` as pass-through.
`Mode480p` and `Mode576p` are already output modes. SD output needs no input
classification to reach it, so there is nothing for the byte to survive for.

The chip's own Mode Detect block is not the answer either. `MD_HD720P_CNTRL`,
`MD_SVGA_60HZ_CNTRL` and the rest are a fixed table of PC and broadcast
standards, and an arbitrary RISC OS raster matches none of them.

## What the byte conflates

Fifteen values carry five unrelated facts, which is why it has 134 references and
why one number reaching two subsystems means two owners.

| fact | replacement |
|---|---|
| which mode is on air | `SourceKey`, measured |
| colour space, YPbPr against RGB | the input selection, which the user made |
| scaling against bypass, the values 14 and 15 | the output mode, `OutputChoice` |
| interlaced against progressive | the engine's scan mode solve, measured |
| SD/HD/PAL/NTSC input branching | nothing -- one algorithm for every source |
| an SD output mode | `PresetPreference`, chosen by the user, already |

`sourceIsRgbhv()`, `scalingRgbhv()` and `rgbhvBypass()` read the byte for the
third row, so they are questions about the output, not the source.

`PresetLoad::ScalingRgbhvStandard` is the sharpest case: it exists only to make a
source take another standard's branch for its side effects.

## What the classifier is asked, and what answers instead

| shape | sites | answers instead |
|---|---|---|
| `== 0` / `> 0`, is there a signal | ~11 | `SourceMeasurement::countIsSource(measureSourceLines())` |
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

## The threshold dither is not carried forward

`getVideoMode()` writes twelve Mode Detect threshold registers dithered by
`random(-2, 2)` around a static captured on its first call. It runs only where
Mode Detect has named no standard while sync is present, and the technique is
sound in principle: a measured period sitting exactly on a threshold never
latches, because the lock counters need consecutive agreeing frames, so moving
the threshold a little each poll lets the period fall clearly to one side.

It is not needed here, and the board is the reason. The branch is unreachable on
an RGBHV source, so the only inputs that reach it are composite and S-Video --
and those do not arrive as themselves. Schematic sheet `AVSV2YPBPR` decodes them
through the ADV7280 and re-encodes them through the ADV7391, so they enter the
scaler as **regenerated, standard-conformant YPbPr**. A source whose timings have
just been reconstructed to broadcast standard does not sit on a threshold
boundary. The AV module's switch table agrees: S-Video, composite and YPbPr all
set the identical analog switch state, and only a register inside the ADV7280
tells the first two apart.

So the escape hatch guards a condition this board's own architecture removes.

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

**`Tv5725::SourceStandard` is next, and it has one caller.**
`doPostPresetLoadSteps()` constructs it from the byte and calls `apply()`, which
branches into an SD, progressive or HD arm. On the bench source none of that arm's
writes is in force:

| field | live | `applyProgressive()` writes | also written by |
|---|---|---|---|
| `IF_SEL_WEN` | 0 | 1 | nothing else |
| `VDS_V_DELAY` | 0 | 1 | `VideoProcessor` bring-up, 0 |
| `VDS_Y_DELAY` | 2 | 3 | `VideoProcessor` bring-up, 2 |
| `IF_HS_Y_PDELAY` | 3 | 3 | `InputFormatter` bring-up, 3 |

Three of the four hold the bring-up value, so bring-up runs after `apply()` and
wins. `IF_SEL_WEN` has no other writer and still does not hold what the arm
writes. Either way the arm's effect on this path is not observable, which is the
bar for deleting it.

`apply()` also reads `PLLAD_KS` back off the chip to pass as its own argument,
which is the register-as-input anti-pattern in miniature.

The experiment that settles it: delete the class, flash, diff a full register
dump against a known-good one taken first, and photograph. A difference names a
register that needs a real owner; no difference means the arms were dead.

### 4. Delete `SourceMeasurement::adopt()`

The only place the engine reads `PLLAD_MD` as an input. Custom presets are gone
and bypass *chooses* 1856 as a literal, so it becomes `hold(divider)` -- told,
not read.

### 5. Delete `getVideoMode()` and `videoStandardInput`

The byte goes with the function. Nothing holds a standard afterwards.

## The bar for each step

**Observable picture behaviour, on the paths the bench can exercise.** Not binary
equivalence and not code equivalence -- code with no observable effect on the
picture is deleted rather than preserved. The bench source is a RISC PC over
ModeServ, so a mode change and a sync-type change are both scriptable, and the
picture is photographable.

## Not in scope

- `recoverDivider()` and its gates. Measured working, and the trap they escape is
  real. They come out only when a replacement is shown to clear the same trap.
- The `HPERIOD_IF` railing state itself, which is a separate investigation.
