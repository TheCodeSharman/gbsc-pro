# The capture may not outgrow the output raster

The picture breaks up when the capture window grows past the width of the
**output raster**. It is not a property of the capture on its own, not of the
kept count, and not of the source line: the same capture is clean at one raster
and shredded at another, with the source, the divider and the sample rate all
unchanged.

`SamplingClock::KeptCeiling` cannot express this, because a divider does not
appear in it.

## The threshold

The divider is held so the source is fixed, and the FRAMING is zoomed, which
moves the capture and leaves the kept count alone. Every step is engine-solved
through `geometryControls` -- `/sc?O=40`, then `/sc?O=8` across the edge -- so
the scale, fetch and stride describe the capture being judged.

| output raster | source | divider | clean at | corrupt at |
|---|---|---|---|---|
| 1280 | 640x480@75, 1080p out | 1900 | 1288 | 1328 |
| 1600 | 640x480@60, 1080p out | 1900 | 1625 | 1633 |
| 1600 | 640x480@60, 1080p out | 1800 | 1631 | 1647 |
| 1920 | 640x512@50, 1080p out | 1900 | 1693, the ceiling | -- |
| 2400 | 640x480@60, 720p out | 1900 | 1827, the ceiling | -- |

Every row is satisfied by **capture > 1.02 x raster**, and the two rows that
bracket it tightly put it in 1.016..1.021. The display window is `raster - 104`
on both of the rasters that can reach it, so the same threshold reads as
`window + ~130`; nothing here separates those two statements.

The rows that never corrupt are the ones whose capture cannot reach the
threshold. At 1920 the capture ceiling is 1693 against a threshold of ~1955; at
2400 it is 1827 against ~2450.

**Two dividers on one raster give one threshold in capture units.** 1800 and
1900 are different line lengths and different ADC sample rates over an
unchanged line duration, and both break at ~1630. So no divider term is needed,
and none of a fixed sample count, a fraction of the line or a write RATE
survives the raster rows above.

## Why it is the playback fetch

`VDS_HSCALE` is ten bits and unity is 1024, so the scaler **cannot minify**.
Zoom-out magnifies less and less until the field pins at 1023, and past that
point every further unit of capture is a unit of produced picture: `produced ~=
capture`, growing with nothing to stop it. The threshold is where the produced
picture is about as wide as the whole output line, so the playback has to fetch
more pixels per output line than the line has clocks.

**Memory bandwidth reaches it.** At a capture of 1617 on the 1600 raster --
clean, and 1.1% under the threshold -- dropping the memory clock from 162 MHz
to 108 MHz (`PLL_MS` 3 to 0) and re-initialising the SDRAM with `/sc?q` takes
edge raggedness from 0.19 to 0.50..0.62 against a control pair that agrees to
0.01. An earlier reading that bandwidth does not reach it was taken at a framing
far below the threshold, where there is slack to absorb the loss.

**Freezing the capture is not evidence that the fault is on the write side.**
A frozen capture displays perfectly at a framing that corrupts, and that was
read as exonerating the playback -- but freezing also removes the capture's
write traffic from the memory arbiter, which is precisely what a starved fetch
needs. The probe cannot tell the two apart.

## What it costs

**A default framing can be corrupt with no zoom involved.** 640x480@75 solves to
a capture of 1448 against a 1280 raster, which is 13% past the threshold, and
the picture is broken as the mode arrives.

Nothing bounds the horizontal capture: `Axis::maximumCapture()` has one call
site and it is `AxisVertical`.

## What the artefact looks like

Narrow columns at a regular pitch carry content from elsewhere in the line: by
autocorrelation of the per-column instability, a fundamental of **120 output px
with harmonics at 2x and 3x**, correlation 0.70. In a colour bar it shows as
wrong-coloured notches -- cyan inside yellow, green inside cyan, magenta inside
green -- and the mode caption appears doubled over itself. Chroma carries it
most visibly because 4:2:2 subsamples it. Well past the threshold it is bands of
displaced content rather than notches, and further still the whole frame is
horizontal streaks.

**A still under-reports it.** At the threshold the artefact is legible only
magnified; the colour-bar boundaries and the card's castellated border blocks
are where it shows first.

## Measuring it numerically

The artefact IS measurable, against an earlier reading that three metrics failed
to see it. What those missed was that it displaces edges by a fraction of a
pixel per row, so a whole-frame or fixed-band score reports the test card's
frequency wedge instead.

**Average a flat strip of the colour bars to one row per row of the photograph,
take the centroid of the strongest luma-gradient column near one bar boundary,
and report the spread of that position across rows.** It is self-anchored, so it
survives the picture moving as the framing changes -- which a fixed band does
not. On the bench panel:

| state | raggedness, px |
|---|---|
| clean | 0.09..0.34 |
| at the threshold | 0.5..0.8 |
| well past it | 1.0..3.5 |

Two shots at unchanged settings agree to within 0.1, so the separation is an
order of magnitude clear of the control.

## What it is not

Each measured against a live corrupt framing, with two shots at unchanged
settings as the control.

| refuted | reading |
|---|---|
| `CAP_REQ_OVER`, the bit that puts the line's last capture request in the horizontal blank | no effect clear of the control |
| `MEM_REQ_PB_RFF_CAP`, `MEM_REQ_WFF_CAP`, `MEM_REQ_PBH_RFFH` -- the capture/playback arbitration priorities | no effect clear of the control |
| `PB_CAP_OFFSET` overrun | capture 1761 is 441 fetch units against a stride of 476; the line fits, and both are derived from the line so the capture cannot exceed it |
| `PB_FETCH_NUM` | moves the picture slightly and never the artefact |
| the eleven-bit line counter | that wall is 2047 and no row above reaches it |
| a kept-count ceiling | 1800 is below `KeptCeiling` and corrupts at 1647; 1827 is above every threshold here and is clean at a 2400 raster |

## Two states that are not this

**A hand-set register can leave the picture shredded with every geometry
register reading correct for the framing.** `/sc?B`, a source mode round trip
and `/sc?~` all fail to clear it, and `/sc?~` from it leaves the sink reporting
no signal at all; `/restart` clears it and the autosaved framing survives.
Establish a clean picture at a small capture before judging any framing.

**Above a line of 2047 the capture window's start does not fit its register.**
At a held divider of 2200 the engine writes `IF_HB_ST2` with 2199 and the field
is eleven bits, so the chip holds 151 -- a window whose start is past its stop
-- and the picture is horizontal streaks. Nothing bounds the write to the field,
and no register reads wrong afterwards except the one that wrapped.
