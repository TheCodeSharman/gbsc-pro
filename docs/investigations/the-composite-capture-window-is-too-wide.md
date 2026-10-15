# The composite capture window is too wide, and the margin is the source's own blanking

On `vga` at 800x600@60 into 1080p, the same source framed at the computed
default shows **214 black columns at the right on composite sync** and none on
separate sync. The black is not the board's blanking: the capture window runs
past the end of the source's picture and brings the source's own blanking in
with it.

## What is measured

Both read at 16:9 with the framing reset, output 1920x1080, same cable, same
mode, only `SYNC` moving.

| | separate | composite |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 627 | 623 |
| `PLLAD_MD` = `IF_HSYNC_RST` | 1342 | 1436 |
| `IF_HB_SP2` .. `IF_HB_ST2` | 274 .. 1293 | 174 .. 1417 |
| capture window width | 1019 | **1243** |
| the source's active width at that divider | 1016.7 | 1087.9 |
| over the picture | 2 samples | **155 samples** |
| card's green frame, emitted columns | 2..5 and 1916..1919 | 20..25 and 1700..1705 |
| `VDS_HSCALE` / `VDS_VSCALE` | 750 / 569 | 916 / 552 |
| `VDS_DIS_HB_SP` .. `ST` | 145 .. 1533 | 144 .. 1532 |

The source's active width is `PLLAD_MD x 800 / 1056`: DMT gives 800x600@60 an
active 800 of a 1056-pixel line. On separate the capture window is two samples
wider than the picture. On composite it is 155 wider, and 155 capture samples
at the 1.54x this leg magnifies by is about 240 emitted columns, against the
214 measured.

**The output registers are not the difference.** `VDS_DIS_HB_SP`/`ST` agree to a
unit between the two legs, so the display window is in the same place and the
same width. What differs is what is written into it.

## Whose black it is

`VDS_Y_OFST` lifts the picture's luma -- captured blanking with it -- while the
VDS blanking interval outside the display window keeps the blanking value. So
after a lift, black still at an edge is the board's and anything that rose is
the source's.

Lifted by 40 with `VDS_DYN_BYPS` 0, column means across the frame:

- **composite**: the last 30 columns, 1890..1919, rise by **31.5** each. They
  are well past the green frame at 1700..1705, so they carry captured source
  blanking inside the display window.
- **separate**: the same columns rise by **0 to 2**, except the few carrying
  the green frame itself. There is nothing there to lift.

So the composite margin is inside the picture the VDS writes, not outside it.
A fix that moves `VDS_DIS_HB_ST` would blank the symptom and lose picture with
it; what is wrong is upstream, in the capture.

## Why the window is too wide

`SourceTiming::matching()` keys on the measured frame, and its own header states
that `STATUS_SYNC_PROC_VTOTAL` counts from zero, so the frame is the count plus
one:

- separate counts 627, so the frame is 628, which is DMT's total for
  800x600@60. The raster matches, `activePixels()` states 800, and the capture
  window is derived from it -- two samples out.
- composite counts 623, so the frame is 624, which is no published raster at
  all. `SourceTiming` states zero active pixels where nothing matched, and the
  capture falls back to the computed default, which is wider than this source's
  picture.

**The count is the whole input to this.** Everything downstream -- the line
rate, the divider, the capture, both scales, the framing -- follows from it, and
the two sync types differ in nothing else. `known-issues.md`, "One source on one
cable emits two different pictures".

## What to try next, in order

1. **Find the four lines.** The count is four short, not one, and nothing yet
   explains where they go. They are not the coast: the count reads 623 at every
   `SP_PRE_COAST` from 2 to 18 and every `SP_POST_COAST` from 2 to 20, with the
   window placed. `SP_DLT_REG` reads 192 on this path and 0 on the other, and
   has not been walked.
2. **Ask whether the count is the composite path's or the engine's.**
   `/samplinglog?rates=120` times the field rate off `DEBUG_IN_PIN` with
   automation frozen, so a count taken there is free of anything the solve
   writes. Compare the two sync types in ONE window.
3. **Ask what the fallback capture is.** If the raster match is the mechanism,
   a source whose count is right but which matches no raster should show the
   same margin -- the RISC PC has modes with no published equivalent, and
   `published_rasters.py` says which.
4. **Do not reach for a tolerance on the raster match first.** Widening the
   match so 624 finds 628 hides a count that is wrong by four lines, and the
   line rate derived from it is wrong by 0.65% whatever raster it then picks.

## Not a defect

**The source's left column going off the frame on separate sync is accepted.**
It is one column at one edge and it does not always happen: the same leg
measured at 2 columns of margin with both green edges on screen in one
acquisition and with the left column lost in another.

**The Wii's bottom rows are in the signal.** The 51 black rows at the bottom of
a 480p pass-through frame are the source's, not a framing deficit.
