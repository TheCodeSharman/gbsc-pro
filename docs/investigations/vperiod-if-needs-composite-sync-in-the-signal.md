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
