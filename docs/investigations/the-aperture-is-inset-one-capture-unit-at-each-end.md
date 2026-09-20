# The aperture is inset one capture unit at each end

The scaler interpolates between two capture units, so an output unit landing at
source position `s` reads units `floor(s)` and `floor(s) + 1`. The last unit the
capture wrote is `capture - 1`. An aperture that closes where the picture ends
therefore shows one unit produced from a capture unit nothing filled, and the
playback stage fetches whatever the previous mode left in that memory.

It reaches the picture as a line down the bottom edge and a column down the
left, on every scaled mode. The line is static while the picture moves, which is
the signature: it is a frozen copy of memory, not video.

## What the solve believed

`Axis::solve()` closed both windows on `floor(windowStop + originOffset +
produced)`. At 320x256@50 on `vga`, capture 582 at `VDS_VSCALE` 552 in a
1124-line raster:

| | |
|---|---|
| magnification | `1024 / 552` = 1.85507 |
| `produced` | `582 x 1024 / 552` = 1079.652 |
| write origin | `0.2 + 0.8 x 1.85507` = 1.684 |
| write ends | `39 + 1.684 + 1079.652` = 1120.336 |
| `VDS_DIS_VB_ST` | 1120 |

Every row of the aperture is inside that span, so the arithmetic reported no
overrun. What it did not charge is the interpolator's reach: the last aperture
row sits at source position 581.3 and reads rows 581 **and 582**, and only rows
0..581 were captured.

## What the bench measured

Three measurements, all on `vga` at 320x256@50 with automation frozen so the
engine could not re-solve underneath them.

**Blanking the bottom forty rows removes it.** `VDS_DIS_VB_ST` 1120 -> 1080
takes the line away entirely; restoring 1120 brings it back. So the line is
inside the display aperture and is the part's, not the panel's.

**One more captured row removes it.** `IF_VB_ST` 620 -> 621 clears it with the
windows untouched, because the row the interpolator reaches for is then written.
Alternated four times, present at 620 and absent at 621 each time.

**Forty fewer captured rows turn it into a forty-row band.** `IF_VB_ST` 620 ->
580 leaves a band across the bottom of the picture carrying the source border's
*previous* colour while the live border shows its current one. The artefact is
proportional to how far the aperture outruns the write, which is what a
one-unit case cannot show on its own.

The source's border alternates colour, so a clip separates the two
populations without any calibration: written rows vary frame to frame, stale
rows do not.

## What the solve does now

The usable write end is one capture unit's worth of output short of where the
write ends:

    writeEnds = windowStop + originOffset(magnification)
              + produced - magnification

`produced - magnification` is `(capture - 1) x magnification`, so the aperture
closes on the last unit that can be produced from captured memory alone. Both
windows share that far edge, so the playback fetch stops there too.

It costs the far edge one capture unit of picture -- 1.86 output lines and 1.80
output pixels at the bench framing -- and that unit was never written, so
nothing is lost but the artefact. On the bench build the engine moves from
`VDS_VB_ST` 1120 / `VDS_HB_ST` 1853 to 1118 / 1851.

## What would bring it back

Closing either window on `produced` alone. The tempting form is
`VDS_?B_ST == VDS_DIS_?B_ST == corner + produced`, which two host tests asserted
and which reads as the windows hugging the picture exactly; it hugs one unit of
memory nothing wrote. `test_axis.cpp`'s "the aperture's last unit is
interpolated from captured memory" walks the whole zoom range on both axes and
is what holds the rule.

A register dump cannot see this. Every value is self-consistent under the old
arithmetic, and `produced` really is `capture x 1024 / scale` with no loss term
-- `docs/scaler-geometry-model.md` measures it that way, and it stays true. The
picture extent and the *usable* picture extent are different lengths.

## The near end needs the same unit, for a different reason

The far end is the interpolator reaching forward. The near end has no backward
reach -- the first output unit sits at source position 0 and reads units 0 and 1,
both written -- so a margin there needs a different mechanism, and it has one.

**The write origin marks where content first APPEARS, which is the first unit the
capture only PARTLY filled.** `moving-write-origin.md` measured it by creeping
until the picture started, so the fit is systematically one capture unit early.
An aperture opening on the origin therefore shows a unit carrying whatever the
previous mode left in that memory: a thin static line down the left edge, on the
modes where the fractional part falls the wrong way.

Crept on the bench with automation frozen, `VDS_DIS_HB_SP` raised one unit at a
time until the line cleared, the source's flashing border separating written from
stale:

| source | magnification | write origin | first clean corner | `ceil(origin) + 1` | `ceil(origin + mag)` |
|---|---|---|---|---|---|
| 1024x768@60 | 1.133 | 140.32 | **142** | 142 | 142 |
| 800x600@60 | 1.168 | 140.19 | **142** | 142 | 142 |
| 800x600@60, zoomed | 2.165 | 140.12 | **143** | 142 | 143 |

The third framing is what makes it a capture unit rather than an output pixel: a
fixed one-pixel margin predicts 142 there and the line is still visible. The
zoom was taken on one source with everything else held, so the magnification is
the only quantity that moved between the second row and the third.

So `Axis::solve()` opens the display window at
`ceil(windowStop + originOffset + magnification)`, and the memory window still
opens where the write does -- it is the DISPLAY that must not show the unit,
because opening the fetch late moves the picture instead.

**This is not the deleted `nearMargin`.** That was 72 OUTPUT PIXELS, fitted at
one magnification on a 2300 px raster at 129.6 MHz, covering a wide green run-up
band; `display-window-opens-early.md` deleted it because it is needed at no clock
the engine selects, and because an absolute pixel count is a different fraction
of every raster. This is one CAPTURE unit, so it is scale-invariant by
construction and costs the same fraction of the source at every raster and every
zoom.

## Vertically the near end exposes nothing

Measured: with the vertical aperture opened **eleven rows** before the write
starts, the top edge stays clean, where eleven columns at the horizontal near end
is a plain band of stale memory. Reading before the first written COLUMN of a
line reaches the previous line's storage, which holds real data; reading before
the first written LINE reaches past the start of the frame, which does not come
back as picture.

The inset is applied on both axes even so. It is the same statement about the
same model -- the origin is where content starts, not where the first whole unit
lands -- and the axes differ only in whether the consequence is visible. Two rows
at the top of a raster the picture overscans anyway is not worth an exception
that the next reader has to re-derive.
