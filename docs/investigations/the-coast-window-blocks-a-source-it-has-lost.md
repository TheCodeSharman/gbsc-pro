# The coast window blocks a source it has lost

`SP_H_CST_SP` is placed from the SOLVED line rate, in
`VideoSourceAcquisition::holdSolvedSource()`, as `0.968 x (27e6 / lineRate)` --
a value spanning almost the whole line. A mode change that raises the line rate
therefore leaves a window wider than the arriving line, and the sync processor
never finds an hsync edge to count.

## The frozen count

Measured on `vga` composite, 320x256@50 -> 640x512@50 by ModeServ, no other
change:

| | reading |
|---|---|
| `SP_H_CST_SP` | 1672, the departing mode's, against an arriving line of ~1008 |
| `STATUS_SYNC_PROC_VTOTAL` | **308 in every one of 24 readings across 45 s** |
| `VPERIOD_IF` | 533, the arriving frame |
| `PLLAD_MD` | 2200, the departing mode's |

**A register that dithers going perfectly static is one that has stopped.** The
same count reads 305..309 on a healthy source. Writing `SP_H_CST_SP` to 976 by
hand had it reading 531..835 within 2.5 s, with the engine installing the
solved 1424 divider straight after -- replicated twice, on two builds.

## Nothing in the engine can see it

The count freezes at the value the last solve measured, so it does not go
WRONG -- it equals `solvedLines_` exactly, and every arm in
`VideoSourceAcquisition::sourceMoved()` compares against that. The one arm that
could fire, `countContradicted()`, is refused by its own
`lines == solvedLines_` guard, which exists so an interlaced source whose two
instruments reconcile by no factor cannot re-arm for ever.

The formatter's frame is the second instrument and it followed the source,
623 -> 533. `SourceMeasurement::frameMovedFrom()` now lets the guard tell a
counter that STOPPED from a source standing still.

**THE CASE THE GUARD EXISTS FOR IS UNAFFECTED, AND THAT IS MEASURED RATHER
THAN ARGUED.** The Wii in 576i over `ypbpr` is the source it was added for:
50 samples across 75 s read `VPERIOD_IF` 624, `STATUS_SYNC_PROC_VTOTAL` 310
and `STATUS_IF_VT_OK` 1, one value each with no spread. The count is
contradicted throughout -- 625 reconciles against 310 by neither factor -- and
no arm fired in the window, because the formatter's frame never moves.

**THE ARM FIRES, AND WHAT IT REACHES IS A SECOND FAULT.** Watched for 220 s
on 640x512@50 composite, 188 register samples beside the console:
`STATUS_IF_VT_OK` reads 1 in 166 of 188 with 11 transitions, so the run does
accumulate, and `TIMINGS change (contradicted count)` fires. The engine then
re-measures and solves the mode CORRECTLY -- line rate 26786, divider 1424,
`source key: 534@50.16/613??`, sync pad driven -- and about 1.6 s later the
count reads 684 against the solved 534 and contradicts again.

So the frozen state is converted into a cycle rather than into a picture. The
divider walks 1424/1446/1746/2040/2200 and the window 256/974/975/982/1672
across the run. In the solved state both are RIGHT for the arriving mode --
the window is 974..982 against the 976 its own arithmetic asks for -- and the
count still goes bad, which is the fault
`tools/gbsc-pro-hwtest/sweep_coast.py` attributes to the coast PAIR: 7 of 31
readings inside the formatter's frame at 7/6 against 42 of 42 at 1/1.

## What the window does while the source is LOCKED is not settled

Walked across the register on a settled source, 3 readings a step, automation
UNFROZEN:

| source | line, 27 MHz counts | window walked | count |
|---|---|---|---|
| `vga` 320x256@50 composite | 1728 | 40 .. 3976 | 308 at every step |
| `ypbpr` Wii 480p, sync on green | 858 | 62 .. 958 | 524 at every step |
| `ypbpr` Wii 480p | 858 | 1086 | **718**, the count left the source |

So the Wii breaks above its line and the RiscPC does not break at all, and
neither shows a lower bound.

**THAT LOWER HALF DISAGREES WITH
`the-coast-window-is-placed-once-the-rate-is-known.md`, WHICH MEASURED BETTER.**
That page reads 256 on the same Wii at 480p as pinning the count at 521 in 49
of 60 samples against a true 524 -- a systematic three-line shortfall -- where
this walk reads 524 in 3 of 3 at 190 and at 62. Sixty frozen samples a row
beats three unfrozen ones, so the derived window stands and must not be deleted
on the strength of the walk above. What the disagreement means is open: the
candidates are the freeze, the sample count, and the firmware having moved.

## What is settled

- A window left describing a line the source is not sending can stop the
  processor counting, and the count then holds its old value rather than going
  wrong.
- `VideoPath::prepareToMeasure()` writes the reference window alongside the
  reference vertical blank for that reason -- the same discipline as the
  reference divider beside it.
- **That fix cannot reach this fault**, because `prepareToMeasure()` runs only
  on a mode change the engine has armed, and this is the fault that stops one
  being armed.

## The pair cannot be a constant, and that is measured on both ends

`sweep_coast.py` on a settled 320x256@50 composite source scores every pair
from 7/6 down to 1/1 identically -- count steady, blanking live, acquired at
all nine -- so the bench mode says nothing against a short pair, and 1/1 is
what 640x512@50 composite needs. That reads as an argument for making 1/1 the
composite pair outright and deleting the choice.

**The Wii refuses it.** Held at 1/1 over `ypbpr` in 576i, 50 samples across
45 s:

| coast | `STATUS_SYNC_PROC_VTOTAL` 310 | spread | `VPERIOD_IF` | `STATUS_IF_VT_OK` |
|---|---|---|---|---|
| 7/2, the derived pair | 50 of 50 | -- | 624, one value | 1 throughout |
| 1/1 | **16 of 50** | 308..316 | 621..627 | 0 and 1 |

So both candidate constants are refused by sync on green -- 0 pre was already
known to destroy its lock, and 1/1 degrades its count the same way. The pair
is a property of the source and has to be chosen per source, which is what
makes this hard rather than a constant nobody had tried.

## What the mode file does not predict

Blanking before the vertical sync is the obvious candidate, because the pre
coast is applied there and 7 lines of it could overrun a short one. It does
not separate the cases, from `RetroScaler-Acorn.mdf`:

| mode | vsync | bottom border + front porch | at 7/6 |
|---|---|---|---|
| 320x256@50 | 3 | 20 | works |
| 640x512@50 | 3 | **1** | fails |
| 240x352@70 | 2 | 37 | works |
| 640x480@60 | 2 | 10 | works |
| 800x600@60 | 4 | **1** | works |

800x600@60 has the same single line before its vertical sync as the mode that
fails, and carries the derived pair without trouble.
