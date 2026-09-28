# The capture origin is not a sample count, and the divider is not what varies it

`SyncProcessor::RetimeOriginSamples` is 63 ADC samples, added to the retime stop
that the input formatter's line counter takes its origin from. Measured against
the mode file at full framing, undoubled modes want anything from 62 to 91, and
the spread has been read as a function of the line rate.

It is not one. At a **single line rate, field rate, sync polarity and divider**,
five modes still disagree by nine samples — and they agree to half a source pixel.
The quantity the correction has to carry is a count of the SOURCE's pixels, and
`SP_RT_HS_SP` counts ADC samples, so no single value in it can be right for two
modes whose sampling densities differ.

## The measurement

`full_margins.py` at full framing, `PATTERN CARD`, `ANIM OFF`. The lead is the
black before the card's green frame, against the mode file's sync + back porch +
left border. `d px` is in source pixels and `d samples` is the same reading
through the mode's own samples per source pixel, which is `PLLAD_MD / htotal`.

Five H-negative modes, all at 31.47 kHz and 59.9 Hz, all solved at `PLLAD_MD`
1442, spanning a **four-fold range of samples per source pixel**:

| mode | htotal | samples/px | `HLOW_LEN` | d px | d samples |
|---|---|---|---|---|---|
| 1280x480 | 1600 | 0.901 | 167 | **+3.1** | +2.8 |
| 720x480 | 858 | 1.681 | 100 | **+2.7** | +4.6 |
| 640x480 | 800 | 1.803 | 169 | **+2.4** | +4.3 |
| 360x480 | 532 | 2.711 | 170 | **+3.4** | +9.1 |
| 320x480 | 400 | 3.605 | 148 | **+3.3** | +11.8 |

In source pixels the spread is 1.0 on a mean of 3.0, which is 14%. In ADC samples
it is 9.0 on a mean of 6.5, which is 58%. The sync width runs from 100 to 170
samples across the set and moves neither figure.

## What that rules out, and how

Each of these is a control rather than a curve: the quantity named is the only
one that moves.

**The sampling density, as a sample count.** Above. A four-fold range of samples
per source pixel leaves the error constant in source pixels.

**The pixel clock, and the back porch.** `800x600@75` and `1600x600@75` are
timing twins — identical sync duty, line rate, vertical total and polarity, so
the sync waveform reaching the chip is the same, and both solve `PLLAD_MD` 1166
and the same retime stop. Their pixel clocks differ by exactly two, 49.5 against
99.0 MHz, and their back porches by 3.23 against 1.78 us. `800x600@60` and
`1600x600@60` are the same pairing at 40.0 against 80.0 MHz. Both pairs agree:
+8.2 against +5.8 samples at 60 Hz, and +6.1 against +8.5 source pixels.

**The sample rate.** The 60 Hz pair samples at 54.5 MHz and the 75 Hz pair at
54.7 MHz, and they read 21 samples apart.

**The measurement's anchor.** `full_margins.py` maps dongle column zero to the
capture window's start; `counter_origin.py` creeps the window's own start until
the feature leaves and so carries no anchor at all. On three 60 Hz modes the two
differ by a constant:

| mode | anchor-free | against the mode file | difference |
|---|---|---|---|
| 1024x768@60 | +0.1 | +2.1 | 2.0 |
| 800x600@60 | +5.1 | +8.2 | 3.1 |
| 1280x960@60 | +15.0 | +17.7 | 2.7 |

So the anchor is worth about 2.6 samples and is constant within one output
raster, and the fifteen-sample spread beside it is the counter's own. That also
makes the cheaper instrument valid for comparisons taken at one output raster.

## Three source pixels of it are the source's

The constant across the 31.47 kHz set is about **3.0 source pixels**, independent
of the pixel clock over a four-fold range. A delay that is a fixed number of the
source's own pixel clocks is what a video chip's output pipeline is, so it is not
evidence about the scaler, and it is not correctable from anything the chip can
measure — the horizontal axis has no native resolution and the source's pixel
clock is unknowable. `docs/capture-limits.md`.

Subtracting it and reading the residual back in ADC samples splits the set by
**sync polarity**:

| sync | modes | line rate | residual |
|---|---|---|---|
| H-negative | 640x480, 720x480, 320x480, 1280x480, 360x480 | 31.5 kHz | −0.5 .. +1.1 |
| H-negative | 1024x768@60 | 48.4 | −1.1 |
| H-positive | 800x600@60, 1600x600@60 | 37.9 | +4.2, +3.7 |
| H-positive | 1280x1024@60, 1280x960@60 | 64.0, 60.0 | +14.3, +15.2 |

**On negative sync the shipped 63 is already right**, over six modes and two line
rates. The error is a positive-sync one, and it grows with line rate there.

## The measured pulse is biased by polarity too, and in the same direction

`STATUS_SYNC_PROC_HLOW_LEN` against the mode file's own sync width at the divider
in force, nine modes:

| sync | bias in samples | in time |
|---|---|---|
| H-negative | −5.4, −3.1, −3.2, −4.4 | −77.6, −68.3, −73.2, −62.6 ns |
| H-positive | +1.7, +1.7, +2.4, +2.3, +2.1 | +31.2, +31.1, +26.0, +26.6, +26.7 ns |

Both are tighter as a time than as a count — 11% against 29% on negative sync,
9% against 17% on positive — so this is a delay and not a counting error, and the
two sync polarities are about 98 ns apart. It reaches the origin because
`retimeStopFor()` subtracts this reading, but at two to five samples it accounts
for none of the spread above.

The polarity is normalised through `SP_HS_INV_REG` before the count is taken, so
a positive-going source is measured through an extra inversion. That the residual
origin error and the pulse bias both separate on the same boundary is the reason
to look there first.

## What has not been established

Why the positive-sync residual grows with line rate. Three points carry it —
+4.0 at 37.9 kHz and +14.8 at 60 to 64 kHz — which is a slope through two
clusters, not a mechanism. A fourth positive-sync line rate between them is what
would say whether it is a line, and `1600x1200@60` at 75.0 kHz and
`1024x768@75` at 60.0 kHz are both in the monitor definition.

## Traps

- **`full_margins.py` needs both of the card's green edges** and reports the
  lead from their separation. A mode whose picture overruns the emitted frame has
  only one, and the run is then refused rather than reported. The ruler does not
  have to be measured: `PLLAD_MD / htotal` agrees with the separation to 0.10%
  on every mode where both edges are found.
- **The 75 Hz output raster does not give a repeatable reading.** 1125 lines at
  75 Hz is not a standard mode, and `800x600@75` read +8, +29 and +17 samples on
  three acquisitions while `1600x600@75` twice lost the picture off the right of
  the frame. Every 60 Hz mode here repeats to 0.3 samples. Take no origin
  reading at 75 Hz without repeating it.
- **`counter_origin.py`'s vertical creep reports its own failure** by disagreeing
  with its expectation. `1024x768@60` crept 4..39 expecting 29 and crossed at
  5.5, which is the run-up clamped away at the floor rather than a reading.
