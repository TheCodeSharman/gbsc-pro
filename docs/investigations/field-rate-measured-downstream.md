# The field rate is measured downstream of the divider it chooses

`SourceMeasurement::measureLineRate()` multiplies the source line count by
`getSourceFieldRate()`, and the divider follows from the product. The line count
comes from `STATUS_SYNC_PROC_VTOTAL`. The field rate does not come from a
register at all: it is a pulse the ESP times at `DEBUG_IN_PIN`, routed there
from one of the TV5725's internal test buses.

**The bus it is routed from is the input formatter's, whose line counter the
divider sets.** So the measurement that chooses `PLLAD_MD` is taken from a block
`PLLAD_MD` configures.

## What that costs, measured

Forcing a 1080p output raster on the bench source (320x256@50, VTOTAL 311,
50.08 Hz) puts the unit in a state it does not leave:

```
              PLLAD_MD  IF_HSYNC_RST  SP_VTOTAL  SP_HTOTAL  HPERIOD_IF
before            2250          1125        311       2250         431
after, +120 s     1124          1124    173..191    ~2249         431
```

`SP_HTOTAL` holding ~2249 against a `PLLAD_MD` of 1124 is twice the divider, the
signature of a PLL locking to every second hsync. `HPERIOD_IF` never moves, so
the line arriving never changed.

**The reason once given here for that -- that 1124 asks for ~17.5 MHz, below the
PLL's lock range -- is wrong.** 17.5 MHz is CKO, and the PLL does not lock at
CKO: it locks at `CKO x 2^PLLAD_KS`, and `Adc::applySampleRate()` selects `KS` 3
for that rate, putting the VCO at 140 MHz. Measured directly, `PLLAD_MD` 1124
with the `KS` that goes with it locks in 98% of samples.
[`adc-pll-lock-range.md`](adc-pll-lock-range.md).

So what leaves this state at every-second-hsync is **not settled**. A divider
written without the `KS` that belongs to it would do it -- 1124 at `KS` 2 is a
70 MHz VCO, far under the edge -- and so would a latch that never fired. Note
also that the capture has `IF_HSYNC_RST` equal to `PLLAD_MD` rather than half of
it, which no healthy state has.

The console names each step. Over the 2.8 s after the raster change the field
rate reads 109.75, 110.53, 91.56 and 110.53 Hz against a real 50.08:

```
sampling: 311 lines x 110.53 Hz -> line rate 0      x60
sampling: 311 lines x 110.53 Hz -> line rate 34376
```

`rateFollowsCount()` refuses the first sixty. `HeldRateRejectionLimit` then
takes the sixty-first as it stands, still 110.53 Hz, because the hatch requires
nothing of the reading it admits.

1124 itself is not that rate. The source is reclassified progressive across the
raster change -- `IF_LD_SEL_PROV` and `IF_PRGRSV_CNTRL` both read 1, and only
`InputFormatter::applyScanMode()` writes the first, so `scanModeChanged(false)`
ran -- and with line doubling off `recommendedDivider()` clamps to
`InputLine::WriteLimitUnits & ~1`, which is 1124 exactly.

`recoverDivider()` identifies the 2:1 multiple correctly and still cannot
escape: it re-derives through `recommendedDivider()`, which re-applies the same
clamp under the same `lineDoubled_`, so `chosen == divider_` and it refuses.
After `RecoveryAttempts` it stops trying, and nothing resets `recoveries_` but a
successful `measureLineRate()` -- which cannot happen while the field rate is
measured through a mis-programmed input formatter.

`/sc?~` is the only cure. The sync path is untouched throughout
(`SP_SOG_MODE` 0, `SP_EXT_SYNC_SEL` 0, `VSACT` 1), so this is not the
self-latching sync-type decision of `sync-type-selection.md`. It is a second
loop of the same shape.

## The obvious fix is refuted

The engine's stated rule is that the only registers it may read are the
`STATUS_SYNC_PROC_*` ones, so routing the field rate off the **sync processor's**
test bus instead reads as both principled and sufficient. It is neither.

`getSourceFieldRate(useSPBus)` already offers that, and it does nothing on a
separate-sync source: with `rto->syncTypeCsync` false it writes `TEST_BUS_SEL`
0, the input formatter's bus, exactly as the `useSPBus == 0` path does. Only the
inert `TEST_BUS_SP_SEL` write differs. Passing 1 measured identically -- 110.53
and 91.56 Hz, same trap, same 1124.

Forcing `TEST_BUS_SEL` to 0xa unconditionally is worse and settles the question:

```
sampling: 311 lines x 0.00 Hz -> line rate 0
```

The sync processor's bus carries no vsync pulse for a separate-sync source at
all. The engine then has no rate, falls back to adopting whatever is on the
chip, and lands on `PLLAD_MD` 1856 -- the literal `bypassModeSwitch_RGBHV()`
writes.

So the input formatter's bus is not a careless choice. It is the only one that
carries the pulse on this source, and the circularity is a property of the
board, not of the call site. What can be broken is the other end: which divider
the IF is running when the pulse is timed.

## The same circularity across a source mode change

The trap is not confined to a forced output raster. Driving the bench source
from 320x256@50 to 640x480@60 over ModeServ reaches it, and the console names
the failure outright:

```
sampling: 524 lines x 184.51 Hz -> line rate 0      repeated indefinitely
sampling: 312 lines x  91.61 Hz -> line rate 28583  accepted, mid-transition
sampling: 311 lines x  50.08 Hz -> line rate 15575  correct, solves at once
```

171.53 and 184.60 Hz against a real 60 is roughly three times the field rate,
and every reading is refused, so no divider is chosen for the new mode and the
old one stands. The line count is a perfectly good 524 throughout, which is what
puts the state out of `recoverDivider()`'s reach: it gates on a count outside
what any source runs, the steadiness gate passes, and the rate is the half that
fails.

**It is intermittent.** The same leg on the same build solved correctly on one
trial and refused on the next, and the mid-transition reading above shows why a
refusal is not the only bad outcome: 312 lines at 91.61 Hz passes the
cross-check and sizes a raster 15% out.

The recovery is the source moving again. A count the input formatter can follow
restores the rate at once, which is consistent with the bus the rate is timed
from being the one the divider configures.

## What breaks the loop: measure from a reference divider

`Geometry::holdReferenceSampling()` puts the sampling chain into a state the
pass chooses before anything is timed through it, so the reading cannot depend
on the mode before. The reference is the divider the capture write limit allows
-- `2 x WriteLimitUnits` line-doubled, `WriteLimitUnits` progressive -- which
lands `IF_HSYNC_RST` on `WriteLimitUnits` for every source and every scan mode,
at roughly constant CKO because the divider tracks the line length. The scan
mode it is chosen for comes from the line count, which the divider does not
touch.

**The oversampling is not part of the reference.** `PLLAD_CKOS` and the four
decimator bits describe one ratio between them, and the IF's units come off the
decimated clock. Writing the reference at ratio 1 while the mode wanted 4 left
the IF not running at all: the rate read a flat `0.00 Hz` for 117 consecutive
samples, with `STATUS_SYNC_PROC_HTOTAL` locked to the reference throughout.
Only the divider moves.

Measured across the mode change that used to trap:

```
before   sampling: 524 lines x 184.51 Hz -> line rate 0       x30, indefinitely
after    sampling: 311 lines x  50.08 Hz -> line rate 15575   x3, all accepted

to 524   PLLAD_MD 1124  IF_HSYNC_RST 1124  HTOTAL 1124  HPERIOD_IF 213
to 311   PLLAD_MD 2250  IF_HSYNC_RST 1125  HTOTAL 2250  HPERIOD_IF 431
```

`STATUS_SYNC_PROC_HTOTAL` equals the divider at both ends, which is the witness
that it latched.

Measuring is now a **disturbing** act -- it moves the sampling clock -- so it
must not run on a schedule. Today it is confined to a mode change, which the
engine notices by the line count moving. A source that changes to the same line
count at a different field rate is not noticed that way, and the chip's own
mode-change interrupt (`s0_0F` bits 0 and 1, which fire at ~0.9-1.1 s in both
directions) is what would catch it.
