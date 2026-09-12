# Returning to one mode repeatedly: what is deterministic and what is not

Driven from ModeServ, returning to `X320 Y256 C256 F50` several times through an
excursion, recording what the engine measured, what it solved, and a photograph
of the panel each time.

## The solve is deterministic when nothing perturbs it

Four returns on separate sync, each via 800x600 and 640x480, with a **full 608
register dump** taken at each:

| | |
|---|---|
| dumps 0 vs 1 | **1 byte differs** -- `SP_H_CST_SP` 1668 against 1667 |
| dumps 0 vs 2 | **0 bytes differ** |
| dumps 0 vs 3 | **0 bytes differ** |

`/geometry` identical all four -- `oh` 50, `eh` 955, `ov` 38, `ev` 582, `ch`
1024, `cv` 622 -- and the picture in the same place on the panel, left edge
30/30/32/29 of 1162 camera columns.

**A subset is not enough to make this claim.** An earlier run asserted it over
fourteen chosen registers, which left out `VDS_HS_ST`/`VDS_HS_SP` -- and an
output sync start is a pan. The dump is what closes it.

## Composite sync does not lock the ADC PLL

Four returns on composite sync, same path, same source:

| | separate sync | composite sync |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 311 | **308** |
| `PLLAD_MD` | 2208 | **2050** |
| `STATUS_MISC_PLLAD_LOCK` | 1 | **0 in 4 of 4** |
| `IF_HSYNC_RST` | 1104 | 1025 |
| `VDS_HSCALE` | 547 | 513 |
| framing | `oh` 50 `eh` 955 `cv` 622 | `oh` 47 `eh` 886 `cv` 616 |

The count is what everything else follows from: composite sync measures the
311-line source as 308, the divider solved for that is 2050 rather than 2208,
and **the PLL does not lock there**. The framing difference is arithmetic on the
wrong count, not a second fault.

That is the beating. It is the same shape as
`the-adc-pll-does-not-lock-at-the-half-divider.md` -- a divider the loop
filter does not suit -- reached here by a sync type rather than by a mode.

**So the two sync types do not produce the same picture from the same source**,
and the reason is upstream of the solve: one of them miscounts the source by
three lines.

## A bright cyan bar at the extreme left, on composite sync

Two thin full-height cyan lines outside the card's own left edge, present in 4
of 4 composite-sync photographs, at camera columns 19, 20, 30 and 46. The card
itself starts near column 140 there.

`VDS_HB_SP` is **8** on both sync types, which is the floor
`../scaler-geometry-model.md` records as measured -- below 8 corrupts. So this is
the left-hand corruption at its documented edge, made visible by the wider
magnification composite sync's smaller divider produces (`VDS_HSCALE` 513
against 547).

**Whether it is also present on separate sync is NOT established.** The obvious
check is invalid: the picture starts at column 30 there, so a fixed 0..140
window is inside the picture and reports a light-blue card block as a bar. A
band has to be anchored to the picture's own edge, which is the rule
`CLAUDE.md` states and this measurement broke.

## The framing shift is real, intermittent, and not yet attributed

An earlier run of five returns, through a shorter excursion, put the picture
100 camera columns to the right on two of the five -- same width, so a
displacement rather than a rescale, and the set's own indicator lamp in the same
place, so not the camera:

| trip | left | right | width |
|---|---|---|---|
| 0, 1, 2 | 30, 30, 32 | 832, 832, 833 | ~802 |
| **3, 4** | **133, 128** | **924, 925** | ~794 |

The four-trip run with full dumps did not reproduce it. So it is intermittent,
and the run that caught it recorded only a register subset -- which is exactly
the gap that makes the encoder unprovable either way.

**The encoder is the leading hypothesis** and the test is stated:
identical registers with a displaced picture. `the-encoder-reframes-the-output.md`
already measures 57 columns adrift across an output mode change with the raster
registers identical either side, so the mechanism is established; what is
missing is catching this instance with a dump beside it.

**And the instrument is a suspect.** A full dump is hundreds of `/getreg`
requests, each deferred to `loop()`, so it slows the loop that drives
acquisition. The run WITH dumps showed no shift and the run WITHOUT showed it
twice -- consistent with the measurement changing the outcome, in the direction
that hides it.

## Why this stops here

Every conclusion above is drawn against a firmware in which `runSyncWatcher()`
and `VideoSourceAcquisition::poll()` both run, several fields have two writers,
and the classification and the engine disagree about whether the source is
present. Two paths writing the same registers on different schedules is a race,
and a race cannot be attributed by bisecting the picture: whichever ran last is
in force, and the state is self-consistent afterwards.

So the competing paths come out first, and the intermittent framing shift is
re-measured against one owner per register and one tick.
`../video-source-acquisition.md`.
