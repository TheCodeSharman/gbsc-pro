# What a line-doubled source's blanking measures against its mode file

At full framing the capture window spans the whole capturable line and frame, so
the emitted picture carries the source's own blanking at every edge. The card's
green frame marks the outermost framebuffer pixels, which makes its two edges a
known number of source units apart and so gives capture units per source pixel.
Every margin then reads in the source's own pixels, against what the mode file
states the source emits.

`tools/gbsc-pro-hwtest/full_margins.py` is the measurement.

## The engine measures the raster correctly and places it wrongly

Bench RISC PC on `vga`, separate sync, `X320 Y256 C256 F50`, `PLLAD_MD` 2200,
line doubled, output 1920x1080. Nothing hand-set:

| | measured | file | error |
|---|---|---|---|
| line total | 512.3 px | 512 | +0.3 |
| pixel clock | 8.005 MHz | 8.000 | +0.005 |
| sync width | 36.3 px | 36 | +0.3 |
| frame | 312 lines | 312 | 0 |
| h active | 320.0 px | 320 | 0.0 |
| v active | 256.0 lines | 256 | 0.0 |
| h lead | 81.5 px | 110 | **−28.5** |
| h trail | 110.8 px | 82 | **+28.8** |
| v lead | 30.8 lines | 33 | **−2.2** |
| v trail | 25.2 lines | 23 | **+2.2** |

Every DURATION is right to a third of a pixel. Both counter ORIGINS sit late, so
the source's picture is captured earlier on the counters than it really is --
28.5 source pixels horizontally, 122 ADC samples, and 2.2 lines vertically.
Repeatable to a tenth over successive reads.

## The two counters take their origins from different edges

The line counter's origin is the hsync LEADING edge, which is the edge a video
standard counts from, so the whole pulse is in the expected lead. The frame
counter's is the vsync TRAILING edge, so the pulse is not -- it falls after the
front porch instead. Comparing the vertical lead against the file's
`sync + back porch + border` rather than `back porch + border` reports a 5.2
line error where there is a 2.2 line one.

## An IF unit is two ADC samples, and the vertical window counts doubled lines

Both of the chip's own figures cross that boundary on a doubled source and both
have to be converted before they meet a window register:

- `STATUS_SYNC_PROC_HLOW_LEN` counts ADC samples; `IF_HB_SP2`/`IF_HB_ST2` count
  IF units. Dividing the line counter by the sync width without converting
  reports a 512-pixel line as 256.
- `IF_VB_SP`/`IF_VB_ST` count DOUBLED lines where `STATUS_SYNC_PROC_VTOTAL`
  counts the source's. Leaving that out reports 256 active lines as 560.

`PLLAD_MD / IF_HSYNC_RST` is the factor, 2 or 1.

## The retime stop that frames the raster, on a doubled source

Walking `SP_RT_HS_SP` with automation frozen moves the picture one ADC sample
per register unit -- the same step as on an undoubled source, so the register
does not follow the doubling. At `PLLAD_MD` 2200 on `X320 Y256 C256 F50`:

| `SP_RT_HS_SP` | h lead | error |
|---|---|---|
| 2107 (the engine's) | 81.5 | −28.5 |
| 2050 | 94.8 | −15.2 |
| 2014 | 103.1 | −6.9 |
| 1985 | 109.9 | −0.1 |
| 1950 | 118.0 | +8.0 |

Four source rasters, all at `PLLAD_MD` 2200 and the same 1080p output, each
jumped straight to the stop one reading predicts and read back within 0.5 px:

| mode | field rate | output raster | `HLOW_LEN` | framing stop | correction |
|---|---|---|---|---|---|
| X320 Y256 F50 | 50.08 | 1916 | 155 | 1985.0 | −123 |
| X640 Y256 F50 | 50.08 | 1916 | 155 | 1978.2 | −130 |
| X768 Y288 F50 | 50.08 | 1916 | 164 | 1971.6 | −127 |
| X640 Y200 F60 | 59.9 | 1604 | 156 | 2011.1 | −97 |
| X640 Y240 F60 | 60.1 | 1604 | 156 | 2009.1 | −97 |

**Three source rasters in one output state agree within 7 ADC samples.** That
comparison is anchor-free: whatever constant the instrument's absolute mapping
carries is the same for all three, so it cancels in their spread. A correction
that followed the source's sync width would not be constant there, and one that
followed the line total would not either -- the three lines are 512, 1024 and
1024 source pixels long.

## What is NOT established

**The absolute correction, and the split between the two field rates.** Both
rest on the instrument's anchor -- that the dongle's column zero is the first
output pixel the display window shows. At 1080p that is sound in construction:
the engine sizes the display window to the encoder's active fraction, and the
write start lands within about a pixel of the window's start. Its uncertainty is
the transmitted window's latch, which two readings of one state either side of
an output excursion put at about 10 dongle columns, 5 ADC samples here.

The two field rates solve DIFFERENT output rasters, 1916 against 1604, so their
30-sample split is not anchor-free. It is six times the latch's own scatter,
which makes it more than noise and less than settled.

**Whether the correction is a count or a fraction, in the doubled regime.**
Every 15 kHz mode the bench source offers has a sync duty of 7.0 to 7.4 per
cent, which is the same degeneracy that made `0.93 x PLLAD_MD` look right --
`HLOW_LEN` and `PLLAD_MD` cannot be told apart as the term that scales. Holding
a different divider does not break it either, because a fixed-time pulse's
sample count scales with the divider.

## Refuted: changing the output resolution as the discriminator

Holding the source and `SP_RT_HS_SP` still and switching the output resolution
moves the measured lead by 57 source pixels, which reads as proof that the whole
error belongs to the instrument. It is not: `full_margins.py` converts dongle
columns through the encoder's 1080p line total, so at 720p and 960p it is out of
its range. What says so is on the same readings -- at 720p it reports 383.5
active lines for a 256-line source, and at 960p 288.0.

The two 1080p readings either side of that excursion differ by 4.7 source
pixels. That part is real and is the latch.

## Traps

- **A one-row green run at the last row of the frame is unwritten memory, not
  the card.** Taking the outermost green runs as the frame's edges stretches the
  picture a third beyond its own raster and reports the vertical lead 3 lines
  out. Choose the pair whose separation matches what the scale and the card's
  own size imply, and print the residual so a bad pick is visible.
- **The card's patterns animate**, so a frame difference is contaminated by the
  flash rather than by the register under test. `ANIM OFF` on ModeServ stops it.
- **Blanking black over black shows nothing**, so a first-lit test calibrating a
  window's edge reports where the picture's content starts. At a default framing
  the outermost thing on screen is the source's own blanking, so there is
  nothing at the edge to extinguish.
- **`/sampleclock?hold=2600` leaves the vertical window degenerate** on this
  source -- `IF_VB_SP` 2, `IF_VB_ST` 0 -- and no picture reaches the dongle.
  Releasing the hold recovers it.
- **The card's green frame is unmeasurable below about 1.4 capture units per
  source pixel**, which a held divider reaches quickly: 1500, 1650 and 2000 all
  failed to show a frame on a 512-pixel line where 1800 and 2200 read cleanly.
