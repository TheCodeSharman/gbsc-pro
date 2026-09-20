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

## What is still open

**Whether the fraction belongs to the encoder or to this television.** The left
edge rules out symmetric overscan, but not a set that crops one end, and nothing
on the board can see what the MS9288A transmits -- it is on no MCU's I2C bus and
EDID is unreachable. A second display is what separates them, and until one is
attached a per-mode fraction taken from this panel may be this panel's number.
`docs/known-issues.md` states the general form of that trap.

**The mechanism.** 0.804 and 0.786 correspond to 690 and 679 active pixels
against the standard's 720, or to the encoder stopping 2.90 and 1.90
microseconds before the end of the line. Neither is a round number, neither
matches a front porch or a blanking interval, and the two SD modes differ by
three times more than their standards do. Refuted along the way: a fixed pixel
count, a fixed time, a fixed fraction shared between modes, and the raster
ratio.

## The instruments

`tv-snap` for the photographs and `photo_profile.py` for the column profile. The
panel reference needs the source at 800x600@60 and `/uc?x` to turn
`preferScalingRgbhv` off, and `/uc?x` again to come back -- the line count no
longer puts an RGBHV source into pass-through, so it has to be asked for.

The source's border flashes cyan and magenta twice a second, so a band at the
edge of a frame that changes colour between two shots is the source and not the
scaler. `the-colour-flash-is-the-source-border.md`.
