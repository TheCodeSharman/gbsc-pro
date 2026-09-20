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

## The exposed-memory artefact is real, and it is a different object

`produced` is the output pixels the write covers -- `capture x 1024 /
VDS_HSCALE`, where `capture` is `IF_HB_ST2 - IF_HB_SP2`. Where it falls short of
the display window, the tail of that window is not written and shows what was
last there. **That artefact exists and is reproducible**, and telling it from the
bar is the point of this page.

Shrinking `IF_HB_ST2` ALONE, with automation frozen so nothing re-scales, is what
produces it. Column profile over rows 100..620, capture start held at 129:

| `IF_HB_ST2` | cols 1140..1215 | cols 1225..1240 |
|---|---|---|
| 1083 (default) | 10 | 52 58 58 44 |
| 1063 | 10 | 52 58 59 44 |
| 1043 | 10 | 52 58 58 44 |
| 1003 | **25** | 58 63 63 48 |

A twenty and a forty unit shortening change NOTHING, because the source is static:
what a shortened write leaves behind is identical to what it would have written.
At eighty units the band appears, and its left edge is where `produced` now ends
-- `110 + (1003 - 129) x 1024 / 546 = 1749` output px, which is photo column
1138 against 1135 measured.

**It looks nothing like the bar.** Wide, flat, dim, and multicoloured comb where
the bar is narrow, bright and blue.

**AND IT IS A BAND, NOT A STRIP.** A write that stops short on every line ought
to leave a full-height column, and it does not. Row profile over columns
1140..1215, on two runs shortened by the same eighty units:

| rows | shrunk from capture start 129 | shrunk from capture start 113 |
|---|---|---|
| 60..220 | 38 73 89 58 75 79 60 27 | 11 |
| 340..470 | 11 | 21 64 89 77 63 30 78 |

Same capture width of 874 units, same `produced`, same windows, and the band
sits in a different place. So its vertical position is not set by the geometry.

What is unwritten is the whole column; what is VISIBLE is wherever the stale
bytes happen to be bright. On most lines the previous content past `produced` was
the black beside the picture and reads 10, which looks like nothing at all. Move
the capture start and a different slice of the old frame occupies those
addresses. **Judging an exposed tail by where it shows is therefore wrong** -- it
is there on every line, and the band is a property of what was there before.

**And the bar in that frame is stale**, not written: it is the bar from before the
shrink, frozen in the region the write no longer reaches. Panning it away first
and then shrinking by the same eighty units gives the same band with no bar in
it:

| cols | bar in shot, then shrunk | panned away | panned away, then shrunk |
|---|---|---|---|
| 1140..1215 | 25 | 10 | 24 |
| 1225..1240 | **58 63 63 48** | 8.7 8.2 7.8 9.5 | **22 21 21 20** |

Same exposure, same windows, same `produced`. The only difference is which source
units the capture took, which is what makes the bar written content rather than a
property of the window's end.

**A PAN CANNOT EXPOSE ANY OF THIS.** Both edges move together, so `capture` and
`produced` are unchanged and the write covers exactly the same output pixels.
That is why the bar moving under a pan settles what it is.

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

`IF_HBIN_ST` removes it too, from the input side and without moving the picture,
and it is the field in that block with no per-solve owner --
[the-hbin-start-blanks-the-captured-tail.md](the-hbin-start-blanks-the-captured-tail.md)
has the sweep, the registers that do not reach it, and the pair of snapshots
that are byte-identical with the bar present and absent.

## It is TWO bands, and how much of the second you get is the framing

The tail is not one level. Column profile over rows 100..620 at full zoom-out,
capture 0..1007 of 1007:

| columns | rgb | mean |
|---|---|---|
| 1166..1190 | (76, 79, 26) | 60 -- yellow |
| 1192..1210 | (109, 120, 140) | 120 -- grey-blue, twice as bright |

Adjacent, sharply divided, and different colours. Both are captured: a pan moves
the capture window and nothing else -- `IF_HB_SP2` 105 -> 97 -> 95 with
`VDS_HSCALE` and `VDS_DIS_HB_ST` unmoved -- and the yellow band marches and is
eaten as the window's right edge walks toward it, 31 columns to 12 to 10 to 9.

**The second band appears only when the capture reaches far enough.** At the
default framing and at the pan framings above it is two or three columns; at full
zoom-out it is 31. So a session that meets the artefact at one framing sees one
bar, and the colour it reports is whichever level its window happened to stop in
-- which is the other half of why the colour "changes between modes" below.

**Do not read the two as bar-plus-exposed-memory.** `produced` was 1790 against a
display window of 1788 at the framing that shows both, so nothing was left
unwritten and neither band is stale content.

## Why the colour is not the picture's

It changes between modes -- green and grey at one framing, blue here -- which
reads as stale memory and is not. Past the picture the window is on the source's
front porch and the approach to the next sync pulse, and what the ADC digitises
there follows the clamp and exactly where the stop lands, both of which move with
the mode and the divider. Nothing there is picture, so nothing there follows the
picture.

## The picture also pans between acquisitions, and that is a separate thing

The acquisition carrying the bar usually carries the picture up to 73 output
pixels to the left as well, at the same registers. The two are not one
displacement: the pan is after the horizontal scaler and the capture places
content in identical units either way, while the bar is captured content.
[the-pan-is-downstream-of-the-scaler.md](the-pan-is-downstream-of-the-scaler.md)

The bar's strength varies independently of the pan -- 13 columns at peak 25 and
51 at peak 116, at one pan of -42 -- so the account below describes where the
window's tail falls, and the clamp decides what that tail digitises to.

## Where the fix has to go

Not `VideoSourceLine::lastCapture()`, which bounds the line at `units - 2` and
the capture write limit: on this source that is 1102, twenty units past where the
window actually stops, so it is not what places the default.

**That holds for the DEFAULT framing and not for a forced 100% one.** Where
`/framing/full` is on, the framing asks for the whole capturable span and
`lastCapture()` is exactly what places the stop -- 1099 on a 1101-unit line,
forty-one units into the blanking that precedes the next pulse.
[a-flip-test-cannot-tell-a-captured-tail-from-stale-memory.md](a-flip-test-cannot-tell-a-captured-tail-from-stale-memory.md)
has that measurement, and the three tests that separate this bar from an exposed
memory window.

What places it at the default framing is `AxisHorizontal`'s active window, and
**nothing on the chip can measure the right value** -- a border is black active video, electrically
identical to back porch. For a source running a raster the standards state,
`SourceTiming` supplies it exactly and the assumption is never used. For one that
does not, 0.864 is a guess, this source wants 0.852, and the remedy the engine
already offers is the zoom.

**One source is not a mandate to move a global default.** What this page settles
is where the artefact comes from and how to tell it from the three things it is
routinely mistaken for.
