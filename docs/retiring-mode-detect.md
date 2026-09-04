# Retiring the standard byte

`getVideoMode()` and `rto->videoStandardInput` are the old detection: a
classifier that names one of fifteen video standards from Mode Detect's status
registers, and a byte that carries the answer. Both are being deleted.

Nothing downstream needs a standard. The engine identifies a source by
`Tv5725::SourceKey` -- the measured line count and a bucketed field rate -- which
is what this chip can actually see, and every geometry decision derives from
that pair. There is no SD-versus-HD or PAL-versus-NTSC choice left to make.

## What the byte conflates

Fifteen values carry five unrelated facts, which is why it has 134 references
and why a single number reaching two subsystems means two owners.

| fact | replacement |
|---|---|
| which mode is on air | `SourceKey`, measured |
| colour space, YPbPr against RGB | the input selection, which the user made |
| scaling against bypass, the values 14 and 15 | the output mode, `OutputChoice` |
| interlaced against progressive | the engine's scan mode solve, measured |
| SD/HD/PAL/NTSC branching | nothing -- one algorithm for every source |

`sourceIsRgbhv()`, `scalingRgbhv()` and `rgbhvBypass()` read the byte for the
third row, so they are questions about the output, not the source, and move with
it.

## What the classifier is asked, and what answers instead

| shape | sites | answers instead |
|---|---|---|
| `== 0` / `> 0`, is there a signal | ~11 | `SourceMeasurement::countIsSource(measureSourceLines())` |
| `== rto->videoStandardInput`, has it moved | 2 | `Geometry::sourceMoved()`, which already holds the solved count and rate |
| `videoMode == 0 && HSACT -> videoStandardInput` | 3 copies | a signal-present predicate; the idiom is already a workaround for the classifier being wrong |
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
It goes with the function, and is recorded here rather than preserved in code.

## Order

Each step stands alone and leaves the tree working.

1. The signal-present shapes, one site at a time. **Done for
   `updateSpDynamic()`'s sweep**, which is what the black-screen fix was.
2. Collapse the three copies of the `&& HSACT` idiom into one predicate.
3. The two "has it moved" sites, on to what the engine already holds.
4. Delete the dither.
5. Preset selection on to `SourceKey` and `OutputChoice`.
6. `SourceStandard::apply()` -- the ADC filter and sampling density it installs
   derive from the measured pair.
7. The values 14 and 15 on to the output mode.
8. Delete `getVideoMode()` and `videoStandardInput`.

## The constraint that makes this worth doing

One path per decision, owned by the engine. The standard byte is written by the
sketch and read by the engine, and every field both of them touch has two
owners -- which is the mechanism behind the black screen, the stale sync-type
configuration, and the ratcheted SOG level. Deleting the byte removes the
second owner rather than arbitrating between them.
