# The transmitted window starts exactly where our blanking ends, and its length
# is a per-mode fraction that is not the standard's

`OutputMode::solve()` sets the active window from two different kinds of
quantity: the sync pulse and back porch are the standard's DURATIONS, converted
to whatever clock the line runs at, and the active width is the standard's
FRACTION, `horizontalTotal x activePx / totalPx`. Measured against the panel on
all six output modes, the first half is exact everywhere and the second is
wrong on the two SD modes.

## What was measured

The panel's painted area is found once, from an 800x600 source in pass-through,
which fills the screen by construction: **photo 52.5 .. 1572.3** at this camera
position.

Per output mode, the photo-column to output-pixel mapping is then re-derived in
the state being judged, because it does not survive an encoder re-lock.
`VDS_DIS_HB_SP` is clipped to two values and `VDS_DIS_HB_ST` to three, one
photograph each, and consecutive frames are differenced: the band where they
differ is the strip one register blanked and the other did not, so its two edges
are two register values seen in photo columns.

**CLIPS MUST SPAN THE LINE, NOT ITS RIGHT-HAND END.** Fitted on right-hand clips
alone the mapping is an extrapolation of ten to one, and dropping a single
contaminated point moved the panel's left edge by 180 output pixels. Clipping
both windows makes the fit an interpolation, and the residuals fall from tens of
photo pixels to about two.

**A CLIP OUTSIDE THE PAINTED AREA IS NOT A MEASUREMENT.** On a mode whose window
overruns the panel the strip falls off-screen, its band edge pins to the panel
rather than tracking the register, and the point reads as a large residual.

## The left edge is exact on every mode

`activeStart` is `sync + back porch`, both converted from the standard's
durations. Against the panel, on the bench RISC PC at 320x256@50:

| mode | our total | `activeStart` | panel's left edge | delta |
|---|---|---|---|---|
| 1080p | 1916 | 140 | 141.2 | **+1.2** |
| 1024p | 2023 | 360 | 361.6 | **+1.6** |
| 960p | 2156 | 424 | 426.2 | **+2.2** |
| 720p | 2156 | 284 | 284.6 | **+0.6** |
| 576p | 2070 | 317 | 319.1 | **+2.1** |
| 480p | 2053 | 244 | 244.4 | **+0.4** |

Six modes spanning 1916 to 2156 pixels and 54 to 108 MHz, agreeing to about two
pixels, which is the uncertainty in the panel reference itself. The encoder
finds active video where our blanking ends, exactly, and the durations model
needs nothing.

**THIS ALSO SETTLES THE TELEVISION'S OVERSCAN.** Overscan is a symmetric
magnification: it eats both ends. Nothing is lost at the left on any mode, so
whatever shortens the SD modes is not the set cropping the picture. The
pass-through reference agrees independently -- an 800x600 source passed straight
through fills the panel edge to edge.

## The right edge is short on the two SD modes and nowhere else

| mode | `activeStop` | panel's right edge | delta |
|---|---|---|---|
| 1080p | 1812 | 1829.4 | +17.4 |
| 1024p | 1894 | 1917.5 | +23.5 |
| 960p | 1957 | 1966.1 | +9.1 |
| 720p | 1956 | 1967.1 | +11.1 |
| 576p | 2042 | 1944.4 | **-97.6** |
| 480p | 1966 | 1895.1 | **-70.9** |

On the four that work the panel paints past our `activeStop`, so the whole
active window is transmitted and the measurement sets no upper bound on what the
chain could carry. On the two SD modes it stops short, and the deficit is not
one quantity: -70.9 and -97.6 pixels, -1.31 and -1.51 microseconds, -3.5% and
-4.7% of the line.

## The raster ratio is REFUTED as the boundary

The standing hypothesis was a threshold in `our total / the standard's total`:
the four that work run 0.87x to 1.31x and the two that do not run 2.40x, so the
boundary had to lie between. 720p and 960p were measured to find it and both
came out clean.

`OutputMode::EngineCeilingHz` was lowered to 45 MHz and the firmware reflashed,
which makes the raster solver pick the 40.5 MHz display clock for both SD modes
and lands them at **1.50x and 1.80x** -- inside the window the boundary was
supposed to be in. The deficit is undiminished:

| mode | ratio | our total | painted span / line |
|---|---|---|---|
| 480p | 2.393 | 2053 | **0.8040** |
| 480p | 1.795 | 1540 | **0.8042** |
| 576p | 2.396 | 2070 | **0.7852** |
| 576p | 1.497 | 1293 | **0.7875** |
| | | `activePx/totalPx` | 0.8392 / 0.8333 |

So there is no ratio threshold, and the shape of the model is right after all:
**what the chain takes IS a fixed fraction of the emitted line, held to 0.3%
across a 33% change of raster.** The fraction is simply not the standard's on
the two SD modes -- 0.804 against 0.839, and 0.786 against 0.833.

That is what makes a measured per-mode fraction safe to ship where it was not
before: the quantity is stable against the one thing the engine varies
underneath it.

## The mechanism: the SD rasters are transmitted as VESA PC modes

The sink names the format it receives, and on the two SD modes it does not name
an SD format:

| our raster | the sink reports |
|---|---|
| 525 lines at 50 Hz | **640 x 480 @ 50 Hz** |
| 625 lines at 50 Hz | **800 x 600 @ 50 Hz** |

So the encoder does not carry a 50 Hz 525- or 625-line raster as CEA 480p or
576p. It matches the line count against a table, finds VESA DMT's 640x480 and
800x600, and transmits those -- 640 and 800 active pixels where the standard's
720 was assumed, which is 0.800 and 0.781 of DMT's own line totals against the
0.804 and 0.786 measured here. That closes the fraction: it is the VESA mode's
active width, not a defect.

It also predicts the vertical, and the prediction holds.

## The vertical window is the VESA mode's too, and CEA's leaves a black bar

Measured on the bench, RiscPC 320x256@50 on `vga`, automation frozen, the
vertical window stepped in ten-line increments with its span held and the
picture's own edges read off each photograph. A span that reaches both panel
edges is what makes the pinning visible:

| picture start (raster line) | top edge (photo row) | bottom edge |
|---|---|---|
| 4 | 37 (pinned) | 812 |
| 14 | 37 (pinned) | 827 |
| 24 | 37 (pinned) | 841 |
| 34 | 50 | 855 |
| 44 | 64 | 869 |

The bottom tracks 1.407 photo rows per raster line throughout, so the raster to
panel mapping is linear and one to one -- this is not a set letterboxing the
picture. The top is pinned until the start passes **24.8**, and the same
arithmetic puts the last painted line at **624.6**. So the chain paints raster
lines 25..624: **600 of the 625, from 24 lines of blanking**, which is DMT
800x600@56's vertical window exactly.

CEA 576p states 576 lines from line 44. Placing the picture there left **19
lines of the painted area black across the top of the screen** and 4 unused at
the bottom, which is what the mode looked wrong for.

The same sweep at 480p pins at 35.2 and 514.7, against DMT 640x480's 35 and 515
and CEA 480p's 36 and 516 -- the two windows agree to within a line, which is
why that mode never showed a bar and why the vertical was not suspected sooner.

**Only where the picture may go changed.** The emitted vsync pulse stays at
CEA's five lines: the chain locks to it as it is, and lengthening or shortening
it was never the fault. `Mode576p` now states 600 active lines with 19 lines of
back porch and 1 of front, which is still 625.

## What is still open

**Whether these windows belong to the encoder or to this television.** The sink
reporting `800x600@50` is evidence about the ENCODER -- a mode name on an info
panel is the HDMI timing received, where overscan would be the set's own choice
about how to paint it -- so the identification is the board's behaviour and
portable. What a second display would still settle is the horizontal fraction's
last fraction of a percent, and whether any set crops one end.

**Why the horizontal does not follow the same table.** Vertically the chain uses
DMT's blanking; horizontally it does not. Crept at 576p, the panel's first
painted column is raster 321 against our `activeStart` of 317, where DMT
800x600@56's 72 + 128 of blanking would put it at 404. The encoder cannot know
our pixel clock, so it has to find the line's active region by measurement while
the line COUNT is exact and can be looked up -- which is consistent with both
halves, and is not established.

## The instruments

`tv-snap` for the photographs and `photo_profile.py` for the column profile. The
panel reference needs the source at 800x600@60 and `/uc?x` to turn
`preferScalingRgbhv` off, and `/uc?x` again to come back -- the line count no
longer puts an RGBHV source into pass-through, so it has to be asked for.

The source's border flashes cyan and magenta twice a second, so a band at the
edge of a frame that changes colour between two shots is the source and not the
scaler. `the-colour-flash-is-the-source-border.md`.
