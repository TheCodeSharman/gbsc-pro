# The SD vsync window follows the sync type, not the standard

`SP_SDCS_VSST` places vertical sync in the source's frame, one source line per
count, and it is in the path only where the sync processor separates vertical
sync out of composite sync. The classification byte selects between two values
for it and answers a different question from the one the register asks.

RD-5725-1.1 gives both fields under *Sync separation control*: `SP_SDCS_VSST`
(s5_3f, with `SP_SDCS_VSST_REG_H` at s5_3b[2:0]) is the "SD vs. start position"
and `SP_SDCS_VSSP` (s5_40, high bits at s5_3b[6:4]) the stop.

## What writes it, and what it is worth

On the scaling path two writers land in one call of `doPostPresetLoadSteps()`:
`prepareSyncProcessor()` writes 4/1, and `Tv5725::SourceStandard::apply()`
writes 14/11 over it for standards 3, 4, 8 and 9. Everything else keeps 4/1.

Measured A/B/A with `tv-snap`, the same state photographed either side of each
change:

| source | sync | `SP_SOG_MODE` | 14 -> 4 |
|---|---|---|---|
| Wii 480p on `ypbpr` | sync on green | 1 | picture moves **down ~10 lines** |
| RiscPC 320x256@50 on `vga` | separate | 0 | **no change**, framing identical |
| RiscPC 320x256@50 on `vga` | composite | 1 | picture moves **down ~10 lines** |

The start is the whole of the effect. Holding the start at 4 and moving the stop
between 1 and 11 photographs identically, so the stop is inert at both values
this firmware writes.

Nothing else moves with it. On the Wii, `STATUS_SYNC_PROC_VTOTAL` 524,
`STATUS_SYNC_PROC_HTOTAL` 1096 against `PLLAD_MD` 1096, `HPERIOD_IF` 214 and
`STATUS_MISC_PLLAD_LOCK` 1 read the same at both placements, across six samples
a second apart at each. **A register dump cannot tell the two apart; only the
picture can.**

## The byte is a proxy for the wrong fact

What decides whether the value reaches the picture is `SP_SOG_MODE`, which
follows the sync type -- the same question `SyncOnGreen::inSyncPath()` already
asks of the separator level. The standard byte does not carry it:

- a composite-sync RGBHV source holds `PresetLoad::Rgbhv`, takes no arm of
  `SourceStandard`, and keeps 4 with the window live
- a component 480p source holds 3, takes the progressive arm, and gets 14

So two sources whose sync arrangement is identical are placed ten lines apart
because a classifier named one of them, and a source whose sync arrangement
makes the register inert has a value chosen for it anyway.

## It is a free pan, not a raster property

Swept on the Wii at 480p with the engine running, every write read back, and the
engine's 14 repeated between every treatment. The bottom edge of the cyan tile,
in rows of a rectified `tv-snap`:

| `SP_SDCS_VSST` | tile bottom | against 14 |
|---|---|---|
| 4 | 573 | +14 |
| **14** (five controls) | **559, 559, 559, 559, 559** | — |
| 24 | 545 | -14 |
| 44 | 515 | -44 |
| 84 | 456 | -103 |

Linear at **1.40 photo rows per count** across the whole 80-count range, with no
saturation and no break. The panel's 1080 output lines occupy about 680 photo
rows and the source's 480 lines are scaled to fill them, so 1.40 photo rows is
**one source line per count** exactly. `STATUS_SYNC_PROC_VTOTAL` 524 and
`VPERIOD_IF` 524 never move, and neither does the capture window the engine
solves -- `/geometry` holds `ov` 35, `ev` 480 throughout.

So the register is a vertical pan of the captured frame with no correct value,
and **the engine already owns vertical pan**. Two owners of one placement, one
of them chosen by a classification.

The bound is content, not lock: at 84 the tile's height drops from 489 rows to
441 because the frame has been panned off the top of what is captured.

## Deleting the arm is not free

`SourceStandard` is down to this one arm and this one register pair, and it has
one caller. Removing it outright leaves `prepareSyncProcessor()`'s 4/1 in force
on the one path that reaches it, which is measured above as ten lines of
downward shift on the Wii -- an offset the geometry engine does not know it has,
because it solves the capture window from its own measurement and this moves the
frame underneath it.

Both values hold lock on both live paths. Neither is a lock window on these
sources; the choice between them is placement.

## The bypass path has three writers, and one of them tracks the source

The scaling path settled on one constant. Pass-through has its own set, and they
are not the same question.

| writer | what it puts there | when |
|---|---|---|
| `HdBypass::applyForStandard()`'s computed path | `SyncProcessor::applySdVsyncPosition()`, 14/11 | every source with no arm of its own, at the bypass switch |
| `HdBypass::applySd()` / `applyProgressive()` | 250/1, 301/5, 520/522, 48/46 | standards 1..4, at the switch |
| `steerHdBypassVsyncWindow()` | `STATUS_SYNC_PROC_VTOTAL - 9`, or 1 outside 230..340 lines | continuously, 15 kHz sources only, rate limited to one write per 765 ms |

The third is why a register read after a pass-through excursion answers with a
number no constant in the tree contains. Measured on the bench source left
scaled after one: `SP_SDCS_VSST_REG_H` 1 with `_L` 46, which is **302** -- the
311-line RiscPC's count less nine -- against a stop of 11, the constant. Read as
the low byte alone it is 46 and looks like nothing at all.

**Read the pair as one value.** Each is a low byte plus a three-bit high field
at a different address, so the high half is easy to leave out and the answer is
then wrong by a multiple of 256 without looking wrong.

Leaving pass-through does not put it back -- nothing on the leave path writes
it -- but the next scaled load does, because `prepareSyncProcessor()` runs
`applySdVsyncPosition()`. Measured across a `/sc?~` and a source mode change
back to 320x256: 14/11.
