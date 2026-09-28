# The capture origin has a term at each end of the cable

`SyncProcessor::RetimeOriginSamples` is 63 ADC samples, added to the retime stop
the input formatter's line counter takes its origin from. Measured against the
mode file at full framing, the source's active region lands anywhere from 2.5
samples early to 10 late, and which it is varies by mode.

**One term of it is found and corrected.** Oversampling ratio one puts the origin
16 samples earlier than ratio two, and the two 108 MHz modes were the only states
reaching it -- which is what they were reading 17 samples off for.
`SyncProcessor::UndecimatedOriginSamples` carries it.

**Two controls settle that the remainder is not one quantity.** Neither end of
the cable can produce what the other's control measures, so both contribute and
no single correction absorbs either:

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

**The 108 MHz half of that structure is the oversampling step below**, and
correcting it leaves the 1024x768 pair as the open part.

**So those coefficients describe one family of modes and are not a correction.**
What survives the wider set is the pair of controls, not the line through them.

## The two 108 MHz modes were the only two at oversampling ratio one

`Adc::postDividerFor()` crosses at CKO 80 MHz and `applyOversample()` reduces the
ratio to what the post divider has room for, so a source whose divider and line
rate put CKO above 80 MHz runs `PLLAD_KS` 0 and oversampling ratio **one**, with
both decimators bypassed. Of the thirteen states above those two are the only
ones; the rest run two, and one held point runs four.

**Holding the divider across that crossover moves the offset 16 samples with the
source untouched** -- one pixel clock, one sync width, one line rate, one
polarity:

| mode | `PLLAD_MD` | CKO | `KS` | ratio | d samples |
|---|---|---|---|---|---|
| 1280x960@60 | 1444 | 86.64 MHz | 0 | 1 | +17.0 |
| 1280x960@60 | 1380 | 82.80 MHz | 0 | 1 | +16.0 |
| 1280x960@60 | 1320 | 79.20 MHz | 1 | 2 | +0.8 |
| 1280x960@60 | 1200 | 72.00 MHz | 1 | 2 | +1.1 |
| 1280x1024@60 | 1260 | 80.62 MHz | 0 | 1 | +16.5 |
| 1280x1024@60 | 1240 | 79.34 MHz | 1 | 2 | +0.7 |

The last pair is a 1.3% change of divider either side of the boundary. **So the
pixel clock, the sync width and the line rate are all refuted**: none of them
moved, and a term belonging to any of them cannot do this.

**It is the RATIO and not the PLL's own crossover row.** The two move together on
every state the engine reaches, so they are separated by forcing ratio one at row
one. `/sc?o` does it: `PLLAD_CKOS` and the decimators move together, so the kept
rate -- and the whole solved geometry -- stays where it was, which is what makes
the pair comparable. At `PLLAD_MD` 1240 and CKO 79.34 MHz, one divider, one VCO,
one capture window:

| ratio | d samples |
|---|---|
| 2 | +0.7 |
| **1** | **+16.8** |

**And it is constant in samples, not in time.** Seven ratio-one states over two
modes, seven dividers and CKO 79.3..92.4 MHz mean **16.83** with an sd of 0.50,
where a fixed 195 ns would have fallen 2.3 samples across that range. Four
ratio-two states mean **0.90**, sd 0.16. The step is **15.9 ADC samples**.

**Ratio four is not a further step of the same kind.** A latency of N ADC
conversion clocks would cost N kept samples at ratio one, N/2 at two and N/4 at
four, so a 16-sample step from one to two implies 8 more from two to four.
`320x480@60` held at `PLLAD_MD` 800 runs ratio four and sits 0.4 samples off the
ratio-two family's own line, not 8. `SyncProcessor::UndecimatedOriginSamples`
therefore states a step at ratio one alone rather than a `32/ratio` law.

Two explanations were closed before the divider hold, and neither was needed:

- **Not the machine's video bandwidth.** `1280x960@60` at 256 colours asks
  73.7 MB/s of video DMA, four times what it asks at 16. The two read **+17.1 and
  +16.9**, with the same solved divider, raster and density.
- **Not a raster the source cannot deliver.** The measured line rate matches the
  file to 0.00% on both -- 60.000 kHz against 60.000, and 63.981 against
  63.981 -- with the vertical totals right as well.

**Fixed, and measured on the unit.** `SyncProcessor::UndecimatedOriginSamples`
is 16, added to the origin at ratio one, with the ratio-two path left alone:

| mode | ratio | before | after |
|---|---|---|---|
| 1280x960@60 | 1 | +17.0 | **-1.1** |
| 1280x1024@60 | 1 | +16.9 | **+1.3** |
| 1024x768@60 | 2 | -1.9 | -1.9 |
| 320x480@60 | 2 | +10.1 | +9.6 |

The realised movement is 18.1 and 15.6 samples against the 16 written, which is
the two-samples-between-acquisitions spread below rather than a wrong constant --
the two before-and-after pairs are different acquisitions either side of a flash.

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
