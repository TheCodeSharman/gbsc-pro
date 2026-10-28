# The sync phase sets the ADC family's ceiling

`PA_SP_S` and `PA_ADC_S` are both positions inside the ADC clock period, and
the question that keeps returning is whether one predicts the other.
`no-scan-derives-the-adc-sampling-phase.md` refutes that across eight
acquisitions. This page is the JOINT grid inside one acquisition, which
`phase_grid.py` was written for and which had no recorded result.

It answers a different question, and the answer is not a correlation: the sync
phase decides what the ADC family can REACH.

## The grid

1280x720@60 in pass-through, `PLLAD_MD` 2038, count 749, frozen, the wedge box
found once and held. `PA_ADC_S` swept at step 2 across the field for each of
seven `PA_SP_S` values. Two independent acquisitions, the source bounced away
and back between them.

| `PA_SP_S` | peak contrast, grid 1 | grid 2 | best `PA_ADC_S` |
|---|---|---|---|
| 30 | 53.3 | 52.9 | 18 |
| 0 | 53.3 | 52.9 | 18 |
| 4 | 53.3 | 52.8 | 18 |
| 6 | 52.9 | 52.2 | 18 |
| 7 | 50.8 | 50.4 | 16, 20 |
| 11 | **27.8** | **24.9** | 22 |
| 16 | **32.2** | **30.4** | 16 |

**A BAD SYNC PHASE CANNOT BE RECOVERED BY ANY ADC PHASE.** At `PA_SP_S` 11 the
whole row is capped at about 26 against the 53 three other rows reach -- the
ceiling is halved and every ADC phase is under it. So the sync phase is not one
of two comparable knobs; it bounds what the other can do.

**AND THE RANKING IS REPRODUCIBLE.** Two acquisitions agree to within three
grey levels on every row and preserve the order exactly, so the arc does not
re-roll the way the ADC optimum does.

## It refutes the offset model again, inside one acquisition

`PA_SP_S` 30, 0, 4 and 6 all put the ADC optimum at **18**, in both grids. Four
sync phases spanning eight steps, one ADC optimum.

That is stronger than the eight-acquisition refutation it agrees with, because
the capture origin is held by construction rather than by selecting trials that
shared one: there is no re-acquisition inside a grid. A model in which the scan
already contains the ADC's answer has no room left.

## What it costs to be wrong, and where the edge is

The harm is not a step at the band's edge. Taking grid 1's peaks against the
53.3 that 30, 0 and 4 reach:

| `PA_SP_S` | reachable | cost |
|---|---|---|
| 0, 4, 30 | 53.3 | none |
| 6 | 52.9 | under 1% |
| 7 | 50.8 | 5% |
| 16 | 32.2 | 40% |
| 11 | 27.8 | 48% |

**So the quiet family is an ARC through zero, not the band either soak
assumed.** Measured separately on a third acquisition with `PA_ADC_S` pinned
and the whole sync field walked, the movement between frames -- which is what a
badly chosen sync phase puts across the entire picture -- reads 1.17..1.21 grey
levels for `PA_SP_S` 23..5 and rises to 3.34 at 11, a factor of 2.9.

**A phase of 0 to 5 therefore costs nothing**, and the soak band of 6..19 that
`phase_after_input_change.py` scores against counts those as failures. What is
actually harmful is roughly 7..22.

## The metric, and one that inverts

Contrast on PM5544's finest grating is the ADC phase's measure. For the sync
phase the symptom is a high horizontal frequency across the WHOLE picture, so
the measure is movement between frames with the card's animation off.

**A HIGH-PASS MEASURE OF A TIME-AVERAGED CLIP INVERTS, AND READS BACKWARDS.**
Averaging a shimmering picture blurs it, and blur has less high-frequency
content than a steady one -- measured, the mean absolute second difference
along x reads 4.20 at `PA_SP_S` 11 and 6.16 at 30, so the unstable phase scores
as the cleaner. Score movement, or score a single frame.
