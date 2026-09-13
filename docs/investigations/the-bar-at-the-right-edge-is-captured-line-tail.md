# The bar at the right edge is the tail of the captured line

A coloured vertical bar stands at the right-hand end of the picture on the bench
RiscPC at 320x256@50. It is captured input at a fixed position in the SOURCE
line, arriving because the default capture window assumes more active video than
the source sends. It is not the playback stage running off the end of the memory
window, and it is not the encoder.

## What it is

The horizontal axis assumes active video starts at 0.117 of the line and runs
for 0.864 of it -- `AxisHorizontal`, `docs/vesa-gtf.md`. On a 1104-unit line that
places the untuned capture window at 129..1083, which is exactly where the bench
lands it.

Measured, the source's picture stops around unit **1070**, not 1083. The window
therefore takes about thirteen units of whatever lies past the picture, and those
thirteen units magnify to roughly thirty output pixels of bar:

| | fraction of the line |
|---|---|
| assumed active extent | 0.864 |
| measured on this source | **0.852** |

## What says so

**A pan press moves the capture window and NOTHING else.** Measured over all
1536 registers either side of one press:

```
IF_HB_SP2    129 -> 127
IF_HB_ST2   1083 -> 1081
```

Segment 3 is byte-identical -- display window, memory window, `VDS_HSCALE` and
the raster all unmoved. So anything that moves when the capture window moves is
in the captured line.

**It marches and it fades.** Creeping the window left two units a press, with the
bar's position and brightness read off the photograph as a column profile over
rows 100..620:

| capture stop | bar left column | peak column | peak value |
|---|---|---|---|
| 1083 | 1221 | 1233 | 59 |
| 1081 | 1224 | 1234 | 58 |
| 1079 | 1227 | 1236 | 56 |
| 1077 | 1230 | 1236 | 53 |
| 1075 | 1232 | 1237 | 47 |
| 1073 | 1235 | 1238 | 36 |
| 1071 | 1238 | 1240 | 21 |
| 1069 | -- | -- | gone |

The background beside it reads 10 and an unrelated bezel feature reads 21, which
is the floor the last row sits on.

The left edge advances **2.8 photo columns a press**. A fixed feature in the
source predicts 2 units x 1.875 output px/unit x 0.68 photo col/output px =
**2.55**. The brightness decays monotonically because the window's right edge
eats the bar while its left edge walks toward that edge: it is clipped away, not
switched off.

Photo columns map to output pixels at 0.68 by differencing two frames at
`VDS_DIS_HB_ST` 1899 and 1859, where the difference IS the forty pixels the
register blanked.

## What it is not

**Not the playback stage running off the memory window.** Both windows end on
the same pixel, which makes that the obvious reading, and it is wrong three ways:

- giving the memory window fifteen pixels of margin at the far end
  (`VDS_HB_ST` 1899 -> 1914, display window left at 1899) leaves the bar
  untouched -- the column profile peaks at 58.7 against the baseline's 59.0
- the write already fills the window: `produced` is 954 x 1024 / 546 = 1789.19
  against a display window of 1899 - 110 = 1789, and the write origin
  `VDS_HB_SP + 55 + 25 x magnification` = 8 + 55 + 46.9 = 110
- an unwritten tail cannot move, and this moves

**Not the encoder.** The encoder decides where the picture sits on the panel and
nothing the scaler writes moves it, so an encoder artefact is fixed to the panel.
This one is fixed to the source.

**Blanking it is not fixing it.** `VDS_DIS_HB_ST` 1899 -> 1859 removes it,
because forty pixels of blanking covers thirty pixels of bar. The bar is still
captured; it is just not shown.

## Why the colour is not the picture's

It changes between modes -- green and grey at one framing, blue here -- which
reads as stale memory and is not. Past the picture the window is on the source's
front porch and the approach to the next sync pulse, and what the ADC digitises
there follows the clamp and exactly where the stop lands, both of which move with
the mode and the divider. Nothing there is picture, so nothing there follows the
picture.

## Where the fix has to go

Not `VideoSourceLine::lastCapture()`, which bounds the line at `units - 2` and
the capture write limit: on this source that is 1102, twenty units past where the
window actually stops, so it is not what places the default.

What places it is `AxisHorizontal`'s active window, and **nothing on the chip can
measure the right value** -- a border is black active video, electrically
identical to back porch. For a source running a raster the standards state,
`SourceTiming` supplies it exactly and the assumption is never used. For one that
does not, 0.864 is a guess, this source wants 0.852, and the remedy the engine
already offers is the zoom.

**One source is not a mandate to move a global default.** What this page settles
is where the artefact comes from and how to tell it from the three things it is
routinely mistaken for.
