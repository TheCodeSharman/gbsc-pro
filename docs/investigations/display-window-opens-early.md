# The display window opens before the picture, at both ends

The engine models where the pipeline lands the first written pixel —
`VDS_?B_SP + START_CONST + START_PER_MAG x magnification` — and places the
display window there. At 129.6 MHz the model runs short, so the window opens on
pipeline run-up rather than on picture, and what shows there is first zeros,
which decode to green, and then stale memory as a grey comb.

Reachable in normal use: zoom out to the bound, pan hard left, at 1080p.

The FAR end does not do the same in reverse. Measured directly, the picture ends
at `corner + produced` -- see below -- so only the near end has a run-up, and
only above the clock the engine selects.

## The measurement

Bench RiscPC 320x256@50, output 1080p, raster 2300 x 1125 at 129.6 MHz, capture
1044 units at magnification 2.02. `VDS_HB_SP` 8, so the model puts the picture's
first pixel at 113 and the engine opened the window exactly there.

Creeping `VDS_DIS_HB_SP` and reading the left edge off a photograph:

| window opens at | left edge |
|---|---|
| 113 (the engine's own placement) | green |
| 149 | grey comb, brightest pixel 86 |
| 167 | comb fading, 56 |
| 173 | 26 |
| **179** | **black, 3** |

So the picture proper starts about 66 px later than modelled. The far end
measured the same way: black only once the window stopped 80 px early.

## What it is not

| refuted | how |
|---|---|
| the hsync tip being captured | the capture started at 80 against a pulse measuring 79.6 units, which looked decisive — but a guard of one unit was flashed and the band came straight back. The clean readings had all been at 480p and the green ones at 1080p; the output mode was doing the work |
| the capture outrunning the playback fetch | narrowing `IF_HB_ST2` from 1124 to 1100, a 24-unit reduction, leaves the band unchanged |
| `START_PER_MAG` alone, at 68.5 input units | fits 116 px at magnification 1.673 and 137 at 2.020, and is refuted at 3.293, where the data is present 69 px before it allows |

## What the origin actually does is still open

Measured off the green boundary, the write origin is 116 px at magnification
1.673 on a 1916 px raster at 107.8 MHz, 137 px at 2.020 on 2300 px at 129.6 MHz,
and under 52 px at 3.293 on the same raster. It is neither the modelled shape,
nor proportional to magnification, nor a constant, nor a constant time.

### It is not the playback fetch

`PB_FETCH_NUM` follows the capture width, so it moved with every framing tested
and was the one quantity never varied on its own. Swept with the framing frozen
— capture 973 units, `PB_CAP_OFFSET` 282, magnification 2.222, both windows
fixed — against its computed 244:

| `PB_FETCH_NUM` | left edge | picture |
|---|---|---|
| 292 | comb, unchanged | intact, card in the same place |
| 268 | comb, unchanged | intact |
| **244** (computed) | comb | intact |
| 220 | — | the line fails to finish; the start of the picture repeats at the right |
| 200 | green as well | fails to finish |

So a fetch 20% above the requirement changes the run-up not at all, and below the
requirement the line starves, which is a different and worse fault. **The fetch
does not drive the write origin**, and the remaining candidates have to be looked
for elsewhere.

### Magnification alone moves it, and the onset is sharp

`VDS_HSCALE` crept one unit at a time with everything else pinned — capture 973
units, `PB_FETCH_NUM` 244, `PB_CAP_OFFSET` 282, `VDS_HB_SP` 8, and the display
window held at 191..2196 rather than allowed to follow the solve, so the window
is a constant and only the magnification moves. Comb pixels counted in the band
the run-up occupies:

| `VDS_HSCALE` | magnification | comb |
|---|---|---|
| 478..472 | 2.142..2.169 | **0** |
| 471 | 2.174 | 2 |
| 470 | 2.179 | 433 |
| 468 | 2.188 | 1215 |
| 465 | 2.202 | 2236 |
| 462 | 2.216 | 3226 |
| 461 | 2.222 | 3485 |
| 460..458 | 2.226..2.236 | 3069, 2756, 2472 |

So a near margin of 72 px covers the run-up up to a magnification of about
**2.17** and not beyond, and above the onset the exposed run-up grows steeply —
from nothing to a third of the band within ten units of scale. The fall past 461
is the comb's content moving out of the band rather than the artefact receding;
the onset is the number to design against.

The picture is stationary throughout: the card's left column drifts from photo
column 374 to 378 across the whole sweep, which is the content shift the 4%
magnification change accounts for.

**The margins are constants and this is not.** 72 px was fitted at one
magnification and holds only below the onset.

### It is not the output hsync pulse, in position or in width

The pulse is the line's origin and everything downstream is timed from it, so it
is the obvious candidate. It is not the cause.

**The encoder holds its sampling window fixed relative to the pulse**, so moving
the pulse slides the whole picture across the panel — 62 px of pulse moves the
picture 26 photo columns left, which is the same 62 px expressed in the panel's
830 columns per 1920 pixels. The comb sits about 50 columns left of the card, so
it slides off the panel edge and a fixed band records it as cured.

Compensated — pulse moved to 62..100 AND every output window moved right by the
same 62, so the picture lands back on the columns it occupied at pulse 0:

| | card at | comb | profile across the band |
|---|---|---|---|
| pulse 0, windows 8 / 191 / 2196 | 377 | 16 cols | 121 132 132 130 122 111 101 75 43 |
| pulse 62, windows 70 / 253 / 2258 | 377 | 16 cols | 120 130 130 129 121 110 99 75 41 |
| `VDS_HSCALE` 474, pulse 0 | 374 | **0** | 4 4 4 4 4 5 5 5 5 |

Identical to within camera noise. **Position does nothing.**

Width, swept at position 0 with the windows untouched, is the same answer: 10,
44, 60, 80 and 100 all leave the comb at 16 columns and the same peak. 20 and 30
produce no picture at all — the encoder drops the signal — which is a fact about
the encoder rather than a clean reading.

So a pulse moved later hides the artefact behind THIS panel's left edge rather
than stopping it being generated. That is a legitimate thing to want, but it is
one display's overscan: the bypass reference puts the panel's first painted
column at 320 and the comb at 326..342, so the hiding is by six columns of
overscan, and a set that paints further left shows it again. It also costs
right-hand reach, because the whole picture moves with it.

### It corrupts picture, so it cannot be blanked away

At a default framing the run-up lands in the source's own black border — the comb
at photo columns 326..342, then thirty columns of captured input blanking, then
the card. Blanking it there costs nothing, which makes a larger `nearMargin` look
free.

It is not. Zoomed in and panned so content is hard against the LEFT edge — capture
219..1018 of a 1044-unit capturable region — the comb sits directly ON picture:
brightness rises to 170 across columns 326..338 over content that continues at
~120 either side of it, with no black gap anywhere. Widening the margin there
clips real image.

So the three cheap answers are all closed. Blanking clips picture. Moving the
picture, by the output hsync pulse, costs the same number of pixels off the right
edge, which is where the picture only just reaches the panel. And magnifying less
is not available: the capturable region is 1044 units against a 2300 px raster, so
even capturing all of it only reaches 2.20 against an onset of 2.17.

### The raster width decides whether the onset is reachable at all

Magnification at a default framing is about `raster / capture`, and the capture is
a property of the source. So a wider output raster forces more magnification for
the same fill, and the onset is a bound on raster width for a given source rather
than a bound on zoom:

| mode | clock | raster | magnification | fills the panel | comb |
|---|---|---|---|---|---|
| 1080p | 108 MHz | 1920 | 1.95 | no, black bar | no |
| 960p | 107.8 MHz | 2156 | 2.077 | yes | no |
| 1080p | 129.4 MHz | 2300 | 2.222 | yes | yes |

960p meets both conditions on this source. 1080p cannot: `DisplayClock::Seeds`
offers nothing between 108 MHz and 129.6, so its raster is either 1920 or 2304
and there is no intermediate to land on.

### The clock is implicated directly, not only through magnification

The onset measured by creeping `VDS_HSCALE` at 129.6 MHz -- clean to 472, comb
from 471, magnification 2.17 -- does not hold at another clock. On the 1916 px
raster at 107.8 MHz, magnification **2.179** shows no run-up at all, with the
display window opened right down onto the picture's own corner:

| clock | raster | magnification | near margin needed |
|---|---|---|---|
| 129.6 MHz | 2300 | 2.222 | more than 72, under 121 |
| 107.8 MHz | 1916 | 2.179 | **0** |

Two magnifications 2% apart, opposite outcomes. So magnification is not a
sufficient description, and the earlier onset figure is a property of that clock
rather than of the part. Two candidate readings remain, and they are not
separated: the run-up is a fixed TIME, so a faster clock spans more output
pixels of it; or it follows the raster width for a reason nothing has proposed.

Nor is it produced line length. At a capture of 785 the comb is present at every
magnification from 2.09 to 2.73, spanning produced widths of 1640 to 2144 px, so
no single threshold in produced pixels fits either.

### The margins are constants and both of them are wrong off their fitting point

`nearMargin` 72 and `margin` 84 were fitted at magnification 2.020 on a 2300 px
raster. At 107.8 MHz they cost picture at both ends, and a fixed pixel count is a
larger FRACTION of a narrower line, so the cost grows as the raster shrinks.
Measured against the bypass reference, with content panned hard against the edge
under test:

| | left edge | right edge |
|---|---|---|
| bypass 800x600 | 319 | 1155 |
| 107.8 MHz, margins 72 / 84 | 335 | 1148 |
| 107.8 MHz, near margin 28 | **319** | -- |
| 107.8 MHz, far margin 62 | -- | **1156** |

Both reach the panel with margin to spare, and the rest of each fitted constant
is pure loss -- 44 px of the near margin, 22 of the far. Below those values
nothing more is gained, because the panel edge is the limit rather than the
solve. Crept all the way down at magnification 2.179 with content hard against
the edge under test, BOTH margins reach zero with no artefact: the profiles at
margin 0 and at the value that first reaches the panel are identical, and what
falls away past the last lit column is the bezel.

**Neither margin should be a constant.** What each needs depends on the clock as
well as the magnification, and at 107.8 MHz across the usable range it is
approximately nothing.

### The far end: this instrument says the model is right, another says it is short

`Axis::solve` puts the far edge of both windows at `corner + produced`. Measured
by the border-difference instrument below, that is where the picture ends: six
magnifications with a residual under 0.35 px, and two more up to 2.50x within
8 px.

**A second measurement disagrees, and the two are not reconciled.** Creeping
`VDS_DIS_HB_ST` down to where the green band at the far end vanishes puts the
picture's end 25 px short of `corner + produced` at magnification 1.138, 28 px
short at 1.365 and 10 px short at 1.718 —
[display-window-overruns-the-picture.md](display-window-overruns-the-picture.md).

The instruments differ, which is the likeliest place for the disagreement to
live. This one puts the capture window's far edge INSIDE the source's screen
border, so the last written pixel is magenta and the boundary is read by
differencing two frames. The other reads the boundary between written black and
the band, a softer edge, at about ±5 px. Neither has been run at the other's
framing, so **do not treat either figure as settled**, and do not size a constant
from one of them.

The write origin, recovered from the FAR edge as `far - produced`, against the
`VDS_HB_SP + 55 + 25 x magnification` the near edge gives directly:

| `VDS_HSCALE` | magnification | produced | origin from the far edge | model |
|---|---|---|---|---|
| 500 | 2.048 | 1636.35 | 115.0 | 114.2 |
| 560 | 1.829 | 1461.03 | 108.8 | 108.7 |
| 620 | 1.652 | 1319.64 | 103.4 | 104.3 |
| 700 | 1.463 | 1168.82 |  98.1 |  99.6 |
| 800 | 1.280 | 1022.72 |  93.6 |  95.0 |
| 900 | 1.138 |  909.08 |  89.4 |  91.4 |

Fitted `57.41 + 28.04 x magnification` against the model's `63 + 25 x`, worst
residual 0.35 px. At the default framing's 1.838 the two are 0.1 px apart, against
the 84 px `Axis::margin` reserved and the 28 px shortfall that reserve was
attributed to.

**The bar at the far end reproduces, and the round-trip test that says otherwise
cannot see it.** Three output mode round trips -- 1080p to 960p and back, and
1080p to 480p and back, each with the framing reset -- leave the far end dark
with the source's screen border blanked. But blanking the border also writes
BLACK into the region past the write, so a bar has nothing to be made of
whatever the mechanism, and the test cannot distinguish. Exposing that region
means dirtying it bright FIRST and retreating the write off it afterwards.

Done that way the band is present at every magnification measured, it is not
stale memory, and it is not captured content —
[display-window-overruns-the-picture.md](display-window-overruns-the-picture.md).

The near end has no equivalent: nothing shows there at any clock the engine can
select.

### The instrument: flip the source's border and difference two photographs

Brightness thresholding on a photograph measures whatever the card happens to
have at that position. Differencing two frames that share their content and
differ only in the source's screen border measures the WRITE, and nothing else.

- `PATTERN CARD` sets the RISC OS border magenta; `MODE X320 Y256 C256 F50`
  resets the palette and repaints the same PM5544 card, so the pair differ only
  in the border. The border reaches the end of active video, so it puts colour
  hard against the far end of the capture without any panning.
- Place the capture window's far edge INSIDE the border and the last written
  output pixel is magenta. Everything past the write is identical in both frames,
  so the difference falls to the camera's noise floor there and the boundary is
  read off without knowing what the card looks like.
- Move the magnification by writing `VDS_HSCALE` with everything else frozen.
  The capture, the fetch and both windows stay put, so `produced` is the only
  quantity that moves.

Photo columns to output pixels, measured in place: photograph at one
`VDS_DIS_HB_ST` as a reference, then at several others, and difference each
against the reference. The difference is exactly the strip the register blanked,
and its rising half-height IS the register. A quadratic fits seven points over
600..1800 to 0.8 columns; a straight line leaves a 2.6-column smile, which is the
lens.

**The mapping does not survive an output mode change.** Measured 57 columns
adrift -- a fifth of the picture's width -- after a 1080p / 960p / 1080p round
trip and a 1080p / 480p / 1080p one, with the raster registers identical either
side. The encoder samples the analog output and re-acquires, and where it lands
the picture on the panel is its choice. **Re-measure the mapping after anything
that re-locks the encoder**, and treat a photographic measurement that straddles
one as two experiments. Read against a stale mapping, a correct far edge reads as
110 px of overshoot and a repeat of the line reads as the picture.

Two further cautions, both paid for:

- A brightness count over a fixed band cannot be compared across anything that
  moves the picture. Starved-fetch frames score *lower* than a clean edge on such
  a count, because the picture left the band rather than because the edge cleared.
- With the display window left open past the picture, the playback stage keeps
  fetching and the line repeats, so there are several bright blocks and only the
  first is the picture. Anchor the search to the block containing the peak.

## What the code does about it: nothing, any more

`Axis` carried two constants for this — `nearMargin` 72 px and `margin` 84 px,
horizontal only — and `fitToRaster` added both back into the room so the picture
was produced larger and the valid part still filled the screen. **Both are
deleted.** The display window is now the picture at both ends.

They were fitted at magnification 2.020 on a 2300 px raster and are needed at no
clock the engine can select. Crept onto the picture's own corner at both ends,
with content panned hard against the edge under test, nothing shows at:

| mode | clock | raster | magnification |
|---|---|---|---|
| 1080p | 107.8 MHz | 1916 | 1.838, 2.0, 2.179 |
| 960p | 107.8 MHz | 2156 | 2.0 |
| 720p | 80.85 MHz | 2156 | 2.0 |

Keeping them cost picture at both ends, measured against the bypass reference:
the left edge landed at photo column 335 against bypass's 319, and the right at
1148 against 1155. A near margin of 28 reaches the left edge and a far margin of
62 reaches the right, so most of each constant was pure loss even where the
artefact exists.

**They also made the framing depend on the output resolution.** Being absolute
output pixels, they were a different fraction of every raster — 8.8% of the
picture hidden on a 1916 px line against 7.2% on a 2300 px one — so the same
framing showed a different part of the source at different output resolutions,
which `PanAndZoom` holding proportions of the capturable region exists to
prevent.

**What brings the NEAR one back is raising `OutputMode::EngineCeilingHz`.** At
129.6 MHz the run-up is real, lands on picture rather than on the source's
border, and cannot be blanked without clipping nor moved without costing the
right edge — which is why the ceiling is where it is. Its 72 px is output pixels
at that clock on this board, a property of the part's pipeline rather than of a
television, so it travels; a board running the engine at another clock has not
been checked.

**Whether anything brings the FAR one back is open.** It was fitted to an
artefact whose cause was never established, and the border-difference instrument
puts the far edge right to 0.35 px across magnifications 1.14 to 2.05 — but the
creep instrument puts the picture 10..28 px short of `corner + produced`, which
is the size a far margin would cover. Settle the disagreement above before
restoring any constant.

## Photographing green is not the same as seeing it

The band is a *dark* green, around `rgb(6,41,8)` through a camera. A detector
thresholding on brightness misses it entirely and reports a clean edge, which is
what produced two false readings here before the pixels were dumped and read.
Test green DOMINANCE — green above both red and blue by a margin — and set the
absolute floor low.

## Related

- [tail-green.md](tail-green.md) — the same colour at the far end of the line,
  from the capture path's write limit, and the sync tip case this was mistaken
  for
- [display-window-overruns-the-picture.md](display-window-overruns-the-picture.md)
  — the far end measured the other way, which puts the picture short of
  `corner + produced` rather than on it
- [moving-write-origin.md](moving-write-origin.md) — where the model came from,
  and why measuring from a moving corner is what made it look like loss
