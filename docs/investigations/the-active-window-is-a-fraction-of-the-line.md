# The output active window is a fraction of the line, not a front porch time

The encoder starts sampling where the scaler's blanking ends, and then resamples
the line into the standard's active pixel count. So what it can carry is

    horizontalTotal x activePx / totalPx

and everything painted past that is off the end of its line. A front porch
stated as a duration cannot express that, because the emitted raster overruns
the standard's by a different factor in every mode -- 1920 against CEA-861's
2200, 2026 against VESA DMT's 1688.

`OutputMode` therefore carries each standard's own raster in the standard's own
pixels at the standard's own clock, and takes two different kinds of quantity
out of it:

| | kind | why |
|---|---|---|
| sync pulse, back porch | DURATION, converted to the clock the line runs at | the encoder measures the pulse and finds active video where the blanking ends |
| active width | FRACTION of the line | the encoder resamples the line into `activePx` samples however long it is |

The front porch is never stated. It is what the total leaves, and
`OutputMode::FrontPorchMinPx` is the floor under it.

## What it cost

A framing set flush to the panel at 1080p spilled off the right at 1024p. The
stored proportions are byte-identical at both resolutions and the engine filled
each mode's stated active region to within 0.2%, so neither of those was the
fault: the active region itself was 3.6% too wide at 1080p and 5.4% too wide at
1024p, and the difference between the two errors is what made a framing look
resolution-specific.

## The measurement

The aperture's near edge stepped with automation frozen, each frame differenced
against the maximally blanked one so the blanking boundary is located by where
the frames stop agreeing rather than by an edge threshold. Straight-line fits
from raster units to photo columns:

| | slope, photo px per raster px | max residual |
|---|---|---|
| 1080p | 0.9143 | 0.93 photo px |
| 1024p | 1.0030 | 0.83 photo px |

The panel's painted area comes from a pass-through 800x600 frame at the same
camera position, which is the only reference for where the panel stops:
crop columns 50.8..1564.4. Converted through each fit:

| | raster | emitted active | **panel window** | picture | past the panel |
|---|---|---|---|---|---|
| 1080p | 1920 | 140..1856 (1716) | 144.5..1799.7 (**1655**) | 140..1852.9 | 53 px |
| 1024p | 2026 | 360..1978 (1618) | 363.6..1872.7 (**1509**) | 360..1976.7 | 104 px |

Two readings come out of it.

**The window starts where our blanking ends**, within 4.5 raster px at 1080p and
3.6 at 1024p. The encoder is not imposing a porch of its own.

**Its width is the standard's fraction of the line.** 1655 of 1920 is 86.2%
against CEA's 1920/2200 = 87.3%; 1509 of 2026 is 74.5% against DMT's
1280/1688 = 75.8%. The emitted fractions were 89.4% and 79.9%.

## The model this refutes

That the encoder imposes the standard's porch POSITIONS as well as its pixel
count, putting active at `raster x (sync + backPorch) / totalPx` -- 167.6 at
1080p, 432.1 at 1024p. It fits the required width just as well and is wrong
about the position, and the near-edge sweep settles it: at 1024p, blanking
raster 380, 400 and 420 each moved the boundary to crop columns 69, 88 and 108,
all inside the painted area. Those raster positions are on the panel, so the
encoder cannot be starting at 432.

## What changed, and what it landed on

| | `VDS_HSCALE` before | after | what the bench asked for |
|---|---|---|---|
| 1080p | 419 | **429** | 428 |
| 1024p | 444 | **468** | 467 |

The bench figures are from stepping `VDS_DIS_HB_ST` and reading the picture's
own right edge off each photograph, five points per mode. The solve now lands
within one unit of both, from the standards alone with no bench constant in it.

Photographed afterwards, the two modes are the same image: cross-correlated
column profiles give **0 photo px of shift at r = 0.9978**, which is what a
framing surviving an output resolution change looks like.

## What is still open

The measured windows are 1.2% (1080p) and 1.8% (1024p) narrower than the
standard's fraction, consistently in that direction, so about 1% of the picture
still runs past the right of this panel. That residual is not named. It is the
size of a television's own overscan and is within the reach of the crop
calibration, and sizing a constant from it would ship one panel's number --
`CLAUDE.md`, "Separate what the board must emit from what one display happens to
show". A second display is what splits the two.
