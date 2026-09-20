# The display window closes past the picture, at the far end

`Axis::solve` puts the far edge of both output windows at `corner + produced`.
Measured on the bench, the pipeline stops delivering before that, so the last
part of every line falls inside the display window with no picture under it and
shows as a band of saturated green.

Reachable at the default framing, with no pan and no zoom: the zoom-out bound at
1080p puts it hard against the right of the raster.

The band is `RGB(0, G, 0)` with red and blue reading exactly zero through a
camera — the `Y=U=V=0` decode that [tail-green.md](tail-green.md) sets out. The
colour is the same; the cause is not, because that one is a position in the
INPUT line and this one moves with the OUTPUT window.

## The measurement

Bench RiscPC 320x256@50, output 1080p, raster 1916 x 1126 at 107.8 MHz.
`PLLAD_MD` 2250, so the IF line is 1126 units and the capture is 80..1124 =
1044 units. `VDS_HB_SP` 8, `VDS_DIS_HB_SP` 106.

The register value at which the band first appears IS the picture's far edge:
below it the window closes over written picture and nothing green is exposed,
above it the window opens past the last delivered pixel. No mapping from photo
columns to output pixels is needed, which is what makes this the reading to
take.

| `VDS_HSCALE` | magnification | band absent | band appears | picture ends | `corner + produced` | overhang | overhang / magnification |
|---|---|---|---|---|---|---|---|
| 900 | 1.1378 | 1252 | 1256 | ~1254 | 1279.3 | 25 px | 22.2 |
| 750 | 1.3653 | 1492 | 1496 | ~1494 | 1522.5 | 28 px | 20.8 |
| 596 | 1.7181 | 1888 | 1892 | ~1890 | 1899.7 | 10 px | 5.8 |

**The overhang is real at every magnification and fits no constant.** Neither a
fixed count of output pixels nor a fixed count of input units describes all
three. The first two rows agree closely in input units and a coefficient fitted
to them alone would look convincing; the third refutes it. Two points cannot
disconfirm a line, and fitting one here is the error that put `Axis::margin` and
`Axis::nearMargin` into the code.

**The 1.718 row is the weakest reading.** At that magnification the picture ends
close to `farBound` at 1914, so the band can never exceed about 24 px, and the
detections either side of the threshold are near the camera's noise floor. It is
not established whether its departure is a property of the pipeline or of the
measurement.

## The write begins at a fixed position in the input line

The capture window's START is not honoured below about unit **252** of the IF
line. The engine sizes both output windows for `IF_HB_ST2 - IF_HB_SP2` and the
pipeline delivers `IF_HB_ST2 - 252`, so everything the window was opened for
below 252 is window with nothing under it.

Eight engine-solved framings, bench RiscPC 320x256@50, raster 1920 x 1126, IF
line 1253 units, `VDS_HB_SP` 8. Each step is one `/sc?I=60` press, so the scale,
the capture, the fetch and the stride are all the engine's:

| `IF_HB_SP2` | `IF_HB_ST2` | band | delivered units | `ST2 -` delivered | `252 - SP2` |
|---|---|---|---|---|---|
| 147 | 1230 | 166.1 | 978.3 | 251.7 | 105 |
| 165 | 1212 | 141.7 | 959.6 | 252.4 | 87 |
| 182 | 1195 | 117.4 | 942.3 | 252.7 | 70 |
| 199 | 1178 | 88.6 | 925.9 | 252.1 | 53 |
| 215 | 1162 | 59.8 | 910.8 | 251.2 | 37 |
| 231 | 1146 | 31.0 | 894.2 | 251.8 | 21 |
| 246 | 1131 | none | -- | -- | 6 |
| 261 | 1116 | none | -- | -- | 0 |

`ST2 -` delivered holds at **251.2 to 252.7 over six states**, and the lost input
units are `252 - IF_HB_SP2` in every row. **The band closes where that reaches
zero**, predicted from the first five rows and confirmed on the last three: it
is gone by `IF_HB_SP2` 246, where the remaining 6 units are 12 output px and
under the camera's floor.

`AxisHorizontal` opens the capture at 0.117 of the line, which is 147 units
here. The pipeline delivers from 0.201 of it. **The whole of the band is that
disagreement** -- there is no loss term at the far end and no misplaced origin.

## What it is NOT the input formatter's blank-for-scale-down

`IF_HBIN_SP` holds 272 on this source, which is close enough to 252 to look like
the answer, and it is not. Swept 150, 272 and 400 against a live band, engine
frozen: the picture **pans and resizes** every time -- the card's edges run
468.8 / 573.9 / 378.0 -- and the band does not move by a pixel, at 1408..1552 in
all three. A write gate would have closed the band at 150 and more than doubled
it at 400.

## What 252 is, is open

One source and one mode. Nothing says whether it is an absolute count of input
units or the 0.201 of a line it happens to be here, and the two differ by 128
units at the next mode up.

**800x600 cannot settle it.** There the capture is 340..1899 of an undoubled
1900-unit line at `VDS_HSCALE` 1023, so `produced` is 1560.6 against a display
window of 1496: the window is over-filled rather than under-filled, and no zoom
out is available to reverse that because the scale is already at its ceiling.
The band is a zoom-OUT artefact and that mode has no zoom-out left.

**`activeStart` is not the lever.** `VideoProcessorTimings` passes 0 for it on
both axes, but `Axis::placePicture` uses it as an output-side floor on `corner`.
What is wrong is an input-side start, and no value of `activeStart` reaches it.

## What it is not

| refuted | how |
|---|---|
| **captured content near the end of the line** | `IF_HB_ST2` swept 1124 -> 1088, 36 units, with the engine frozen and `VDS_HSCALE` confirmed unmoved. The band does not shift or change width |
| **stale memory the playback re-reads** | at `VDS_HSCALE` 480 the write covers the region with card content; returning to 596 brings the identical green back, matching the earlier profile to within camera noise. What is in that memory from the previous framing does not survive into the band, so it is regenerated every frame |
| **the capture path's write limit** | that limit is a position in the input line, so narrowing the capture past it removes it. This band is indifferent to the capture stop and moves with `VDS_DIS_HB_ST` |
| **the VDS line filter's tail green** | `VDS_D_RAM_BYPS` reads 1, so the filter is out of circuit while the band stands. [the-tail-green-is-the-vds-line-filter.md](the-tail-green-is-the-vds-line-filter.md) |
| **the playback fetch running out** | `PB_FETCH_NUM` 271 -> 330, a 22% rise over the `ceil(capture / 4)` the engine writes. The band does not move by a pixel, and neither does the picture |
| **captured content, from the input side** | `IF_HBIN_ST` 300 blanks the whole captured picture to black and the band remains, brighter. `PATTERN CARD` against `PATTERN PM5544` gives the same band to 2 px. `IF_HBIN_SP` 150 / 272 / 400 pans and resizes the picture and leaves the band exactly where it is |
| **the encoder or the panel** | `VDS_DIS_HB_ST` 1903 -> 1500 turns the band black -- G 72 against a black control of 26. Display blanking reaches nothing downstream of the scaler |

## Whether there are two artefacts here is open

A band of **multicoloured horizontal streaks** appears in place of the flat green
when the display window is opened far past the write — measured with the window
73 px beyond `corner + produced` at a capture of 840 units and `VDS_HSCALE` 900,
a combination the engine does not solve. Coloured bars at the far end are also
reported from normal use.

So the reading that fits both is that the exposed region shows two different
things by depth: zeros close to the write, and memory contents further out. That
would make the stale-memory refutation above true only for the shallow case,
which is the one the engine actually produces. **Nothing has separated them by
depth deliberately**, and doing so is the next measurement: hold the framing,
open `VDS_DIS_HB_ST` in steps well past `corner + produced`, and record where
flat green gives way to structure.

## The code says two different things about the same pipeline

`Axis::fitToRaster` charges `startPerMag_`, 25 input units, against the room the
picture has to fill:

```cpp
float produced = room * capture / (capture + startPerMag_);
```

`Axis::solve` charges nothing, and places both windows on the result:

```cpp
solved.produced_ = scale.produced(capture);   // capture * Unity / scale
```

One of these is wrong about how much of the capture reaches the output. The
measured overhang is the size of that disagreement, and its two clean rows land
near the 25 units `fitToRaster` assumes. Resolving it does not need hardware —
the arithmetic is host-testable.

## Photographing this band

- **The exposure meters on the crop, so the crop decides whether the band is
  visible at all.** Metered on a bright test card the band reads a green of 22
  against a 255 scale, which reads as a reflection rather than as picture;
  metered on the dark region beside it, it saturates. Take the edge under test
  with its own crop.
- **Test green DOMINANCE with a low absolute floor.** Red and blue read exactly
  zero in the band, which separates it from anything in the room at any exposure.
  A brightness threshold reports a clean edge.
- **The rig cannot resolve line structure.** 1080 output lines fall on about 440
  photo rows, so alternating lines average away completely and even and odd rows
  differ by 0.2 of a level. Whether a band is combed is not answerable from this
  camera.
- **Calibrate photo columns to output pixels by differencing `VDS_DIS_HB_ST`,
  and take the LEFT edge of the change.** Blanking from a lower value darkens
  only the columns that carried content, so the change's right edge is where the
  picture ended and says nothing about the register. Its left edge IS the value
  written. Four values spanning 600 output px fit
  `output = 1.1072 x photo + 175.35` with residuals of 0.4 px.
- **A row band clear of the card's own green is what the band is measured in.**
  A profile over the full height merges the test card's green colour bar into the
  region being measured and reports whichever fragment survives a contiguity
  test, which is neither monotonic in the scale nor repeatable.
- **A capture window swept by hand is not the capture the pipeline honours.**
  `IF_HB_SP2` moved with the engine frozen makes the band's left edge track
  `capture x 1024 / scale` to within 0.9%, which reads as the full window being
  delivered and contradicts the engine-solved sweep above. `PB_FETCH_NUM` stays
  sized for the capture the last solve chose throughout, so what that measures is
  the mismatch. The same applies to `VDS_HB_SP`: moved far enough it closes the
  band and opens one on the LEFT, which looks like a misplaced origin and is the
  window being dragged off the write.
- **Unfreezing does not undo hand-written registers.** The engine writes on
  change, so a state assembled by hand persists until something re-derives it;
  `/sc?U` does. A framing the engine would never solve produces artefacts of its
  own, and they are not evidence about the ones it does.

## Related

- [tail-green.md](tail-green.md) — the same colour from the capture path's write
  limit, which is a position in the input line
- [display-window-opens-early.md](display-window-opens-early.md) — the near end,
  and the far-edge measurement that puts `corner + produced` right to 0.35 px
  across six magnifications. That reading and the one here disagree; it places
  the capture's far edge inside the source's screen border so the last written
  pixel is coloured, where this one reads the boundary between written black and
  the band
- [moving-write-origin.md](moving-write-origin.md) — why a span measured from an
  assumed-fixed corner reads as loss
