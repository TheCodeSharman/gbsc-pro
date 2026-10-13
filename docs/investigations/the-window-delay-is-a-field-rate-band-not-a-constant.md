# The transmitted window's opening follows the field rate, and the four readings of twenty measured our own aperture

`OutputMode::TransmittedWindowDelayPx` says the chain starts carrying video a
fixed count of samples after our back porch ends, and 20 is the count. Measured
in one session across two inputs, two sources and two output modes, there is no
such count: the opening moves with the output's FIELD RATE, by 28 samples
between the 50 Hz and 60 Hz rasters of one output mode.

The quantity is the sink's own window position, which
[`the-sinks-own-window-position-is-per-raster.md`](the-sinks-own-window-position-is-per-raster.md)
measured across the stock AKF50 set and holds. This page is what rules out the
two explanations a single off reading leaves open -- the input and the output
mode -- and why the readings that support 20 cannot see any of it.

## The measurement

`VDS_DIS_HB_SP` and `VDS_DIS_HB_ST` each walked four values into the window,
automation frozen, read off the USB HDMI capture. The estimator is the edge of
the columns that went to TRUE black and were lit in the reference frame, which
is what keeps a source's own black bands out of it: a leading-run reading on
`PATTERN CARD` merges with the card's own black and drags the slope, giving
1.033 on the first interval against 1.145 on the next two.

| source | input | output | T | field Hz | window opens | sync + porch | delay |
|---|---|---|---|---|---|---|---|
| Wii 480p | `ypbpr` | 1080p | 1602 | 59.94 | 146.05 | 139 | **+7.1** |
| Wii 480p | `ypbpr` | 720p | 2402 | 59.94 | 384.70 | 378 | **+6.7** |
| RiscPC 640x480@60 | `vga` | 1080p | 1600 | 60.02 | 145.19 | 139 | **+6.2** |
| RiscPC 320x256@50 | `vga` | 1080p | 1916 | 50.08 | 173.78 | 139 | **+34.8** |

Each row is a four-point fit whose residuals are at most 0.3 columns of ~1900.

**The instrument is checked against two quantities it did not fit.** The slope
is predicted by the standard's line total over our raster, and the measured
slope matches it to 0.0007 on every row -- 1.3740 against 2200/1602, 1.0320
against 1650/2402 scaled by the dongle's 1.5, 1.3750 against 2200/1600, 1.1480
against 2200/1916. The window's WIDTH is predicted by the mode's own fraction
`T x carriedPx / totalPx`, and comes out 1395.2 against 1398.1, 1670.0 against
1672.1, 1394.0 against 1396.4 -- within 3 units of ~1400 to 1670, which is the
fraction model those earlier records already carried.

## What it is not

- **Not the input, and not the component path.** The Wii on `ypbpr` at T 1602
  and the RiscPC on `vga` at T 1600 open at 146.05 and 145.19 -- 0.86 units
  apart, on different inputs, different sync types and different colour paths.
  Whatever moves the opening reaches both alike.
- **Not the output mode.** The same source gives +7.1 at 1080p and +6.7 at
  720p, across a 50% change of raster and a change of standard.
- **Not the lock's placement.** Three `PAD_SYNC_OUT_ENZ` toggles on the settled
  60 Hz `vga` state leave the first lit column at 262, 262, 262 -- the opening
  does not move when the sink places its window afresh.
- **Not a time after the pulse.** 720p at T 2402 runs the same clock as 1080p
  at T 1602 to 0.2%, and the two deltas agree; the 50 Hz row runs the same clock
  again and is 28 samples away.

What is left is the output's field rate, in the bands the per-raster page
measured: ~6 to 7 samples on the 60 Hz rasters of both outputs, ~35 on the
50 Hz 1080p raster.

## Why the readings that support 20 cannot see it

[`the-transmitted-window-opens-late.md`](the-transmitted-window-opens-late.md)
reads +19.5, +20.3, +19.8 and +20.8 by walking a blanking register into the
window with the picture zoomed to overrun the aperture at both ends. **With
picture at the aperture's edge, on a raster whose own position is AFTER the
aperture, the sink lands its window ON the aperture** -- which the per-raster
page measured on eight of twenty-eight states, the 1916 raster among them. The
walk then extrapolates to the aperture the constant itself placed, so it returns
the value already in force whatever that value is.

That is why the four readings agree with each other to a unit and with nothing
else: 1080p at T 1916 reads 159.5 and 161.6 on that procedure, against 171.7
over four states on the per-raster page's procedure and 173.78 here, both of
which had the aperture far enough away that the pull could not reach it. **A
measurement of this constant has to put BLACK at the aperture's edge**, or leave
the aperture well past the sink's own position, or it measures itself.

## What is still open

**Where the window lands with picture at the edge and the aperture far from the
own position.** The per-raster page's rule covers the aperture near the own
position -- landing on it where it is after, and 6.6 to 10 units before it where
it is before. Both `vga` and `ypbpr` 60 Hz rows here have the aperture 176 units
after the own position, aspect shaping having narrowed it, and open 13 units
BEFORE the own position rather than on it or near it. Two inputs and two sources
agree on that, so it is repeatable and it is not the rule as stated.

**640x480@60 is the one source measured in both conditions, and the direction
is that the further the aperture, the earlier the window opens**: 158.5 with
black at the edge, 151.9 with picture at the edge and the aperture at ~160, and
145.19 here with picture at the edge and the aperture at 335. So the sink's pull
is not toward the aperture -- it is away from it, which no reading of the rule as
stated predicts.

The experiment is the per-raster page's own: the same four states, settled,
through a pad toggle, with black panned to the aperture's edge and then picture,
at a shaped aperture and an unshaped one. Nothing here separates the aperture's
DISTANCE from the aspect shaping that produced it, and the shaping is the
variable the earlier sweeps did not have.

## What follows for the constant

**Do not move it to 6.** It is wrong at 20 on every raster measured here and
wrong at 6 on the 50 Hz raster by 29 samples, because it models a quantity that
takes two values per output mode. The per-raster page's conclusion stands: a
per-output constant cannot close it and a table per output and field-rate band
can.

**A change of placement cannot break the width parity.** The memory window's
width is biased odd in `OutputWindow::solve()` against the capture's own stop,
inside whatever bounds the placement hands it, so a translation of the aperture
is re-biased on the next solve. What would break it is insetting one edge or
changing the span. [`horizontal-scale-corruption.md`](horizontal-scale-corruption.md).
