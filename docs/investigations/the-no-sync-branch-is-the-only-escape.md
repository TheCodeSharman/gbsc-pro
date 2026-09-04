# The no-sync branch is the only escape, so it cannot be gated first

`runSyncWatcher()`'s no-sync branch fires on `getVideoMode() == 0`, and on an
RGBHV source that is not a classification: the branch hands the held standard
back or 0, so a source the sync processor is counting reads as no source at all
once the byte has been cleared, and the recovery walks the ADC and the sync
processor off it.
`docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md` is that
fault.

`Geometry::sourceIsPresent()` is the measurement that replaces the
classification there. Wiring it into that gate is measured to fix the fault and
to leave the unit with no way out of two other states, so **the gate is not the
first step of the migration** -- the SOG level has to have an owner before it
moves.

## What the gate buys, measured

A sync-type round trip on the bench RiscPC -- `SYNC 1`, then `SYNC 0` -- with
the gate reading `!geometry.sourceIsPresent()` instead of
`detectedVideoMode == 0`:

| | return leg, 5 s after `SYNC 0` | for the next 50 s |
|---|---|---|
| classifier gate | `VTOTAL` 97/99/47, `HSACT` 0, `ADC_SOGCTRL` 4/2/10, `SP_H_PULSE_IGNOR` 2, `SP_DLT_REG` 112, `PLLAD_MD` flapping 1124/2250, one `/getreg` timing out | recovers unaided after about a minute |
| engine gate | `VTOTAL` 311, `HSACT` 1, `VSACT` 1, `SP_H_PULSE_IGNOR` 255, `SP_DLT_REG` 0, `SP_H_CST_SP` 1661 | held, capture enabled |

So the source is re-acquired at once rather than being hunted for.

## And the absent source still recovers, which is the case it must not break

`/input?src=rgbs` has nothing attached, so it is the "the signal really has
gone" case, and it is reachable without a cable change. With the gate wired, the
sketch's recovery runs exactly as it did: counts 97, 98, 99, 103, 130, 168 --
every one outside the source bounds -- `HSACT` 0, `ADC_SOGCTRL` walking
12/5/11/1, `SP_DLT_REG` cycling 112/160/192/48. Returning to `vga` restores
`VTOTAL` 311, `ADC_SOGCTRL` 12, `SP_H_PULSE_IGNOR` 255, `SP_DLT_REG` 0 and
`SP_H_CST_SP` 1661 within 6 s, held for 45 s.

A range check alone would not have got that far: `SourceMeasurement::countIsSource()`
passes garbage from an unlocked sync processor. The steadiness run is what
separates the two, and the absent input above never holds a plausible count.

## What it costs: two states with nothing left to clear them

### A black state with a mode change it cannot finish

On a second round trip, `ADC_SOGCTRL` went 12 to 5 in one step about 5 s after
the return and stayed there. Thirty seconds later the ADC PLL began falling out
of lock -- `STATUS_SYNC_PROC_HTOTAL` reading 3938 and 699 against `PLLAD_MD`
2250 with `STATUS_MISC_PLLAD_LOCK` 0 -- and `CAPTURE_ENABLE` read 0 across six
consecutive samples with the sink still locked to 1920x1080/50 and the screen
black. The engine has a mode change armed that it cannot finish, so capture
stays frozen. **The no-sync branch is what used to clear it**, and with the gate
wired nothing does. `/input?src=vga` clears it.

**THE SLICER IS NOT THE MECHANISM, AND IT WAS THE FIRST THING BLAMED.** The
slicer only reaches the sync processor when `SP_SOG_MODE` is 1, which follows
the sync type; the bench source is separate sync on the VGA input, where the
schematic routes the dedicated HSync pin, and it reads `SP_SOG_MODE` 0,
`SP_EXT_SYNC_SEL` 0. So `ADC_SOGCTRL` 5 is not in the sync path and cannot
starve it.

Nor was it the pre-emptive tuning that moved it, which returns immediately
unless the sync type is csync. What is left is the recovery's own two
adjustments, and **they are the ungated ones**: `fastSogAdjust()` gates only on
`noSyncCounter <= 5`, and the every-150 block sets the level to 0 or 5 and calls
`optimizeSogLevel()` with no test of whether the slicer is in use at all.

So the walk is evidence that the recovery ran, not the reason the picture went.
**What caused the black state is not established** -- `SP_SOG_MODE` was not
sampled during it. What is established is that removing the escape leaves
nothing to clear it.

### The recovery adjusts a slicer that is not in the path

That is a defect on its own terms, independent of the gate. Two of the four
routines that move the level ask whether sync on green is the sync source and
the other two do not, so a separate-sync source has its slicer walked during
every recovery for no effect that anything can name.

### The classifier's own latch at 0

`getVideoMode()`'s RGBHV branch returns `rto->videoStandardInput`, so once the
byte is 0 it stays 0 whatever the sync processor is counting, and the mode
change branch below it needs a non-zero reading to fire. The no-sync branch is
the only path that re-runs detection and puts a standard back.

Both escapes are accidents of a recovery that runs too eagerly. Removing the
eagerness without giving the two states an owner leaves them permanent.

## A scrambled picture that no config register distinguishes

After the first round trip the picture was scrambled -- every line the wrong
length -- while `/geometry` was self-consistent, `HPERIOD_IF` read 431 for the
mode, `VTOTAL` 311, and a 1536-register dump differed from a clean dump of the
same build in ten fields, none of them a window, a scale or a scan mode.

The witness is the one register that counts real ADC clocks:

| | scrambled | clean |
|---|---|---|
| `STATUS_SYNC_PROC_HTOTAL` | 2381 | 2250 |
| `PLLAD_MD` | 2250 | 2250 |
| `STATUS_MISC_PLLAD_LOCK` | 0 | 1 |

So the ADC PLL was not running at the ratio the divider asked for, and nothing
re-latches it: the engine writes the divider and latches once per solve. The
config space says the machine is correct throughout.

`PLLAD_FS` is 0 in that state and 1 in the clean one, and it is **not** the
cause on its own -- writing `PLLAD_FS` 0 and latching, from a clean locked
state, leaves the picture clean. `updateCoastPosition()` is what writes it,
alongside `PLLAD_ICP`, for a csync source under 322 lines.

## What has to land first

Not the gate. The escape it removes is real whatever the black state's cause
turns out to be, so what has to come first is the acquisition the recovery is
doing badly: one owner for the slicer level, and every adjustment of it asking
whether sync on green is the sync source. Until then the sketch's recovery is
the only thing that leaves the state, and a gate in front of it is a gate in
front of the only exit.
