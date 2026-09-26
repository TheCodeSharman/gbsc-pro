# `VPERIOD_IF` needs composite sync in the SIGNAL, not a path register

The input formatter's vertical measurement is valid when there is composite sync
at the input to separate, and debris when there is not. Which sync path the
registers select does not decide it, and neither does line doubling.

Measured on `vga`, RiscPC 320x256@50, one cable, one mode, firmware frozen so
nothing rewrote the chip underneath. The true half-line count for the mode is
**623** -- VTOTAL 312 from the AKF50 `v_timings`, counted in half lines.

| path registers | source sync | `VT_BAD` | `VPERIOD_IF` | `SP_VTOTAL` |
|---|---|---|---|---|
| `SOG_MODE` 0, `EXT_SYNC_SEL` 0 | separate | 1 | 66..118, noise | 311 |
| `SOG_MODE` 1, `EXT_SYNC_SEL` 1 | separate | 1 | 72..114, noise | 97 |
| `SOG_MODE` 1, `EXT_SYNC_SEL` 1 | composite | 1 | 1049, steady | 97 |
| `SOG_MODE` 0, `EXT_SYNC_SEL` 0 | composite | **0** | **524**, steady | 526..1975 |
| the firmware's own csync setup | composite | **0** | **623**, steady | 308 |

**Rows one and four are the controlled pair.** Identical path registers, only the
source's sync type moved, and the measurement switches from debris to a steady
value with the flag following it. So the necessary condition is composite sync
present in the signal. That is what the separator extracts vertical timing from,
and a source sending its own VSync on a dedicated pin leaves it nothing.

## `VT_BAD` 0 does not mean the value is right

Two rows read GOOD and only one of them carries the correct count. The forced
separate-path row reports **524 steady against a true 623** with `VT_BAD` 0 --
wrong, stable, and flagged healthy, which is the same shape as the `HPERIOD_IF`
railing. Only the firmware's own composite setup gives 623.

So the flag is worth reading as *"there is a measurement"* and never as *"the
measurement is right"*. Check the value against what the mode implies, as
`hperiod-if-railing.md` already requires for the horizontal.

**The three forced rows are not states the firmware produces.**
`SyncProcessor::applyForSyncType()` writes the coast pair, the clamp and the
pulse-ignore settings as well, and frozen they kept whatever the previous
configuration left. What those rows establish is narrower than a sync-type
comparison: that the two path registers ALONE do not decide validity.

## Line doubling is not the cause

Proposed by an earlier session: that the measurement fails on separate sync
because the line doubler is engaged. Refuted. With the source on separate sync
and the doubler taken out -- `IF_LD_RAM_BYPS` 0 to 1 and `IF_PRGRSV_CNTRL` 0 to
1, both read back -- `VT_BAD` stays 1 and `VPERIOD_IF` stays debris across six
samples, indistinguishable from the six taken with the doubler in.

## The whole of Mode Detect's classification goes with it

`VPERIOD_IF` is not the only casualty, because the classification is derived
from the same vertical measurement. Measured on the firmware's own configuration
either side of a `SYNC` change:

| | `VT_OK` | `HVT_OK` | `NTSC_INT` | `NTSC_PRG` | `PAL_INT` | `PAL_PRG` |
|---|---|---|---|---|---|---|
| separate | 0 | 0 | 0 | 0 | 0 | 0 |
| composite | 1 | 1 | 0 | 0 | **1** | 0 |

On separate sync **all four standard bits are clear**: Mode Detect names nothing,
which is what `PresetLoad::Rgbhv` means rather than a classification in its own
right, and what `ModeDetect::sourceIsInterlaced()`'s header already states --
all four clear is "neither answer is available", not "progressive".

And the composite answer is wrong in its own way. The source is a 320x256@50
PROGRESSIVE RGB mode and the bit that comes up is `INP_PAL_INT`. The bits are
vertical-period buckets, so a 15 kHz progressive source lands in an interlaced
standard's bucket. Neither leg gives a usable classification: one gives none and
the other gives a wrong one.

This is the measurement behind retiring the byte. `docs/video-source-acquisition.md`.

## The fault the witness catches cannot occur where the witness is missing

**The check described here is deleted.** The fault it caught is not reachable once one owner writes the coast pair, measured over 3239 samples.
[two-owners-of-the-coast-lengths-double-the-count.md](two-owners-of-the-coast-lengths-double-the-count.md)

`countIsSerrations()` needs `VPERIOD_IF` to tell a count of the source's lines
from a count of the serration and equalisation pulses either side of the
vertical interval. Those pulses are a composite-sync construct: a separate-sync
source sends a plain VSync on its own pin and runs HSync straight through the
vertical interval, so there is nothing for the sync processor to run through and
miscount.

So the witness is available in exactly the case the fault is possible in.
`countIsSerrations()` returning false on separate sync -- documented as "not a
judgement that the count is good" -- is the correct answer there rather than a
safe default, for this input class.

The scan type does not close the same way. Interlace over separate sync exists
in principle, and there the alternation would be all there is. This bench has no
such source.

## What it costs, which is less than it looks

Nothing reads `VPERIOD_IF` as a rate. RD-5725-1.1 gives it as "input source V
total lines", and the rate arithmetic runs the other way in
`SourceMeasurement::lineRateFrom()`. Its three consumers want what a line count
cannot supply:

- the scan type, from the half line -- a Wii reads 524 at 480i and at 480p, so
  parity in half lines is the only discriminator
- `countIsSerrations()`, which needs a vertical count by a DIFFERENT route from
  the sync processor's so that the two can disagree
- the deinterlacer's field parity, keyed on exact equality

All three already have a separate-sync answer. `SourceMeasurement::scanType()`
falls back to the steadiness run's alternation, and `countIsSerrations()` returns
false when the witness is not measuring -- documented as "not a judgement that
the count is good". So a separate-sync source is not degraded by this; it is
running the path the firmware was written for.

**The untested case is a separate-sync INTERLACED source**, where the scan type
would rest on the alternation alone. This bench has none: the Wii is sync on
green. `docs/bench-sources.md`.
