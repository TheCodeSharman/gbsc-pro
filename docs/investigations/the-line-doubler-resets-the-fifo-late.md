# The line doubler resets its FIFO late, and that is the doubled path's origin

`IF_HBIN_SP` is two things and which one depends on the scan mode. With the
line-double FIFO in circuit it is that FIFO's line reset, and moving it pans the
whole picture. It shipped at **272** ADC samples, inherited from the old preset
tables — which offered 136..272 — and chosen because the bench picture looked
right at it.

It is late by about 120 samples, and that displacement is the whole of why a
line-doubled source's picture was captured 28 to 60 source pixels early while
every duration the engine measured read correct.

`SyncProcessor::RetimeOriginSamples = 63` is **not** implicated: it is right on
undoubled sources and scan-independent, and the measurement below is what says
so.

## The instrument: clip the feature, do not locate it

A position read off the emitted frame converts a dongle column to a capture unit
through an anchor — that the dongle's column zero is the first output pixel the
display window shows. That holds at 1080p and carries the transmitted window's
latch, and it is worth more than the effect being measured: the same state read
at 1080p, 720p and 960p moved 57 source pixels with nothing on the input side
touched.

`tools/gbsc-pro-hwtest/counter_origin.py` asks the counter instead. Frozen at
full framing, it creeps the capture window's own start one unit at a time until
the card's green frame — one source pixel on the outermost framebuffer pixel —
leaves the capture. The value it leaves at IS the feature's coordinate in the
input formatter's counter, whatever the output is doing.

**It reproduces the retime investigation's numbers by a different method**,
which is what validates it:

| state | its null | the retime measurement's |
|---|---|---|
| 640x480@60 | `SP_RT_HS_SP` 1339 | 1338.5 |
| 1024x768@60 | 1361 | 1360.0 |
| X320 Y256 F50 at `PLLAD_MD` 1800 | 1615 | 1615.2 |
| X320 Y256 F50 at 2200 | 1984 | 1985.0 |

Against the mode file the two undoubled states read **+1.5** and **−0.1** ADC
samples, so the 63 is correct where it was fitted.

**There is no sampling-density floor on it.** The green frame is unreadable
below about 1.4 capture units per source pixel when its POSITION is wanted; a
clip is a presence test, and 1024x768@60 reads to a tenth of a sample at 1.07.

**Only the window's START clips cleanly.** Shortening the window instead leaves
the playback fetch sized for the old width, so past the picture's new end the
aperture shows memory nothing wrote — as often green as anything else, and it
reads as a feature that never leaves.

## The reset is the whole doubled term, at one sample per register unit

`X320 Y256 F50`, `PLLAD_MD` 2200, everything else held:

| `IF_HBIN_SP` | the source's active start | against the file's 110 |
|---|---|---|
| 272 | 80.9 px | −125.3 ADC samples |
| 208 | 95.8 px | −61.3 |
| 144 | 110.6 px | **+2.7** |

128 register units moved the picture 128 ADC samples, linear through the middle
point. The null is at 146.7 — and 272 − 146.7 is 125, which is the displacement
the doubled sources showed.

## It is a count, not a fraction of the line

Holding the divider at 1800 rather than 2200 on one source moved the value it
wants from 146.7 to 152. A fraction of the line would have moved it to 120.

## One value, because the split has no mechanism

What each mode wants, from the same instrument:

| mode | field rate | `PLLAD_MD` | wants |
|---|---|---|---|
| X640 Y256 F50 | 50.08 | 2200 | 141 |
| X768 Y288 F50 | 50.08 | 2200 | 145 |
| X320 Y256 F50 | 50.08 | 2200 | 147 |
| X320 Y256 F50 | 50.08 | 1800 | 152 |
| X640 Y240 F60 | 60.1 | 2200 | 175 |
| X640 Y200 F60 | 59.9 | 2200 | 175 |

The 50 Hz cluster spans three source rasters and two dividers and agrees within
11 samples. The two 60 Hz modes agree with each other to one sample and sit 30
away.

**Nothing on the chip differs across that split.** Read on both, every field is
identical: `PLLAD_MD` 2200, `PLLAD_KS` 2, `PLLAD_CKOS` 0, `PLLAD_ICP` 6,
`PLLAD_FS` 1, both decimators in, `IF_HBIN_SP`/`ST`, `IF_HS_DEC_FACTOR`,
`IF_PRGRSV_CNTRL`, both coasts 0, `SP_DLT_REG` 0, and `HLOW_LEN` 156 against
157. The two field rates do solve different output rasters, 1916 against 1594,
but the instrument cannot see the output at all.

The two 60 Hz modes' shortfall is the same TIME rather than the same count —
0.89 us at 13.5 MHz and 0.87 us at 16 MHz — which points at the source rather
than the scaler, and nothing measured confirms it. A field-rate branch in a
counter origin is a fit and not a fact, so there is one value: **160**, which
takes every mode within 15 samples where 272 was out by 127.

## What it is worth, engine-solved with nothing hand-set

The source's active start against the mode file:

| mode | at 272 | at 160 |
|---|---|---|
| X320 Y256 F50 | −28.5 px | −2.5 |
| X640 Y256 F50 | −60.5 | −9.8 |
| X768 Y288 F50 | −59.3 | −8.1 |
| X640 Y240 F60 | −38.0 | +5.8 |
| X640 Y200 F60 | −44.8 | +7.2 |

The undoubled path does not go through it and does not move: 640x480@60 +1.1 px,
1024x768@60 −1.3, 800x600@60 +4.9.

## The vertical counter's origin is a count of lines, and the same one

Read by the same clip, the frame counter's origin sits a fixed distance after
the vsync pulse's LEADING edge, which is the edge a video standard counts from:

| mode | pulse | the file's first active line | measured | origin sits |
|---|---|---|---|---|
| X320 Y256 F50 | 3 | 36 | 31.0 | 5.0 lines in |
| X640 Y240 F60 | 6 | 21 | 15.9 | 5.1 lines in |

Two sources whose pulses differ by a factor of two agree. **Comparing against
the pulse's trailing edge instead manufactures a disagreement**, because the
pulse is then inside the comparison.

`SourceTiming::VerticalOriginLines` states 7 for this, measured on undoubled
published rasters, and it is applied inside `SourceTiming::activeStart()` —
which returns zero for a source matching no published raster, so it reaches
none of the modes above.
