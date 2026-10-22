# The phase scan measures one band and infers the rest

`Adc::acquirePhase()` walks the sync processor's phase adjuster across its
32 steps and scores each phase on how far `STATUS_SYNC_PROC_HTOTAL` departs
from `PLLAD_MD` over 20 samples. The score locates a band of phases where the
line is miscounted. **What it does not establish is that any other phase is
good**, and the rule that took the middle of the widest clean run read it as
establishing both.

## The band the metric finds depends on the sync type, not on the mode

Measured on `vga` at `MODE X320 Y256 C256 F50`, one source, one cable, the sync
type set by `SYNC`:

| arrangement | band the scan finds | scores, of 20 | the widest clean run's middle |
|---|---|---|---|
| composite, `SYNC 1` | **26..29** | 1..8 | **8, 11, 12 or 13** |
| separate, `SYNC 0` | **11..19** | 1, 1, 2, 11, 10, 11, 7, 6, 1 | **31** |

The same band appears at a second divider: at `MODE X640 Y480 C256 F60`,
oversample 2, composite scans give 26..29 again.

**The composite band is real and reproducible.** Walked by hand at 100 reads a
phase across the whole field, `PLLAD_MD` 2200:

```
phase  0 .. 26   0 bad of 100, every phase
phase 27        17 bad of 100
phase 28        22 bad of 100
phase 29         4 bad of 100
phase 30, 31     0 bad of 100
```

## One arrangement's clean run contains the other's band

The region the composite scan reports clean contains the band the separate-sync
scan reports bad, and **that region is where the composite rule sends the
phase.** Of six consecutive acquisitions of the same source and mode on
composite, the rule chose 13, 8, 11, 16, 16, 16 -- the first three inside or on
the shoulder of 11..19.

A fault is measured there on this source, by the picture rather than by the
scan's own metric:

| phase | 0 | 4 | 8 | 10 | 12 | 14 | 16 | 20 | 24 | 28 |
|---|---|---|---|---|---|---|---|---|---|---|
| shimmer, grey levels of frame-to-frame change | 0.52 | 0.56 | 0.73 | 1.15 | **2.13** | 1.45 | 0.86 | 0.66 | 0.67 | 0.66 |
| count dither, of 12 reads | 0 | 0 | 0 | 3 | **5** | 4 | 1 | 0 | 0 | 0 |

So on composite the rule walks into a region measured bad by the picture, by
inference from the only region its own metric can measure.

## The phase does move the pulse measurement, and it moves it the wrong way

`STATUS_SYNC_PROC_HLOW_LEN` read at about 2300 readings a second, A-B-A-B over
the two phases, 120 s a leg on composite:

| leg | readings | outside the band | rate |
|---|---|---|---|
| 16 | 253456 | 110 | 434 per million |
| 13 | 251768 | 89 | 353 per million |
| 16 | 247952 | 111 | 448 per million |
| 13 | 248640 | 98 | 394 per million |

The rate alternates with the register rather than trending, so the phase does
reach the separator's pulse measurement. **16 scores consistently worse**, so a
sweep scored on this marker would choose 13 and not 16.

## What the rule is now

The scan's answer is used only where the scan scores against the default. A
band clear of `Adc::MidField` leaves the phase there; a band covering it hands
the choice to `middleOfWidestCleanRun()`, which is the shimmer case the metric
was given for. On composite the engine then chooses 16 on every acquisition --
measured across three source mode round trips, each scan finding 26..28 and
each landing on 16 -- where the same six acquisitions previously gave four
different answers.

**The gain is determinism, not a known-good phase.** Which phase is best on
composite is unmeasured; what is removed is a per-acquisition lottery over four
values, one of which the picture measures bad.

## THE COMPOSITE SHEAR IS NOT THIS REGISTER

Carried for a session as the shear's cause, and refuted on its own instrument:
`shearwatch.py`, 900 s a leg, A-B-A moving `PA_SP_S` alone with the source
untouched, `ADC_SOGCTRL` 12 and `SP_HS_POL_ATO`/`SP_VS_POL_ATO` 1 throughout.

| leg | windows holding 5+ out-of-band readings | frames displaced | windows off count 308 |
|---|---|---|---|
| **16** | 6 | 5 | 2009 |
| **13** | 5 | 3 | 1554 |
| **16** | 4 | 3 | 1614 |

Phase 13 is indistinguishable from both 16 legs. The reading it replaces rested
on legs scoring 1, 0 and 1 bursts, which cannot separate a register from
Poisson noise at that rate.

**THE RATE DECLINES WITH UPTIME, AND THAT GRADIENT IS LARGER THAN ANY PHASE
EFFECT MEASURED.** Four runs on one unit, one register state, phase 16 except
where noted:

| run | minutes after boot | bursts per 900 s |
|---|---|---|
| 2700 s soak | ~3 | **32.3** |
| 900 s leg | ~50 | 6 |
| 900 s leg, phase 13 | ~65 | 5 |
| 900 s leg | ~80 | 4 |

An eightfold decline with nothing written. **So a leg's absolute count says
nothing without its uptime**, and two legs an hour apart compare uptimes rather
than configurations -- which is how a register came to be named. An A-B-A
controls a monotonic trend and does not rescue counts this small.

## Open

- **Why the band moves with the sync type.** The separator is in circuit on one
  and not the other, and nothing measured says more than that.
- **What the uptime gradient is.** Thermal and post-boot settling are both
  unexcluded, and nothing has been measured from a cold boot at fixed
  intervals.
- **Whether 26..29 is bad as well as detectable.** It has never been run for
  shears; it is only where the metric speaks.

## A phase inside the clean run dithers at 4.8%, and 20 samples cannot see it

The inference this page opens by naming -- that the scan establishes only where
the band is, and nothing about the rest of the field -- now has a figure against
it on separate sync.

`vga`, separate sync, 320x256@50 at 50.474 Hz, `PLLAD_MD` 2200,
`STATUS_SYNC_PROC_VTOTAL` 311 with **0 transitions**, read with
`/samplinglog?ms=25&for=30000`:

| field | readings | distinct | transitions | values |
|---|---|---|---|---|
| `STATUS_SYNC_PROC_HTOTAL` | 1135 | 3 | 100 | 2200 x1080, **2199 x34, 2201 x21** |
| `PLLAD_MD` | 1135 | 1 | 0 | 2200 |
| `STATUS_SYNC_PROC_HLOW_LEN` | 1135 | 2 | 70 | 165 x1100, 166 x35 |

So the engine's own choice -- `PA_SP_S` 29, reached because the band covered
`MidField` and `middleOfWidestCleanRun()` answered -- departs from the divider
in **55 of 1135 samples, 4.8%**, where the separate-sync band this page records
is 11..19 and 29 sits in the clean run. The picture shimmered visibly on sprite
detail in that state.

**The dither is real rather than the two-byte tearing artefact.** 2199, 2200 and
2201 are `0x897`, `0x898` and `0x899` -- one high nibble and three adjacent low
bytes -- and a field whose high byte never moves cannot tear into a different
value. `a-wide-status-field-tears-across-its-two-bytes.md`.

**The scan cannot resolve a dither of that size, and the arithmetic bounds it.**
`SamplesPerPhase` is 20 and a phase scores clean only when every sample has
`off == 0`, so a phase dithering at 4.8% reads clean with probability
`0.952^20 = 0.37`. More than a third of the time such a phase joins the clean
run, which is what the run's middle is then chosen from.

**The reading is late in a boot, which is the direction that strengthens it.**
The uptime gradient recorded above is eightfold and downward, so a figure taken
more than twenty minutes after the last restart sits near the bottom of that
range rather than in its steep part.

**AND IT IS WHY A PHASE SWEEP MUST BE INTERLEAVED.** A monotonic trend of that
size walked from phase 0 to 31 in order reads as a phase profile and is one
reading per phase of the uptime instead. Candidate phases go A-B-A with the
uptime logged, which is the shape `shearwatch.py` already uses.
