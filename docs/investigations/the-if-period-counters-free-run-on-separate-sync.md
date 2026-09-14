# The IF period counters free-run on separate sync

`HPERIOD_IF` and `VPERIOD_IF` do not measure anything on the bench source's
separate-sync leg. They are not railed and not stuck on a wrong value: they
return a different number on every read. On the same source and mode with
`SYNC 1`, `HPERIOD_IF` reads the 431 the mode is due.

## What the two legs read

`MODE X320 Y256 C256 F50` on `vga`, acquired, `PLLAD_MD` 2206 against
`STATUS_SYNC_PROC_HTOTAL` 2206, `STATUS_SYNC_PROC_VTOTAL` 311, PLL locked. Due
values are `HPERIOD_IF` 431 (`27e6 / (4 x 15625) - 1`) and `VPERIOD_IF` 311, the
line count -- which is what the Wii at 480p reads, 214 and 524.

| leg | `HPERIOD_IF` | `VPERIOD_IF` | `STATUS_IF_HT_OK` | `STATUS_IF_VT_OK` |
|---|---|---|---|---|
| `SYNC 1`, csync | 431 | -- | -- | -- |
| `SYNC 0`, separate | 92, 10, 511, 511 in four reads | 35, 125, 28, 47 | 0 | 0 |

Eight control repeats across two sweeps all read like the second row: every
sample different, spanning the whole range including both 0 and 511.

**Both OK flags read 0 throughout.** On this path the chip correctly reports that
it has no valid measurement, which `docs/tv5725-chip.md` records as not being
true of `STATUS_IF_HT_OK` -- that claim was taken against the railing fault and
does not hold here.

## Only one bit changes anything, and it freezes rather than fixes

Swept with every write read back and the shipped separate-sync state repeated as
a control between every treatment: `SP_H_PULSE_IGNOR` at 255, 2 and 0; coast set
to the csync values; `SP_HS_POL_ATO`/`SP_VS_POL_ATO` cleared; `SP_VSIN_INV_REG`
set; `SP_SYNC_BYPS` set. Every one reads as noise, indistinguishable from the
control.

`SP_H_PROTECT` -- "H count overflow protect" -- is the exception. With it set,
both counters go perfectly steady:

| run | state | `HPERIOD_IF` | `VPERIOD_IF` |
|---|---|---|---|
| 1 | protect | 452 x6 | 100 x6 |
| 1 | protect + pulse ignore 2 | 226 x6 | 23 x6 |
| 2 | protect | 155 x6 | 64 x6 |
| 2 | protect + coast, + polarity, + vsin invert, + sync bypass | 155 x6 each | 64 x6 each |

**The steady value is not reproducible between runs and is never correct.** In
run 2 every variant landed on the same 155/64 including ones that differ from
each other in the sync path, which is what a frozen counter looks like rather
than a measuring one. So the bit latches whatever the counter happens to hold at
the moment it is set; it does not make the block measure.

No configuration tried produces 431 or 311 on this leg.

## What this means for reading the register

The engine is right to prefer the count-and-rate path here. `/geometry` holds
`lineRateHz` 15625 and the raster the mode is due throughout, so a free-running
`HPERIOD_IF` costs nothing as long as nothing reads it.

`SyncProcessor::acquireClampWindow()` does read it, on the csync branch only,
where it happens to be the leg that works. The clamp window on the separate leg
is computed from `STATUS_SYNC_PROC_HTOTAL` instead.
[the-csync-left-band-is-output-side.md](the-csync-left-band-is-output-side.md)

**Sparse reads are enough to see this.** Four HTTP samples spanning 0 to 511
settle it, and the shape -- a different value every read -- is not what
`hperiod-if-railing.md` describes. Whether the two are the same fault seen on
different legs is open; nothing here reproduces a stable wrong value.
