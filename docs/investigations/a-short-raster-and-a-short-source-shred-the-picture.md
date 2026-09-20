# 480p and 576p shred a 311-line source, and neither half of that is sufficient

The RiscPC at 320x256@50 into 480p or 576p: the card torn into vertical bands,
wrapped sideways about a seam, alternate-line combing. The same source at 1080p
is clean. This records what the fault is NOT, because the eliminations cost more
than the symptom and each one is a probe somebody would otherwise repeat.

## The scaling path is in circuit

`DAC_RGBS_BYPS2DAC` 0, `DAC_RGBS_ADC2DAC` 0, `OUT_SYNC_SEL` 0, and a solved
raster of 2070 x 625. The unit's own OSD says **Bypass** throughout, which is a
separate reporting defect and not the video path -- `presetIdFor()` gives 576p a
code neither display switch knows. `../known-issues.md`.

## The two candidates are confounded by the bench, and separating them is the result

480p and 576p are the only output resolutions that turn the line doubler off, so
"doubler off" and "short output raster" change together. A 720x576@50 source --
624 lines, which un-doubles at every output -- separates them:

| source | output | doubler | capture | result |
|---|---|---|---|---|
| 311 lines | 1080p | on | 954 x 582 | clean |
| 624 lines | 1080p | off | 914 x 576 | clean |
| 624 lines | 576p | off | 914 x 576 | coherent, right edge clipped |
| 311 lines | 576p | off | 1020 x 291 | shredded |
| 311 lines | 480p | off | 954 x 291 | shredded, indistinguishably |

So the doubler being off is not sufficient, the short raster is not sufficient,
and the fault needs both. The captured LINE COUNT is what is extreme only in the
failing rows: 291 against 576 to 582 everywhere else.

## It is not the encoder

A 2072 x 625 @ 50 Hz raster carries a coherent picture and a 2069 x 625 @ 50 Hz
one is shredded. One output timing, one sink decision, two outcomes, and only
the source differs -- so nothing the encoder does can be the cause.

480p failing identically closes the other version of the same attribution: 525
lines is nothing like the 800x600 the television reports for 576p, so whatever
the sink calls the mode is not what selects the fault.

## What the television reports is not a pixel budget

The output raster is a timing. 576p names a line count and a field rate; the
horizontal total is whatever the display clock affords, and the encoder resamples
the analog line into whatever mode it picks. A sink reporting 800x600 therefore
says nothing about how many pixels the VDS emitted.

The reading that the picture overflows the line does not survive the registers
either. `VDS_HSCALE` is ten bits with 1024 as unity, so `produced` can never be
less than the capture -- but it does not need to be: the display window is
1941 px against a produced 1941 px, matched to the pixel, with a capture of 1020
units. The window is wider than the capture, not narrower.

## Probes that changed nothing, with the fault standing

Each against an unchanged control frame, automation frozen so no solve rewrote
the state.

| probe | value | outcome |
|---|---|---|
| `PB_CAP_OFFSET` | 592 | indistinguishable |
| `PB_CAP_OFFSET` | 148 | the documented fetch overlap, so the stride is live |
| `PB_FETCH_NUM` | 510 | indistinguishable |
| `IF_LINE_SP` | 590 | indistinguishable |
| `IF_HB_ST2` | 559 from 1179 | indistinguishable |
| `CAP_REQ_FREEZ` | 1 | stable and identically corrupt |
| `/sampleclock` | md 2250 | barely moves it |
| `IF_HSYNC_RST` | 590 | destroys the picture |

`CAP_REQ_FREEZ` holding the frame still rules out a capture/playback race.
`IF_HSYNC_RST` destroying it says the IF counts the 1180 it is set to, so the
line counter is not secretly halved.

## The two artefacts separate on the scale registers

`VDS_VSCALE` at unity removes the combing entirely and leaves the horizontal
fault untouched -- so the combing is the vertical interpolation working on a
buffer of 291 lines, and the horizontal fault is upstream of the vertical
scaler.

`VDS_HSCALE` at unity leaves the memory line's content stretched with its tail
unwritten, so whatever is wrong is already in the buffer or in how the line is
read out of it.

## What has not been done

Every width quoted against this fault was estimated by eye off a photograph.
That is the method that produced `CORNER_H`, `ORIGIN_OFFSET_H` and
`PANEL_VISIBLE_LEFT`, every one of which was a write start wearing another name.
Calibrate before quoting another: difference two frames taken at different
`VDS_DIS_?B_ST` so the difference IS the strip the register blanked, and
re-calibrate after any output excursion. `../scaler-geometry-model.md`.

## The full register diff holds no accident

A 1536-register snapshot at both ends of the nearest-identical pair -- 576p
output, 624-line source against 311-line source -- differs in 21 fields, and
every one is a consequence of the source rather than a value set wrongly:

```
IF_HB_SP2      167 -> 159     IF_HB_ST2     1081 -> 1179
IF_HSYNC_RST  1096 -> 1180    IF_LINE_SP    1161 -> 1245
IF_VB_SP        44 -> 19      IF_VB_ST       620 -> 310
PLLAD_MD      1096 -> 1180    PLLAD_KS         2 -> 3
SP_RT_HS_SP   1019 -> 1097    PB_CAP_OFFSET  275 -> 296
PB_FETCH_NUM   229 -> 255     VDS_HSCALE     483 -> 538
VDS_VSCALE     953 -> 483     VDS_HSYNC_RST 2072 -> 2069
```

plus the display window and clamp positions that follow them. The divider
changes because the line rate does; the IF line, the retime stop, the stride and
the fetch all follow the divider and the capture; the scales follow the capture
against an almost identical raster.

**So the fault is not a register holding a wrong value.** Both states are
internally consistent, and the difference between them is exactly what the two
sources imply. That is what makes a register dump unable to see this, and it is
why the remaining work is a calibrated measurement of the picture rather than
another diff.
