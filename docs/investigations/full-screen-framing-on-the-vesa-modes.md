# Full screen framing on the VESA modes: the goal, and what is measured against it

## The goal

**A default framing puts the source's outermost pixel on the emitted frame's
outermost pixel.** `PATTERN CARD` draws a one-pixel green frame on exactly those
pixels, so the goal is readable off one capture:

| edge | required |
|---|---|
| left | the green column is flush, column 0 |
| top | the green row is flush, row 0 |
| bottom | the green row is flush, the last row |
| right | almost flush -- one pixel of slack is allowed for the width parity the HSCALE corruption workaround carries |

Nothing about this is a judgement call, and it needs no reference frame: the
capture IS the emitted frame. `tools/gbsc-pro-hwtest/card_edges.py` is the
acceptance test and states the same thing in code.

**The scope is 640x480@60 and 800x600@60 first.** 1024x768@60 and 1280x1024@60
are known worse and are a separate problem; measuring them alongside has
repeatedly turned one fault into four.

## Where it stands

Measured on the USB capture, 640x480@60 into 1080p at a default framing:

```
capture   278 .. 1521  of 1551      exactly the source's active video
scale     910                       produced 1398.7
raster    1600                      aperture 160 .. 1555  (1395)
emitted   10 columns of black at the left, none at the right
```

**The capture window is right and is not the fault.** VESA DMT puts 640x480@60's
active video at 0.180 .. 0.980 of the line; the engine captures 278/1551 = 0.179
to 1521/1551 = 0.981. The same holds on every DMT mode checked, 800x600 through
1280x1024.

**The black is at the output.** Walking the aperture's near edge with automation
frozen moves it at 1.375 emitted pixels per raster unit -- `VDS_DIS_HB_SP` 160
gives 10 columns and 168 gives 21 -- so it extrapolates to zero at **152.7**,
about 7 units before the aperture opens. What the chain carries therefore starts
before our blanking does, and our own blanking is what turns that strip black.

The direction of that walk is the whole diagnosis, because both candidates
darken the left when the aperture moves EARLIER and only one of them darkens it
when the aperture moves LATER:

| if the black were | aperture later |
|---|---|
| the chain carrying window our aperture blanks | grows -- **observed** |
| the aperture opening before the picture starts | shrinks to nothing, then clips picture |

## What is ruled out

- **The capture window.** It is the source's active video to a unit on every
  DMT mode measured, taken inside one acquisition.
- **`PB_FETCH_NUM` and `PB_CAP_OFFSET`.** Both are what
  [playback-fetch-and-stride.md](playback-fetch-and-stride.md) requires: the
  fetch is `ceil(capture / 4)` and the stride is `ceil(line / 4)`, sized for the
  whole line rather than the framing.
- **A delay constant in the solve.** Moving the whole solve, write origin and
  aperture together, is what `OutputMode::TransmittedWindowDelayPx` does, and
  +8, +16 and -8 all leave the emitted margins where +0 does.
  [the-transmitted-window-is-latched-from-our-blanking.md](the-transmitted-window-is-latched-from-our-blanking.md).

## The instrument, and three ways it has lied

Every one of these produced a confident wrong answer that reached a commit.

**Judge a green column by the mean hue down the line, never by a count of rows.**
The frame is one source pixel wide, so at the edges it lands on a fraction of an
output pixel and blends with the ring beside it -- at 800x600@60 the left column
cleared a per-row test on 70 rows of 1080 while the right cleared it on all of
them. A row count reports the left edge missing and the window shifted when both
edges are on screen.

**Read a clip, never one frame.** The card's outermost ring flashes twice a
second and the green frame is drawn over it in both phases. In the yellow phase
the green smears into it and falls under any hue test, so a single capture
reports the edge missing.

**Do not find an edge by the first lit column.** The card's concentric bands put
whole black columns inside the picture, so a brightness test runs past a
blanking edge to the next bright band and reads tens of pixels late -- which
arrives as a jump in an otherwise straight line rather than as an artefact.
Difference against the previous step and take the strip whose width the step
size predicts.

## Two things that cost a session each and are not the fault

**The beat on a dither.** At 1280x1024@60 the grey bar carries a strong vertical
beat, period 4.07 emitted pixels. `PLLAD_MD` is 1464 against a 1688-pixel source
line with both decimators bypassed, so the line is sampled at 0.867 samples per
source pixel with no oversampling: a one-pixel dither at 0.5 cycles/px aliases
to `0.867 - 0.5 = 0.367` cycles/px, which is 2.73 source pixels and 4.09 emitted
-- the measured 4.07. It is sampling density, it is not the frame buffer, and it
belongs to the two modes that are out of scope above.

**The unit drops off the network.** ICMP stops while HTTP keeps answering, and
register reads return nothing intermittently, which reads as a dead route or a
wedged loop. `docs/known-issues.md` carries the dropout.
