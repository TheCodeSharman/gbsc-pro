# The hsync duty is the shorter interval, not the register

`STATUS_SYNC_PROC_HLOW_LEN` is the sync's LOW time in ADC samples — RD-5725-1.1
S0_19 calls it the "Input H-sync low active pulse length (for H-sync polarity
detection)". On a low-active source that is the pulse. On a high-active one it
is the line MINUS the pulse.

`SyncProcessor::hsyncPulseSamples()` takes the **shorter of the low time and its
complement**. A sync pulse cannot be more than half the line and still leave a
raster, so the shorter interval is the pulse, whatever polarity the source sends
and whatever the chip has been told about it.

## Why the inversion cannot be relied on instead

`normaliseHsyncPolarity()` writes `SP_HS_INV_REG` to invert a positive-going
sync ahead of the counter, which would make the low time the pulse on every
source. **It does not reach the counter.** Measured on the bench, RISC PC at
800x600 on `vga`, in pass-through:

```
SP_HS_INV_REG            1
STATUS_SYNC_PROC_HSPOL   1
STATUS_SYNC_PROC_HLOW_LEN  1411 of 1606      0.878, the complement
```

held for thirty seconds across four route toggles, with the console printing
`NOT A PULSE` on every pass. The write lands and reads back; the count does not
follow it.

An earlier reading of the same source appeared to settle — 1411/1606 at one
moment and 196/1606 about 1.5 s later, at a constant divider — which reads as
the block re-counting the line. It is not: a sync-type probe ran between the
two, and that is what changed which interval the counter reported. Nothing
makes the count follow the inversion on a schedule.

## What relying on it cost

The duty gates `SourceTiming::lookUp()`, which matches a source to a published
raster on line count, field rate and sync duty. No published raster has a duty
near unity, so the complement matches nothing.

In pass-through that decides where the blanking comes from. Measured across two
bypass landings at a fixed 800x600 source, same route, same `PLLAD_MD` of 2038:

| | landing A | landing B |
|---|---|---|
| `HD_HB_ST` | 2038 | 1961 |
| `HD_HB_SP` | 238 | 417 |
| border on screen | a wide band | a thin sliver |

417 is `216/1056 x 2038`, the mode's published active start scaled to the
measured line. 238 is the sync envelope, which is what the block falls back to
when no standard matched. Both landings were settled — a third photograph eight
seconds after the first still showed the wide band — and every register read
self-consistent at each.

**Refusing the complement without deriving the pulse is worse**, and was
measured: the engine waits, no solve completes, and the sink reports no signal
while the registers still look right.

## The scaling path hid it

`VideoSourceLine::forDuty()` used to substitute `FallbackDuty` of 0.07 for any
duty outside 0.041..0.152. On the bench source that guess is right to one unit —
7.03% against 7.00% — so the scaling path placed its capture window correctly
here and wrongly on every mode whose pulse is a different fraction of the line,
with nothing on screen to say so. Both scaled landings above therefore agreed
exactly: `VDS_HB_ST` 1695, `VDS_HB_SP` 344, `VDS_DIS_HB_SP` 426.

That is why the symptom looked confined to pass-through. It was not; pass-through
is where it was visible, because its fallback is a different framing source
rather than a constant.

The substitution is gone. `forDuty()` takes an `HsyncPulse`, which has already
been judged a pulse where it was taken, so there is nothing left to guess.

## The bound that remains

`HsyncPulse::isPulse()` is 41..152 per mille — what a sync pulse is across the
modes this board sees. With the pulse taken as the shorter interval it is no
longer reachable by a complement, so it now catches only a reading that is not a
pulse either way round. The duty line says `NOT A PULSE` when it fires, because
nothing is substituted and a waiting engine has to state its reason.
