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

**IT DOES NOT CLEAR THE BENCH CASE, AND THE REASON IS THE RUN.**
`ContradictedArmPasses` is 64 CONSECUTIVE passes and `contradictedPasses_`
resets on any pass that does not see the contradiction. In this state
`STATUS_IF_VT_OK` flickers -- the line doubler is also the departing mode's, so
the formatter measures a doubled 1067 and drops the OK bit -- and
`InputFormatter::verticalPeriod()` returns 0 whenever it is clear. A run
counter that resets on a gap cannot see a fault that flickers.

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
