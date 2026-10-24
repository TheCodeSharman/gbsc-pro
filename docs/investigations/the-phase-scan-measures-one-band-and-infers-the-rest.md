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

## Whether the phase reaches the picture at all is the sync type's, not the mode's

The sections above compare the two arrangements at one mode on the scan's own
metric. **The picture answers the same way, and more strongly: on separate sync
the sync phase moves the emitted frame and on composite it barely moves it.**

`vga` at `MODE X800 Y600 C256 F60`, scaling into 1080p, `PLLAD_MD` 1436 against
`STATUS_SYNC_PROC_HTOTAL` 1437 on both legs, one cable, the arrangement set by
`SYNC`. Each leg is one frozen acquisition with the console certified quiet,
`PA_ADC_S` held at the engine's own value, `PA_SP_S` walked over its whole field
at step 4. The band scored is fixed in OUTPUT columns rather than located per
frame, because the located wedge box differs between the arrangements and a box
that moves with the thing it measures would read flat for that reason alone.
Each cell is the spatial spread of a 36-column slice of the card's frequency
wedge, over rows 582..689, and the row is how far that slice moved across the
whole field:

| slice, output column | 350 | 390 | 430 | 470 | 510 | 550 | 590 | 630 | 670 |
|---|---|---|---|---|---|---|---|---|---|
| **separate** | 4.0 | **12.3** | **12.3** | **13.3** | **15.3** | **11.7** | 9.3 | 10.3 | 9.8 |
| **composite** | 2.2 | 4.1 | 4.0 | 2.9 | 5.7 | 3.5 | **0.6** | 1.7 | 3.3 |

Three to six times more at every slice but the leftmost, on bases of 20 to 82
grey levels. On separate the loss is a BAND rather than a slope -- every slice
collapses together at `PA_SP_S` 8..12, worst at 12, and the coarse slices at
630 and 670 fall from 81 to 71 with the fine ones, so it is not confined to
content near Nyquist. That band sits where the scan's separate-sync band sits
at the other mode, 11..19.

The same pair gridded against `PA_ADC_S` says it again as a ceiling: the
reachable peak spans **1.27x** over `PA_SP_S` on separate, 31.2 to 39.7, and
**1.04x** on composite, 30.6 to 31.7, which is the instrument's own floor.

**So the question the two arrangements separate is not which band is bad but
whether any phase is better than another**, and the mode is held across it.

**THE COMPOSITE LEG IS NOT THE STATE THE FIRMWARE SHIPS FOR THAT SOURCE.** At
its own coast of 0/0 the picture rolls in 95% of frames and no reading of it
repeats -- one phase pair read 13.2, 33.6, 23.3 and 42.7 inside one frozen
acquisition. The leg above was taken with `SP_PRE_COAST` 7 held, where the roll
is 0 of 3602 frames. `docs/known-issues.md`.

**And the temporal measure does not support the obvious mechanism.** Sampling
jitter smearing the phase would make composite the noisier leg, and it is the
quieter one: per-pixel temporal sd over a flat patch of card reads 2.07 on
composite against 3.62 on separate, with the whole frame 2.14 against 2.78.
Why the phase stops reaching the picture when the separator is in circuit
remains open.

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

- **Why the sync type decides whether the phase reaches the picture.** The
  separator is in circuit on one and not the other, and nothing measured says
  more than that: the jitter reading goes the wrong way for the obvious
  mechanism.
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

## The count dither is a content-free gate on whether the fault is live

Round-robin over five phases, four rounds each, `/phase?sp=N` holding the phase
through every solve and nothing frozen. Round-robin rather than a sweep so the
uptime gradient is shared between candidates instead of being read as a profile:

| phase | samples | off the divider | rate |
|---|---|---|---|
| 29, the engine's choice | 1143 | **0** | 0.00% |
| 22 | 1139 | **0** | 0.00% |
| 27 | 1139 | **0** | 0.00% |
| 28 | 1138 | **0** | 0.00% |
| 5 | 1141 | **0** | 0.00% |

5700 samples, `STATUS_SYNC_PROC_HTOTAL` exactly 2200 in every one of them,
including at the phase that read 4.8% earlier in the same boot.

**THAT IS A VOID COMPARISON AND NOT A FINDING THAT THE PHASES ARE EQUAL.** The
fault was absent while it ran, so there was nothing for a phase to be better or
worse at; the picture had gone from shimmering on static detail to shimmering
only on moving sprites over the same interval. A walk returning nothing across
its whole range is a walk whose premise was wrong.

**What it establishes instead is the gate every such experiment needs.** The
dither rate is measured off the chip, needs no picture and no static source, and
separates the two states by two orders of magnitude -- 4.8% of 1135 samples with
the fault visible against 0 of 5700 without it. So a phase comparison is
scheduled by polling it until the fault is live, rather than run when the bench
is free and scored on whatever state it finds.
