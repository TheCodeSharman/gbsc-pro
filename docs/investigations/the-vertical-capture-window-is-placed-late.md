# The vertical capture window was placed late, and the magnitude needed a one-line feature

At 800x600@60 on `vga` the solve started the vertical capture five to six source
lines after the source's active image began, so the top of the picture was cut
and the same amount of the source's blanking was shown at the bottom. Nothing in
a register dump showed it: every value was self-consistent and `/geometry`
reported a 600-line capture of a 600-line mode.

It is `VideoSourceLine::FrameLagUnits`, which was -1.5 and is -7.
[a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md](a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md)
carries the four readings that fix it and why it is a count of the counter's own
units rather than of source lines.

## Why the first attempt could not put a number on it

The obvious measurement -- how much of the card's top border band is on screen
-- is contaminated twice over, and both contaminations are larger than the
effect.

**The picture overruns the panel's painted area at the top.** The engine always
scales the capture to fill the output raster, so there is no framing at which
the picture's top edge is visible with blanking above it. Driven to `IF_VB_SP`
4 through the pads -- 22 lines before the standard's active start, with the
capture certainly inside the source's vertical blanking -- the card's top band
was still hard against the panel edge with no black above it. A count of visible
band rows measures where the panel stops painting, not where the capture starts.

**A source mode change re-lands the encoder.** Two sweeps of the same register
values either side of an 800x600 -> 640x480 -> 800x600 round trip disagreed by
about four lines, with the picture placed four photo rows higher in the second.
[the-encoder-tunes-the-left-edge-in-pass-through.md](the-encoder-tunes-the-left-edge-in-pass-through.md)

## What made it measurable

**A feature one source line tall, and a threshold rather than a position.**
`PATTERN CARD`'s `PROCframe` draws a one-pixel green line on the source's
outermost rows. It is present in the picture or it is not, so creeping the
capture window a unit at a time gives the counter unit the source's first
picture line arrives at, and neither contamination above can reach that: no
photo-column mapping is used, and nothing is compared across an acquisition.

Green is what makes it a threshold. Against the card's black outer band the line
is the only green in the frame, so a per-row `G - (R + B)/2` over a band of
columns separates it from the camera's own colour cast, which is uniformly
magenta here. Fully inside the window it reads 60 to 95 above the local median;
half clipped by the display aperture, about half that; outside, under 10.

**The two contaminations are still there and still do not matter.** The photo
rows the green line lands on drift with the panel's tilt and with where in the
frame it sits, and the calibration of photo rows against output lines is good in
the middle of the panel and wrong at the top -- fitted over output lines 120 to
520 it gives 0.883 photo rows per output line with residuals under 1.2, and
extrapolated to the picture's first row it disagrees with the green line's own
motion by 16%. The threshold does not use any of it.

## What is left

The capture now opens on the source's first picture line, and the first and last
source lines still cannot both be shown: they are 599 apart and the display
aperture shows about 598. That is the aperture's far-end guard, not the capture.
`docs/known-issues.md`.
