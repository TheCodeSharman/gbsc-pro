# The capture origin has a term at each end of the cable

`SyncProcessor::RetimeOriginSamples` is 63 ADC samples, added to the retime stop
the input formatter's line counter takes its origin from. Measured against the
mode file at full framing, the source's active region lands anywhere from 2.5
samples early to 17 late, and which it is varies by mode.

**Two controls settle that this is not one quantity.** Neither end of the cable
can produce what the other's control measures, so both contribute and no single
correction absorbs either:

- **The scaler has a term**, because holding one source at one mode and moving
  only the divider moves the offset by 6.9 samples. The source cannot see the
  divider.
- **The source has a term**, because two modes the chip cannot tell apart land
  2.2 samples apart. The scaler cannot see the pixel clock that separates them.

Every reading here is anchor-free: `counter_origin.py`, which creeps the capture
window's own start until the feature leaves the counter. `full_margins.py` reads
about two samples higher and that has to come off first.

## The divider moves it, with the source untouched

One mode, one source, the engine solving the whole geometry around a held
divider, with its own unheld choice beside them:

| `PLLAD_MD` | units/px | sample rate | d samples | d source px |
|---|---|---|---|---|
| 800 | 2.00 | 25.17 MHz | +3.2 | +1.6 |
| 1400 | 3.50 | 44.06 MHz | +9.4 | +2.7 |
| 1440, unheld | 3.60 | 45.31 MHz | +10.1 | +2.8 |

Neither unit is constant, so the offset is neither a fixed count of samples nor
a fixed time. The held points and the unheld one lie on one trend, which is what
says a hold is a sound way to take this.

## The pixel clock moves it, with every observable timing identical

`800x600@60` and `1600x600@60` are the monitor definition's only pixel-clock
twins: sync 3.2000 us, line 26.400 us, active starting 5.4000 us in, same
polarity and field rate, and the engine solves the same `PLLAD_MD` 1438 and the
same retime stop 1325. Nothing the TV5725 can observe differs. Three interleaved
acquisitions each:

| | units/px | d samples | d source px |
|---|---|---|---|
| 800x600@60, 40 MHz | 1.363 | +5.1, +5.1, +5.1 | +3.7, +3.8, +3.7 |
| 1600x600@60, 80 MHz | 0.681 | +2.9, +2.9, +2.9 | +4.2, +4.2, +4.2 |

They repeat to better than 0.1 source pixels and land 2.2 samples apart. **Both
sample at one rate**, so *constant in ADC samples* and *constant in time* are one
hypothesis across this pair, and it is refuted.

A delay common to a source's sync and its video cancels, because the scaler
times video from the sync edge. What does not cancel is a *differential* delay
between a pixel-clocked video path and the sync generator beside it, which is
what a video FIFO produces.

**They are not one entry doubled.** Sync 128 -> 256 and the line total
1056 -> 2112 both double, while the back porch goes 88 -> 98 and a 78-pixel
border appears either side. What the comparison needs is doubled: the sync
width, the line total and the distance to the first framebuffer pixel. The
**75 Hz pair is not a twin for this question** -- its active regions start
4848.5 and 3393.9 ns in -- so it compares quantities belonging to the sync path
and not the placement of video.

## A line in sampling density fits one family and does not generalise

Seven H-negative readings at 25 to 45 MHz, spanning three modes, three dividers
and a four-fold range of density, fit

    d = 3.97 x (units per source pixel) - 4.33 ADC samples

to a residual rms of 0.56 samples and a worst case of 0.86 over a 10-sample
range, with six H-positive readings a constant 4.2 above it. **Five modes
outside that set refute it.** Fitted over all thirteen states, the best model in
density, sample rate and polarity leaves a residual rms of 4.4 samples, and the
residuals are structured rather than scattered: both 1024x768 modes sit 8.5
low and both 108 MHz modes 5 to 7 high.

**So those coefficients describe one family of modes and are not a correction.**
What survives the wider set is the pair of controls, not the line through them.

## Two anomalies the density line does not reach

| mode | pixel clock | d samples |
|---|---|---|
| 1024x768@60 | 65.0 MHz | -1.9 |
| 1024x768@70 | 75.0 MHz | -2.5 |
| 1280x960@60 | 108.0 MHz | **+17.1** |
| 1280x1024@60 | 108.0 MHz | **+17.3** |

The two 108 MHz modes land about 190 ns late where every mode at or below
80 MHz lands within 10 samples of the file. They repeat to 0.2 samples within a
session and to about 2 across sessions, and they carry the two highest pixel
clocks the bench can measure. Two explanations are closed:

- **Not the source's video bandwidth.** `1280x960@60` at 256 colours asks
  73.7 MB/s of video DMA, four times what it asks at 16 colours. The two read
  **+17.1 and +16.9**, with the same solved divider, raster and density. A fetch
  that could not keep up would move the picture and does not.
- **Not a raster the source cannot deliver.** The measured line rate matches the
  file to 0.00% on both -- 60.000 kHz against 60.000, and 63.981 against
  63.981 -- with the vertical totals right as well. The source is producing
  exactly what the definition states at 108 MHz.

## Traps

- **The anchor belongs in ADC samples, not in source pixels, and it is not quite
  a constant either.** Anchored against anchor-free over five modes spanning a
  four-fold range of density it runs 1.67 to 2.76 samples -- a 48% spread,
  against 184% for the same differences read as source pixels. Some of that 48%
  is the cross-session drift below. Take it off before converting an anchored
  reading to source pixels, or do not convert it.
- **`VDS_HSCALE` is 10 bits and the scaler cannot shrink**, so the captured line
  has to fit the output raster, and that is what caps a divider hold -- about
  `PLLAD_MD` 1780 against a 1600-unit raster. Above it the card's far edge
  leaves the frame and the reading is refused rather than wrong. The same bound
  refuses `1400x1050@60` outright.
- **A feature below one capture unit per source pixel is caught by a single
  sample**, which quantises the crossing by half a unit -- 0.5 samples against
  the 2.2 the twins differ by, so it does not account for them. `PROCframe`
  draws one pixel and takes no width, so a thicker feature needs a source change.
- **One mode's reading moves about 2 samples between sessions** while repeating
  to 0.2 within one. Compare readings taken in one session, or carry the spread.
- **Origin readings at 75 Hz do not repeat at all.** 1125 lines at 75 Hz is not a
  standard mode; `800x600@75` read +8, +29, +17 and +15 samples on four
  acquisitions.
- **A reading taken while anything else drives the source is not a reading.** A
  mode change landing inside a creep moves the picture under a frozen unit, and
  the crossing that comes back is neither state -- measured, `1280x1024@60` read
  +10.2 that way against +17.3 undisturbed.
- **`counter_origin.py`'s vertical creep reports its own failure** by disagreeing
  with its expectation. The horizontal is the sound one.
