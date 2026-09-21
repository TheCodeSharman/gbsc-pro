# The vertical capture window is placed late

At 800x600@60 on `vga` the solve starts the vertical capture after the source's
active image begins, so the top of the picture is clipped. Nothing in a register
dump shows it: every value is self-consistent and `/geometry` reports a 600-line
capture of a 600-line mode.

**How much is not established.** The obvious measurement -- how much of the
card's top border band is on screen -- is contaminated twice over, and both
contaminations are larger than the effect. What stands is the direction and the
mechanism.

## What stands

Moving the vertical capture window earlier recovers picture at the top. With
both edges moved together so the captured height stays at 600 lines, the card's
top border band grows from 12 rows at the solved `IF_VB_SP` of 26 to about 21,
and the card's own displayed lines are what appear. The window is starting
inside the picture.

## Why the magnitude is not established

**The picture overruns the panel's painted area at the top.** The engine always
scales the capture to fill the output raster, so there is no framing at which
the picture's top edge is visible with blanking above it. Driven to `IF_VB_SP`
4 through the pads -- 22 lines before the standard's active start, with the
capture certainly inside the source's vertical blanking -- the card's top band
is still hard against the panel edge with no black above it. So a count of
visible band rows measures where the panel stops painting, not where the capture
starts.

**A source mode change re-lands the encoder.** Two sweeps of the same register
values either side of an 800x600 -> 640x480 -> 800x600 round trip disagree by
about four lines, with the picture placed four photo rows higher in the second.
That is the encoder choosing a landing, measured elsewhere on the pass-through
path, reaching a scaled measurement here.
[the-encoder-tunes-the-left-edge-in-pass-through.md](the-encoder-tunes-the-left-edge-in-pass-through.md)

Closing it needs a reference for the panel's painted area taken at the same
camera position, which on this bench comes from an 800x600 source in
pass-through, or an instrument that does not look through the panel at all.

## The raster match verifies nothing vertical

`SourceTiming::matching()` keys on the line count, the field rate and the
**horizontal** sync duty. Nothing it compares is vertical, so a source that
agrees on all three and spends its vertical blanking differently is placed from
the standard's numbers regardless.

The bench has one of each, and the difference is why a second mode cannot be
assumed comparable:

| mode | source `v_timings` | source active starts | table `vstart` |
|---|---|---|---|
| 800x600@60 | `4,23,0,600,0,1` | 27 | 27 |
| 640x480@60 | `2,32,0,480,0,11` | 34 | **35** |

`RetroScaler-Acorn.mdf`. The 640x480 mode is a line off DMT vertically while
matching it on total, rate and sync width, so the engine places its window one
line late before any lag term is applied.

## The lag constant

`VideoSourceLine::FrameLagLines` is **-1.5**, and it is the whole vertical
correction: the window is the standard's active start nudged by that much, which
is what puts `IF_VB_SP` at 26 against an active start of 27, and at 34 against
35.

Whether it describes a pipeline delay or absorbs a conversion cannot be settled
from what is measured here, and the question is live because both lag constants
are fitted -- `CaptureLagFraction` on two sources, `FrameLagLines` on one, with
its own comment saying a second line count is what would settle its form. A
constant fitted on one mode and a conversion that cancels there are
indistinguishable until two modes are compared on an instrument that survives
the comparison.

## A second clip the capture cannot reach

The card's bottom border band reads about 5 rows at every capture position
swept, including those that put more of the top band on screen. A window that
cannot move it is not what is cutting it, so the bottom is clipped on the output
side and is a separate fault from the top.

## What is refuted

**That the capture window is clamped to the published active region.** It is
not: the pads drive `ov` to 6 and `IF_VB_SP` to 4 with no clamp, well inside the
source's vertical blanking. Whatever prevents the picture being recovered, it is
not the framing control's range.

**That `VDS_VSCALE` can be set by hand to shrink the picture for a
measurement.** The playback fetch and the stride are solved with it, so writing
it alone tiles the picture into repeated fragments. Recovering needed the scale
put back and `PAD_SYNC_OUT_ENZ` toggled, which was found already at 1.

## The horizontal arithmetic disagrees too

```
/geometry   oh 328   eh 1190   ch 1607
DMT         active start 216/1056 -> 329    agrees
            active       800/1056 -> 1217   against eh 1190, 27 units short
```

Not established as a defect: a stored pan and zoom would account for it, and the
framing was not read back against a known default. Read the framing before
treating it as one.
