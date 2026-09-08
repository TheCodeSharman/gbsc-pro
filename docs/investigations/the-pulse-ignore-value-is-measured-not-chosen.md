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

## What this does not settle

- **Whether the derivation holds outside 15 kHz.** It was measured on one line
  rate. The formula scales with the measured line and duty, so it should follow,
  but standards 3 to 7 have no source here to say.
- **The 0x90 result is a boundary, not a limit.** `WidestUsefulPulseIgnore` is
  0x33, above which `widenCoastForSerration()` halves the value; 0x6B is above
  that and is the correct value on this source, so the constant does not
  describe where the field stops being useful.
