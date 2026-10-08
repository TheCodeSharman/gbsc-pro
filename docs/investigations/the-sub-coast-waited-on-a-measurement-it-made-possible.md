# The sub-coast waited on a measurement that needed the sub-coast

A composite-sync source could not be counted until it had been measured, and it
could not be measured until it had been counted.

`SP_DIS_SUB_COAST` suppresses the sync processor's horizontal counting through
the serrated part of the vertical interval. It was written from
`SourceMeasurement::hasSerratedSync()`, which was
`lowLineRate() && SyncMeasurement::isCsync()` — and an unmeasured rate answers
false, by a deliberate choice recorded as *"a source that has never arrived is
configured for nothing"*.

So a SELECTION, which has no measurement of the source arriving, applied the
**outgoing** source's answer. Behind the RISC PC at 800x600@60 the arriving Wii
got `serrated` false, the sub-coast stayed disabled, the sync processor counted
the serrations as lines, and no rate could be measured through it.

    the configuration waited on a rate
    the rate waited on a steady count
    the count waited on the configuration

## The measurement

Wii in 480i selected on `ypbpr`, predecessor the RISC PC at 800x600@60 on `vga`:

| | failing leg | acquired leg |
|---|---|---|
| `VPERIOD_IF` | 524 | 524 |
| `STATUS_SYNC_PROC_VTOTAL` | 97..101 | 259 |
| `STATUS_SYNC_PROC_HTOTAL` | 1103..1240 | 2200 |
| `PLLAD_MD` | 1400, the reference divider | 2200 |
| `SP_DIS_SUB_COAST` | 1 | 1 |

`VPERIOD_IF` is a correct 524 throughout, so the source is arriving and the
input formatter is counting its vertical perfectly while the sync processor's
horizontal is nonsense. **And the console says nothing at all between 1.7 s and
the first recovery**, because a rate measurement needs a steady count first — so
a failing leg prints `DETECT: syncFound 2` and then eight seconds of silence.

Every recovery re-derived the same answer from the same unmeasurable rate, so no
act could break it. The legs that DID acquire got a few ragged counts (262..268
against 259/260) which were enough for a low rate, which flipped the predicate,
which a recovery then applied — 11.63 s to `sync pad: driven`, of which ten were
the wait for the recovery's budget to expire.

## The line rate has no job in it

Three cases, all measured:

| source | sub-coast | effect |
|---|---|---|
| csync at 15.7 kHz, serrated (Wii 480i) | required | without it the count is 97 against 260 |
| csync at 37.6 kHz, unserrated (RISC PC `SYNC 1`) | indifferent | `VTOTAL` 623, `HTOTAL` 1438 against a 1438 divider, lock held, acquired throughout 14 s |
| separate H and V (RISC PC `SYNC 0`) | harmful | `HTOTAL` 3116..3251 against a 2250 divider enabled, 2250 exactly disabled |

The middle row is the one that was missing, and it was taken by writing the one
field by name on a settled unit with everything else untouched. The third is
from [`serrated-sync-is-not-line-rate.md`](serrated-sync-is-not-line-rate.md).

So the **sync type** decides the sub-coast. `hasSerratedSync()` dissolves, and
the `serrated` parameter it fed out of `applyForSyncType()`,
`putSyncTypeInForce()` and `applySyncType()` with it.

Measured after: the horizontal locks at the reference divider — 1400 against
`PLLAD_MD` 1400 — where it read 1103..1240 before.

## And the dynamic configuration followed the measurement one way only

`takeBackSearchSettings()` withdrew the search configuration on the first count
and latched, so the withdrawal happened once. A source that then fell back into
searching kept the SETTLED configuration, and the coast inversion with it:
measured as `SP_COAST_INV_REG` 0 with `STATUS_SYNC_PROC_VTOTAL` at 97, which is
the exact state the inversion exists to avoid — uninverted from the start the
block counts nothing at all.

It follows both ways now, with an edge in front of one writer so a pass that
changes nothing writes nothing.

## Refuted: the separator level is not the fault

`ADC_SOGCTRL` was walked by name one step at a time from `ComponentLevel` 14
down to 1 on a frozen failing unit:

| level | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `VTOTAL` | 97 | 97 | 97 | 100 | 99 | 97 | 99 | 98 | 97 | 97 | 102 | 99 | 97 | 100 |
| `HTOTAL` | 1246 | 1108 | 1107 | 1247 | 1243 | 1109 | 1107 | 1246 | 1244 | 1243 | 1243 | 1240 | 1108 | 1240 |

Nothing moves. The level is not what decides whether this source is counted, and
the reading that suggested it — a hand-driven run where the count came right once
`ADC_SOGCTRL` had reached 1..2 — does not survive a controlled walk.

## What is left, and where it points

The residual failing leg has a DIFFERENT signature and it is not this one. A
full 1536-address `snapdiff.py` either side shows the whole sync arrangement
byte-identical — `SP_SOG_MODE` 1, `SP_EXT_SYNC_SEL` 1, `SP_DIS_SUB_COAST` 0,
coast 7/6, coast window 16/256, `SP_H_PULSE_IGNOR` 107, `SP_DLT_REG` 192,
`SP_H_TIMER_VAL` 58, `ADC_SOGCTRL` 14 — with the difference in the SAMPLING
group:

| | acquired | failing |
|---|---|---|
| `PLLAD_MD` | 2200 | **2098** |
| `PLLAD_KS` | 2 | 0 |
| `PLLAD_FS` | 1 | 0 |
| `DEC1_BYPS` / `DEC2_BYPS` | 0 / 0 | 1 / 1 |
| `ADC_CLK_ICLK1X` / `2X` | 1 / 1 | 0 / 0 |
| `STATUS_SYNC_PROC_VTOTAL` | 259 | 97 |

So a rate was measured, a divider of 2098 at oversample 1 was solved from it
against the 2200 at oversample 4 the source wants, and the count then collapsed.
That is the held-rate defect: a garbage reading taken while the source could not
be followed is accepted, a divider is installed from it, and
`rateFollowsCount()` rejects every correct reading afterwards.
`../known-issues.md`, "Sync on green does not follow the source".
