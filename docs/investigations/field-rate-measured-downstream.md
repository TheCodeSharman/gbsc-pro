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
board, not of the call site. A fix has to break one of the other links: the
escape hatch admitting a reading nothing vouches for, `recoverDivider()` being
unable to produce a divider different from the one that trapped it, or the
reclassification to progressive that makes 1124 the clamp in the first place.
