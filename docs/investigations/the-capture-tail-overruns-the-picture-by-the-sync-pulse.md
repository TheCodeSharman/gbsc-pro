# The captured blanking is the MDF's porches, and the tail carries a sync pulse

At 640x480@60 on `vga` the picture has a narrow black band down the left and a
band two and a half times wider down the right. Both are captured source
blanking, both are what `RetroScaler-Acorn.mdf` states, and the asymmetry is
`lastCapture()` running into the NEXT line's sync interval.

## The mode

Four MDF rows are named 640 x 480 and three of them are wrong for this bench.
`HPERIOD_IF` 213 is `27e6 / (4 x lineRate) - 1` at 31.47 kHz, and
`STATUS_SYNC_PROC_VTOTAL` 524 against a 525-line frame picks the row out:

    pixel_rate 25175   h_timings 94,22,22,640,22,0   v_timings 2,32,0,480,0,11

25175 / 800 = 31.469 kHz. The 31500 kHz rows total 840 and 832 and give
37.5 and 37.9 kHz, which `HPERIOD_IF` would report as 179 and 177.

## The line, in three coordinate systems

`PLLAD_MD` 1494 on an undoubled line, `VDS_HSCALE` 974:

    1 source px = 1494/800 = 1.8675 IF units = 1.9634 output px

The capture window is `IF_HB_SP` 72 to `IF_HB_ST2` 1492, and
`VideoSourceLine::CaptureLagUnits` is 72, so the video at source position `s`
arrives at IF unit `s + 72`. The window therefore takes source units 0..1420,
which is source px 0..760.4 counted from the line's origin.

**The origin is the sync pulse's TRAILING edge.** The source is low-active,
normalising inverts it, and `firstCapture()` adds no pulse guard because
`syncAtHead` is false — so the window opens on the first pixel of the back
porch and the pulse is already behind it.

| region | source px | ends at px | ends at IF unit | in the window |
|---|---|---|---|---|
| back porch | 22 | 22 | 113 | yes |
| left border | 22 | 44 | 154 | yes |
| active | 640 | 684 | 1349 | yes |
| right border | 22 | 706 | 1391 | yes |
| front porch | 0 | 706 | 1391 | — |
| next line's sync | 94 | 800 | 1566 | **to 1492 only** |

**The borders are picture, not blanking.** RISC OS paints the screen border in
the border colour and PatLib sets it, so it is the coloured bar at each end of
the card. Whatever detects content finds the border, and blanking means the
porches and the pulse alone.

So the model predicts:

    left blanking  = the back porch                = 22.0 src px =  43.2 output px
    right blanking = 760.4 - 706, into the pulse   = 54.4 src px = 106.8 output px

## Measured

Photo columns are calibrated against output pixels by moving `VDS_DIS_HB_SP` and
`VDS_DIS_HB_ST` to known values and reading the sharp blanking edge off the
frame. Two independent pairs agree:

| pair | photo px per output px |
|---|---|
| `VDS_DIS_HB_SP` 400 / 700 | 1.0867 |
| `VDS_DIS_HB_ST` 1200 / 900 | 1.0850 |

The panel's painted area is photo columns 43..1558, taken from an 800x600
source in pass-through, where the picture overruns the panel and the
bezel-to-picture boundary is the panel's own edge.

The right band, read in two states whose pictures sat 55 output px apart:

| frame | content ends at photo | content ends at output | window ends at | right blanking |
|---|---|---|---|---|
| A | 1444 | 1475.4 | 1583 | 108 |
| B | 1503 | 1474.4 | 1583 | 109 |

Against 106.8 predicted, so the model closes to about one source pixel.

## The borders measured directly, by flipping them

ModeServ's `BORDER ON|OFF` paints the RISC OS screen border or leaves it black,
so a pair of frames differing only in that isolates the MDF's border fields from
the porches. The left band is off the panel at a default framing, so the second
pair is taken with the picture moved 200 output px right — `VDS_HB_SP` 208 and
`VDS_DIS_HB_SP` 289, which is a register write and does not make the encoder
look again, so the photo calibration still holds.

| edge | border on | border off | difference | source px | MDF |
|---|---|---|---|---|---|
| right, default framing | photo 1443.5 | photo 1397 | 46.5 photo px | **22.1** | 22 |
| left, moved 200 px right | photo 202.5 | photo 251 | 48.5 photo px | **23.0** | 22 |

Converted through the calibration and checked against the model, every boundary
lands within about a source pixel of where the arithmetic puts it:

| boundary | predicted output px | measured |
|---|---|---|
| picture starts, border included | 332.2 | 331.9 |
| picture starts, border black | 375.4 | 376.7 |
| picture ends, border black | 1431.9 | 1432.2 |
| picture ends, border included | 1475.2 | 1475.4 |

So the back porch is `331.9 - 289` = **42.9 output px = 20.9 source px** against
the MDF's 22, the borders are 22.1 and 23.0 against 22, and the right band from
the border's end to the window's stop is `1583 - 1475.4` = 107.6 output px =
**54.8 source px** against the 54.4 the next pulse gives.

**Nothing in the capture is unaccounted for.** The blanking matches the monitor
definition on both sides, and the asymmetry is the zero front porch plus a tail
that runs into the pulse.

## Three confirmations of the head

The head has three independent confirmations that content begins at the end of
the back porch and not at the end of the border:

- the table above puts the back porch's end at IF unit **113**;
- a capture-floor sweep put the first content at unit **110..115**, which is the
  range `test_video_source_line.cpp` records against the lag;
- the left band is at or before the panel's first painted column in every frame
  taken at a default framing, which bounds it under the 86 output px a blank
  border would add.

## What it says about `lastCapture()`

`lastCapture()` is `units - 2`, and the comment on it records that taking the
pulse off the tail costs picture. That was measured before the capture lag
translated the window, and it no longer holds: the pulse is `syncUnits` = 172
units and the lag is 72, so the tail overruns the last content by

    syncUnits - lagUnits = 100 units

against the 101.5 the table gives and the ~102 measured. A tail of **1390**
rather than 1492 stops exactly where the right border ends. Subtracting
`syncUnits` alone gives 1320, which is 70 units short of the content and is the
form that cost picture.

The engine does not need to know where the borders are to derive that. It knows
where the next pulse begins — `units - syncUnits` in source coordinates — and
the lag puts it at `units - syncUnits + lagUnits`, which is 1394 against the
1390 the MDF gives. The difference is the `ceil` on `syncUnits`.

So the tail is the mirror of the head, and the rule is one rule: **the window
excludes one sync pulse, at whichever end of the line it falls, and the lag
translates both ends.**

    lastCapture = syncAtHead ? units - 2
                             : min(units - 2, units - syncUnits + lagUnits)

The head needs no equivalent branch, because `firstCapture()` already adds
`syncUnits` where `syncAtHead` is true and the pulse is in front of the origin.

**The prediction that would refute it:** with the tail at 1390 rather than 1492,
the right-hand border bar and the castellations beside it must be exactly as
they are now. If any of the card's right edge goes with the black band, the
border does not end where the MDF says.

## The black it leaves is not only cosmetic

640x480@60 and 800x600@60 solve the same 1600 x 1126 raster at the same clock,
the same field rate and the same `PLLAD_MD` 1494. The 800x600 line is 1056
source px against 640x480's 800, so the same 1494-unit IF line covers less of
it and `lastCapture()` stops 54 source px INSIDE the active picture rather than
54 past the end of it — leaving **no black margin at the right of the display
window** where 640x480@60 has 108 output pixels of it.

Across eight source mode round trips each, 800x600@60's picture lands in the
same place every time and 640x480@60's lands in one of three.
[the-picture-position-is-re-rolled-by-the-sync-pad.md](the-picture-position-is-re-rolled-by-the-sync-pad.md)
carries that measurement; the right-hand margin is the only difference between
the two modes that sorts with it.

So the tail is a candidate cause of the horizontal instability and not only of
a black band, and the same one-line change tests both.

## Why this is not a fault in the source

640x480@60 and 800x600@60 both have a **front porch of zero** in this monitor
definition, so there is no right-hand blanking of their own to show, and
everything past the right border is the next line's sync interval. 320x256@50
has a 7.42% front porch and does show one. A framing rule judged on one of
these modes is judged on one porch layout.
