# `SP_H_PULSE_IGNOR` decides what the sync processor counts, and one value is right

The field sets the width below which a horizontal pulse is ignored. Eleven
writers set it to eight different values, four of them keyed on
`videoStandardInput`. It is not a don't-care that can be collapsed to a
constant: on a serrated source the count is wrong either side of one value, and
the right value is the one the firmware already derives from measurements.

## The measurement

Wii on `ypbpr`, PAL 576i, `/freeze?on=1` so the written value holds and the
divider does not chase the count, each row a sampling-log run of ~260 samples at
35 Hz from `loop()`:

| value | `STATUS_SYNC_PROC_VTOTAL` | serrations |
|---|---|---|
| 0x02 | 315, 316 | 0.0% |
| 0x06 | 315, 316 | 0.0% |
| 0x08 | 315, 316 | 0.0% |
| 0x0E | 314, 315 | 0.0% |
| 0x33 | 314, 315 | 0.0% |
| **0x6B** | **310** | 0.0% |
| 0x90 | 113, 180, 229, 245, 249, 253, 255, 261 | 19.5% |
| 0xFF | 97, 98, 348 | 0.0% |
| 0x6B again | **310** | 0.0% |

The source counts 310 fields. Only 0x6B reads it. Below that the count is four
to six lines high and perfectly steady, which no steadiness run can see; at 0x90
the separator is ignoring real pulses and the count is noise; at 0xFF it does
not lock at all and reads the 97 that means no lock.

**It repeats.** 0x6B gives 310 at both ends of the run. This is unlike the coast
lengths, whose surface is not reproducible between runs --
`two-owners-of-the-coast-lengths-double-the-count.md`. A value can be chosen for
this field on evidence; a coast pair cannot.

## The value is already derived, for one standard only

`updateSpDynamic()`'s `videoStandardInput <= 2` arm computes it from two
measurements -- `HPERIOD_IF` for the line, and `STATUS_SYNC_PROC_HLOW_LEN`
against `STATUS_SYNC_PROC_HTOTAL` for the sync duty -- and that computation is
what produced the 0x6B in force on this source. The other arms write fixed
bytes: 0x0E for standards 3-4, 0x08 for 5, 0x06 for 6-7. Those are the
standards with no source on this bench, and every one of them sits in the range
measured above as four to six lines wrong.

**So the per-standard dispatch is a derivation for one case and guesses for the
rest**, and the collapse it invites is to the derivation, keyed on the sync type
rather than on a standard: a source with its own vertical sync wants 0xFF, and
`applyForSyncType()` already writes that.

## Why 0xFF is right on one source and no-lock on another

0xFF is what the separate-sync arms write, and the RISC PC runs on it with a
steady 311. On the Wii it gives 97. The field follows the SYNC TYPE, not the
video standard: with a real vertical sync line there are no serrations to
discriminate and every pulse can be ignored, while a source carrying sync on
green needs the threshold placed on its actual pulse width.

## One value serves every composite source, and that is why a selection can write it

A SELECTION HAS NO MEASUREMENT OF THE ARRIVING SOURCE, so a threshold keyed on
serration reads the source being LEFT. Keying it that way cost the Wii its
acquisition whenever its predecessor ran above 15 kHz.

The way out is that the high-rate case does not need a value of its own.
Measured on the RISC PC at 800x600@60 with `SYNC 1` -- 37.9 kHz, composite,
unserrated -- automation frozen, the field written and read back, and a source
mode round trip so the separator re-locks with the value already in force:

| `SP_H_PULSE_IGNOR` | `STATUS_SYNC_PROC_HTOTAL` vs divider | `STATUS_SYNC_PROC_VTOTAL` x10 |
|---|---|---|
| 107, the serrated value | 1438 / 1438 | 623 x9, 664 x1 |
| 2, the narrow value (control) | 1438 / 1438 | 623 x6, 624 x4 |

**Indistinguishable.** 623 is the expected short count for this mode on
unserrated composite sync -- `the-risc-pc-composite-sync-is-not-serrated.md`
-- so the separator is locked and counting correctly on both. That doc measured
the same indifference at 15 kHz by the same method.

So the field follows the SYNC TYPE alone: `OwnVsyncPulseIgnore` 0xFF where the
source carries its own vertical sync, and one value for every composite source.
`UnserratedPulseIgnore` is deleted, and with it the only reason the arrangement
needed to know a serration it could not measure.

**And the search takes the same value rather than a third.** `applyForSearch()`
wrote 2 so every pulse reaches the separator while nothing is counting, which
read as harmless because the search is transient. It is not: the count that
ends the search is also what stops anything writing the field, so the search
value is what a source is then READ with. Measured on a `ypbpr` selection from
a 37.9 kHz predecessor, the field held 2 from the first count for thirteen
seconds -- the separator reading 271 lines against the source's 260 and
`STATUS_SYNC_PROC_HTOTAL` 3244 against a 2200 divider -- until the reprobe rung
re-applied the arrangement. With the search taking the sync type's value the
3244 state does not occur at all and `HTOTAL` stays within about 2% of the
divider throughout.

**What it buys is the right COUNT, which a time measurement hides.** Both
builds flashed in turn, the RISC PC held at 800x600@60, six `ypbpr` selections
each:

| | before | after |
|---|---|---|
| settled on 271/272 lines | 4 of 6 | **0 of 6** |
| time | 2.0 .. 46.5 s | 24.1 .. 25.4 s |

The old build's 2.0, 2.4 and 2.9 s legs each acquired on 271 or 272 against the
source's 260, so scoring a leg on time alone ranks the defect first. **It does
not on its own make the source acquire quickly**, which needs the ADC PLL to
lock and `../known-issues.md` carries open.

## What this does not settle

- **Whether a DERIVED threshold would beat the constant.** The value in force
  came from a computation over `HPERIOD_IF` and the sync duty, and nothing has
  compared the two on a source where they differ.
- **The 0x90 result is a boundary, not a limit.** `WidestUsefulPulseIgnore` is
  0x33, above which `widenCoastForSerration()` halves the value; 0x6B is above
  that and is the correct value on this source, so the constant does not
  describe where the field stops being useful.
- **The halving is a second value and is not audited.**
  `widenCoastForSerration()` leaves 53, which has been seen in force on a
  settled picture without harm and has never been compared against 107.
