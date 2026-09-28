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

**A GREEN RUN ON THE EMITTED FRAME'S OWN FIRST COLUMN IS NOT THE CARD, and
taking it as the near edge reads the source's video 64.8 px early.** Measured on
the bench source at full framing, the runs are `(0,5)`, `(280,287)` and
`(1596,1603)`: under a 20-unit step of `SP_RT_HS_SP` the card's two edges moved
19 columns together and the run at 0..5 did not move at all. `near_run()` refuses
a run touching either boundary now, and the reading it refuses is a plausible
number rather than a broken one — which is what made it expensive.

**A SECOND INSTRUMENT TERM SITS ON THE SAME READINGS.** `half_crossing()`
answers where the feature is half gone, which is its CENTRE, and the tables took
that as where it starts -- half a source pixel late, at whatever counter units
the density makes that. `leading_edge()` takes it off now, verified on the bench
at 1.1 counter units against a predicted 1.075.

**So every reading in the tables below predates that guard and that correction**, and the bench mode
re-reads on the corrected instrument at a null of `IF_HBIN_SP` **181** against
the 147 filed here, repeatable to 0.1 sample and giving one null from four
different `IF_HBIN_SP` values. Only 5 of that 34 is the retime correction
`SyncProcessor::InvertedPulseWidthSamples` accounts for. **The whole set wants
re-measuring before the constant moves**, and what moved the other 29 is not
known.

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
| X1056 Y256 F50 | 50.08 | 2200 | 137 |
| X640 Y256 F50 | 50.08 | 2200 | 141 |
| X768 Y288 F50 | 50.08 | 2200 | 145 |
| X320 Y256 F50 | 50.08 | 2200 | 147 |
| X320 Y256 F50 | 50.08 | 1800 | 152 |
| X640 Y240 F60 | 60.1 | 2200 | 175 |
| X640 Y200 F60 | 59.9 | 2200 | 175 |
| X640 Y352 F60 | 60.0 | 2200 | 179 |
| X896 Y352 F60 | 59.9 | 2200 | 175 |

The 50 Hz cluster spans four source rasters -- lines of 512, 858, 1024 and 1536
source pixels -- and two dividers, and agrees within 15 samples. The four 60 Hz
modes agree within 4 and sit about 32 away.

**The 60 Hz side is not a property of the two 15 kHz game modes.** That was the
live alternative -- both original 60 Hz points are 15.7 kHz, so field rate and
those two rasters were one variable. The last two rows separate them: they are
21.85 kHz, vertical total 364 rather than 262, horizontal totals 768 and 1100,
and pixel clocks of 16.783 and 24 MHz, and they land on the same value. Every
doubled mode the monitor definition offers has now been read, so the bench has no
further point to add: below 400 lines it carries seven at 50 Hz, all of them
vertical total 312, and four at 60 Hz.

**What the split is NOT is a constant time.** The gap is a constant COUNT:

| mode | wants | gap, samples | gap, time | line rate |
|---|---|---|---|---|
| X640 Y240 F60 | 175 | 30.5 | 0.880 us | 15.73 kHz |
| X640 Y200 F60 | 175 | 30.5 | 0.883 us | 15.69 kHz |
| X640 Y352 F60 | 179 | 34.5 | 0.717 us | 21.85 kHz |
| X896 Y352 F60 | 175 | 30.5 | 0.635 us | 21.82 kHz |

`PLLAD_MD` is pinned at 2200 on every doubled line, so the ADC sample period
follows the line rate: across a 39% rise in line rate a constant time would have
to grow the count by the same 39%, from 175 to 243. It does not move. The two
readings that agreed on 0.88 us agreed because they are the same line rate, and
the constant-time reading -- which pointed at the source rather than the scaler
-- does not survive a mode at a different one.

**So the split is real, it is a count, and it still has no mechanism.** Field rate
is what it tracks on this bench, and field rate cannot be separated from vertical
total here: every doubled 50 Hz mode the definition carries is 312 lines, and both
doubled 60 Hz rasters are not. A branch on either is a fit to seven readings with
nothing behind it.

**Nothing on the chip differs across that split.** Read on both, every field is
identical: `PLLAD_MD` 2200, `PLLAD_KS` 2, `PLLAD_CKOS` 0, `PLLAD_ICP` 6,
`PLLAD_FS` 1, both decimators in, `IF_HBIN_SP`/`ST`, `IF_HS_DEC_FACTOR`,
`IF_PRGRSV_CNTRL`, both coasts 0, `SP_DLT_REG` 0, and `HLOW_LEN` 156 against
157. The two field rates do solve different output rasters, 1916 against 1594,
but the instrument cannot see the output at all.

A field-rate branch in a counter origin is a fit and not a fact, so there is one
value: **160**, which takes every mode within 23 samples where 272 was out by
127. The best a single value can do is 158, at 21 — the readings run 137 to 179
— and two samples do not buy a change.

## The sync type moves it further than the field rate does

One reading, on `vga` at X320 Y256 C256 F50 with nothing else changed -- same
divider, same output raster, same capturable window, the source's own horizontal
registers untouched because `SYNC` alters only which pin carries sync:

| sync type | `HLOW_LEN` | `VTOTAL` | the card's left frame | wants |
|---|---|---|---|---|
| separate, `SP_SOG_MODE` 0, coast 0/0 | 156 | 311 | 106.9 px | 147 |
| composite, `SP_SOG_MODE` 1, coast 7/6 | 154 | 308 | 85.5 px | 55 |

**92 samples, against the field rate's 32.** The source's content sits 21.4 source
pixels earlier in the capture on composite sync, so at the shipped 160 the picture
is taken 24.5 pixels early there -- about 8% of a 320-pixel line. The separate-sync
row reproduces the 147 read on a different day, so the instrument is repeatable
across sessions.

It is the scaler's, not the source's: VIDC20's horizontal timing registers do not
change with `SYNC`, so what moves is where the sync processor puts the retimed
hsync the FIFO resets against. **The sync duty is not the explanation** -- 154
against 156 is within noise, and the 222 that would have made a story of it was
read off a stranded engine rather than a healthy one.

**This is ONE reading and no constant should move on it.** Its vertical companion
failed outright, and a second sync type multiplying the number of origin values
wants replication on another source before anything is keyed on it.

**The vertical origin is not measurable on composite sync with this instrument.**
The feature lands 2.8 lines into the capture, so the creep has no room for its
run-up: started at 3 against an expected crossing of 28, it reported 5.5, and a
crossing 22 units from where the observation put it is a failed reading rather
than a small one.

**Round-tripping the sync type strands the engine, and `/sc?~` is the recovery.**
After several `SYNC` changes the engine sat at `state: absent` with the sync
processor counting 311 perfectly beside it, `IF_VB_SP` collapsed to 2..0 so the
vertical window could never fire, and the framing untuned. Neither clearing the
framing override nor waiting out `HeldRateRejectionLimit` released it;
`/sc?~` restored the framing, the count and the picture in one pass.

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
| X640 Y352 F60 | 3 | 12 | 7.1 | 4.9 lines in |
| X896 Y352 F60 | 3 | 12 | 7.0 | 5.0 lines in |

Four sources agree, two of them at a vertical total of 364 against the others'
312 and 262, and the first two have pulses differing by a factor of two. **Comparing against
the pulse's trailing edge instead manufactures a disagreement**, because the
pulse is then inside the comparison.

`SourceTiming::VerticalOriginLines` states 7 for this, measured on undoubled
published rasters, and it is applied inside `SourceTiming::activeStart()` —
which returns zero for a source matching no published raster, so it reaches
none of the modes above.
