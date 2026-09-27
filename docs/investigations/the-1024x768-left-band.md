# The left band at 1024x768@60: what it is not

640x480@60 frames flush at every edge from the engine's own solve. 1024x768@60
carries about 37 emitted columns of black at the left, with the card's green
border -- which marks the source's outermost pixel -- starting there rather than
at column 0. 800x600@60 sits between them, flush at the near edge and 6 px short
at the far one.

This page is the elimination list, because the band survived every candidate that
looked obvious and the cost of re-testing them is a bench session each.

## The state it is measured at

A settled solve with automation LIVE, `VTOTAL` 806 and 48.47 kHz confirming the
mode, the card redrawn after the mode change:

```
PLLAD_MD 1440   IF_HSYNC_RST 1440   capture 316 .. 1416
VDS_HSCALE 808  VDS_DIS_HB_SP 142   VDS_HB_SP 55
PB_CAP_OFFSET 361   PB_FETCH_NUM 275
emitted: columns 2..36 black, green border at 37, far edge at 1916
```

The band is 37 emitted px, which is 21.7 capture units and about 20 source
pixels. **A one-column bright artefact sits at the very left edge, ahead of the
black**, so a first-lit-column test reports no band at all -- measure the dark
RUN, and skip the first few columns.

## What it is not

- **The MDF.** `1024x768 @ 65.000 MHz, h_timings 136,160,0,1024,0,24,
  v_timings 6,29,0,768,0,3` is DMT 16 exactly, with both borders 0, so active
  video starts at 296 of 1344 = 0.2202.
- **The wrong mode being selected.** Four 1024x768 entries exist, at 60.00,
  70.07, 75.03 and 85.00 Hz, and nothing else in the file has `x_res` 1024. The
  measured `VTOTAL` 806 and 48.47 kHz match the 60 Hz entry's 806 and 48.36.
- **A stored framing.** `/geometry` reports `oh` 317 and `eh` 1098 against a
  default of `0.2202 x 1441 = 317.3` and `0.7619 x 1441 = 1098`, so the framing
  is the computed default to a unit.
- **The source's sync.** Measured `HLOW_LEN / HTOTAL` against each mode's own
  standard is within 4 px on all four modes tested and **random in sign** --
  640x480 -2.4, 800x600 +1.2, 1024x768 -4.4, 1280x1024 +3.7 -- including the two
  that frame correctly. That is the counter's quantisation, not a source
  property, and VIDC20 is programmed from the MDF.
- **The clamp and the coast.** `SP_CS_CLP_ST` 15 and `SP_CS_CLP_SP` 19 are
  identical on the working and failing modes. `SP_H_CST_SP` differs and is inert,
  `SP_NO_COAST_REG` being 1 on both.
- **The oversampling chain.** `PLLAD_CKOS` 0, `ADC_CLK_ICLK1X` 1,
  `ADC_CLK_ICLK2X` 0, `DEC1_BYPS` 1, `DEC2_BYPS` 0 -- byte-identical across
  640x480@60, 800x600@60 and 1024x768@60.
- **Sync polarity.** 640x480@60 and 1024x768@60 are both `sync_pol` 3; 800x600@60
  is 0. One of each polarity frames correctly.
- **The magnification, and anything proportional to it.** The three modes
  magnify x1.208, x1.264 and x1.267, so the write origin's `55 + 25m` term lands
  at 85.2, 86.6 and 86.7 -- it cannot produce a band of 0, 0 and 21.7 units.
- **A stray register.** A full 1536-register diff between the working 640x480@60
  and the failing 1024x768@60 resolves to **23 fields, every one legitimately
  mode-derived**: the divider, both capture windows, both scales, the output
  windows, the stride and fetch, the PLL loop-filter range and the sample phase.
  Nothing is set differently that the mode does not explain.

## Also not the stride

`PB_CAP_OFFSET` is the SDRAM ROW STRIDE -- how many bytes are skipped per row --
and capture and playback share it, so they are addressing the same layout by
construction. It decides where the left edge lives IN MEMORY and not where it
appears on screen, which is `VDS_HB_SP` and the aperture. That is why enlarging it
changes nothing about the framing: moving it relocates the layout, so different
leftovers land under a window that is looking at unwritten memory, and that is the
only thing it appears to do to an edge.

## What is left: the capture opens 16 source pixels before active video

Measured rather than extrapolated. Walking `IF_HB_SP2` up with automation frozen
and NO mode change, so nothing can re-solve underneath it:

| `IF_HB_SP2` | near | far |
|---|---|---|
| 316 (solved) | 37 | 1918 |
| 320 | 30 | 1918 |
| 324 | 23 | 1918 |
| 328 | 16 | 1918 |
| 332 | 9 | 1918 |
| 336 | 7 | 1918 |
| 340..348 | 7 | 1918 |

**The far edge never moves**, so the band is blanking being trimmed and the
picture is not displaced. The near edge closes at 1.75 emitted px per capture
unit and reaches a floor of 7 px at about `IF_HB_SP2` 334 -- and 7 px is the floor
640x480@60 and 800x600@60 show too, so it is the baseline and not this fault.

So active video starts at IF unit **~334** and the engine captures at **316**:
**17.5 samples early, about 16 source pixels.** The source starts at line
fraction 0.2314 against the 0.2202 the published raster states.

**An arithmetic error in `(sync + back porch) / total` would be wrong on every
mode**, and 640x480@60 and 800x600@60 are right to a pixel. One mode out by a
fixed 16 pixels reads as a wrong INPUT rather than wrong arithmetic. The next
measurement is the same walk on 1280x1024@60, whose published active start is
0.2133: two modes off gives a pattern where one gives only a number.

## Measurement discipline this cost

- **Freezing automation and then changing the source mode leaves the engine
  solved for the previous mode.** The registers then describe one raster and the
  source emits another, and every reading is void. It produced a near edge of 7
  on what was actually 1280x1024. Change modes with automation LIVE, and confirm
  the mode from `cv` and the line rate before believing a number.
- **A re-lock by source mode round trip lets the engine re-solve.** A hand-set
  register is restored underneath the clip, so a walk reads the same state at
  every step and looks like a control that does nothing. Read the field back
  AFTER the re-lock, not just after the write.
- **Walk BOTH edges.** A near band that shrinks while the far edge holds still is
  captured blanking being trimmed; both edges moving is the whole picture
  displaced. The two want opposite fixes and the near edge alone cannot tell them
  apart.
