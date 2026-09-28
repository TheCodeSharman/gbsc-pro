# The ADC sampling phase cannot reach the shimmer

A sub-pixel horizontal shimmer appears on the scaling path at the shipped
divider, reads as flicker on fine detail rather than as movement, and is
**intermittent on a timescale of hours**. This page records what it is not, and
why the most obvious control cannot touch it.

## The phase is a no-op, and the arithmetic says it must be

`PA_ADC_S` is the only control over where in each source pixel the ADC lands.
Swept through all 32 steps twice on the bench RISC PC at `X320 Y256 C256 F50`,
120-frame clips, `PLLAD_MD` held at 2200 throughout:

| | |
|---|---|
| spread across the 32 phases | sd 0.0022 (pass 0), 0.0026 (pass 1) |
| spread between the passes at the SAME phase | mean 0.0024, max 0.0064 |
| correlation between the passes | **r = +0.21** |

The phase-to-phase variation is the size of the repeat noise and does not
reproduce. There is no phase response to find, and the bench confirmed it
independently by eye.

**It cannot be otherwise at this divider.** The mode's line is 512 pixel clocks
at 8 MHz (`h_timings:36,30,44,320,44,38`, giving 15625 Hz). A doubled line keeps
`PLLAD_MD / 2` samples, so the kept grid is 1100 against 512 and
`gcd(1100, 512) = 4`: along one line the sampler already visits **275 distinct
positions within the source pixel, spread uniformly**. A global phase offset
moves all 275 together and changes the distribution not at all.

So a phase sweep is only an experiment where the sample grid is commensurate
with the source pixel grid. It is worth asking what the two numbers are before
reaching for `creep_adc_phase.py` or `/sc?b`.

## The VCO gain is not the cause, though it looks like it twice

`Adc::vcoGainFor()` raises `PLLAD_FS` once the VCO passes
`HighVcoGainAboveHz` = 130 MHz, and the VCO is the divider times four line
rates, so on a 15625 Hz source the step lands at `PLLAD_MD` 2080. Dividers four
apart either side of it, interleaved, 120 frames each:

| `PLLAD_MD` | VCO | `PLLAD_FS` | picture | `HSCALE` | sd | p2p |
|---|---|---|---|---|---|---|
| 2078 | 129.875 MHz | 0 | 1391x949 | 561 | 0.0023 | 0.032 |
| 2082 | 130.125 MHz | 1 | 1390x949 | 563 | 0.0258 | 0.344 |
| 2078 | 129.875 MHz | 0 | 1391x949 | 561 | 0.0022 | 0.032 |
| 2082 | 130.125 MHz | 1 | 1390x949 | 563 | 0.0274 | 0.354 |

Eleven times the jitter, reproducing to 6%, with the picture and `PLLAD_KS`
unchanged. **It is still not the explanation**, because `PLLAD_MD` 2200 carries
the same `PLLAD_FS` 1 and is quiet, and forcing the gain either way at 2200
changes nothing that matters:

| held 2200 | `PLLAD_FS` | `htotal - md` | sd |
|---|---|---|---|
| as solved | 1 | +0 | 0.0028 |
| forced | 0 | +0 | 0.0037 |
| as solved | 1 | +0 | 0.0028 |
| forced | 0 | +0 | 0.0033 |

**`PLLAD_FS` 0 locks at a 137.5 MHz VCO**, which extends the range `Adc.h`
records from the sweep that set the threshold (gain 0 up to 136 MHz, gain 1 down
to 121 MHz). The threshold was chosen on lock alone, in the middle of that
overlap; nothing about jitter went into it.

`PllChargePump` is one fixed value on every path, so when the gain bit flips the
loop bandwidth moves with it and nothing compensates -- `Icp` and `Kvco` set it
together. That remains unexamined rather than refuted.

## Commensurability is not the cause either

`PLLAD_MD` 2048 would sample every source pixel at the same four instants where
2046 and 2050 never repeat within a line. 2046 -- the maximally incommensurate
choice -- measured four times quieter than 2200, so the ordering is the wrong
way round for a sampling-grid beat.

## The lock duty is not a quality metric, and it moves the wrong way

`STATUS_MISC_PLLAD_LOCK` reads 85.8% with 286 transitions (9.5/s) over 1100
samples on a healthy picture, matching what
[pllad-lock-is-a-duty-cycle-not-a-state.md](pllad-lock-is-a-duty-cycle-not-a-state.md)
already records. Held at an off-engine divider, with the picture's bottom edge
swinging over 40 rows inside one clip, it reads **93.7% with 126 transitions
(4.2/s)** -- its best figures of the session, on its worst picture. HTTP
sampling at ~52 reads/s gives the same answer, 95.0% against ~82%, so the
transport is not the reason.

**So the duty cannot rank one configuration against another.** The metric that
can is `STATUS_SYNC_PROC_HTOTAL` against the divider: exactly 0 in 100.0% of
1100 samples on every healthy state, dithering to -1..+1 only at an off-engine
divider.

## What the shimmer is not

- Not the sampling phase, and not reachable by it.
- Not the VCO gain, and not commensurability.
- Not the frame time lock, which was already refuted by toggling it.
- Not repeated solving: 30-second captures either side carry one solve each.
- Not re-rolled per acquisition. Eight forced source mode round trips all
  measured quiet with byte-identical registers.

## Two traps in measuring it at all

**A single-axis instrument reports a healthy unit while the picture wobbles.**
The vertical movement that reached the bench as "really wobbling, vertical too"
scored 0.0035 horizontally. What showed it was each frame's own lit edges --
bottom 1017..1057 within one clip, against an unmoving 1079 when healthy.

**The displacement estimator's gain is the content's spectrum.** A parabolic fit
over three correlation samples reads 1.24 on a sum of sinusoids and 0.435 on
step edges, so a reading is in arbitrary units until divided by the gain that
content gives. Two states compared across a change of magnification are compared
at two different gains. `picture_jitter.py` probes and divides out its own gain
for that reason; every figure on this page predates that and is uncalibrated, so
they compare within a framing and not across one.

`hdmi_capture.borders()` has a related limit on this source: an isolated dim
blob at columns 1880..1899, peak luma 44.8 with dead black either side, puts the
reported right margin at 35 where the card's content ends at column 1588 and the
true margin is 331.
