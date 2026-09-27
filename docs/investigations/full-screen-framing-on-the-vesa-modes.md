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

Measured on the USB capture, 640x480@60 into 1080p at a default framing. The
divider varies between acquisitions on one build, and the capture and the
framing go with it, so this is the state the aperture walk below was taken at
rather than a figure to compare a later reading against:

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

The two in-scope modes, both read after the link has re-acquired, so neither
carries a stale latch:

| mode | black left \| right | vertical |
|---|---|---|
| 640x480@60 | 4 \| 0 | flush both edges |
| 800x600@60 | 12 \| 0 | flush both edges |

**AND THE RESIDUAL IS NOT IN OUR REGISTERS, because the two modes agree on where
the picture starts and disagree by 8 emitted pixels on the black.** The picture
starts at `VDS_HB_SP + 55 + 25m` plus the leading capture margin, and the
aperture is a fraction of a unit either side of it on both:

| | `VDS_HB_SP` | `VDS_HSCALE` | picture starts | `VDS_DIS_HB_SP` | black |
|---|---|---|---|---|---|
| 640x480@60 | 66 | 855 | 152.1 | 152 | 4 px |
| 800x600@60 | 64 | 810 | 151.9 | 151 | 12 px |

At about 1.38 emitted pixels per raster unit that predicts under two pixels on
each, so what differs between the modes is chosen after the analog frame --
which is where
[the-picture-position-is-latched-not-re-rolled.md](the-picture-position-is-latched-not-re-rolled.md)
localised it. A change to our blanking or our capture cannot close a gap our
blanking does not open.

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
- **Opening the capture earlier, which is worth 2 px on one mode and costs the
  far edge on another.** `IF_HB_SP2` two units below what the engine solves is
  the one change that moves the near edge at all -- 640x480@60 goes from 4 px to
  **2**, repeatably, with `VDS_HB_SP` and the divider held. It does nothing at
  800x600@60, where the left stays at 12 px through two units and four, and the
  green column at the RIGHT degrades from flush to one pixel and then leaves the
  frame. So it is not the fix: the 4 px and the 12 px are not the same fault,
  and only the smaller one is reachable from the capture.

  Note the granule. The horizontal capture position moves in twos -- the low bit
  of `IF_HB_SP2` does nothing -- so 259 and 258 are one state, and a lead of one
  unit buys between nothing and one granule depending on the picture start's
  parity.
- **`Axis::captureMargin`, which reaches that capture start and gives the 2 px
  straight back.** Two granules is the value that opens the capture where the
  hand-set register does, and on the acceptance path it leaves 640x480@60 at
  4 px and takes 800x600@60 from 12 px to **40**. The margin is not only the
  capture's: it is charged in `fitToRaster`, in `minimumCapture` and in
  `maximumCapture`, so raising it moves the scale, the zoom floor, the divider
  ceiling and the memory window's near edge together -- measured at 640x480@60,
  `VDS_HB_SP` 66 to 62 and the divider 1456 to 1454. **The capture start is
  worth nothing without the write origin staying put**, which is what separates
  the hand-set register from the constant that appears to do the same thing.

## The instrument, and four ways it has lied

Every one of these produced a confident wrong answer that reached a commit.

**A NEAR MARGIN CANNOT BE JUDGED WITH AUTOMATION FROZEN, because the transmitted
window's start is latched at link-up and a register poke does not re-lock it.**
This is the expensive one, because freezing is what every other geometry
measurement here requires, and the reading it gives is stable, repeatable and
wrong. Measured at 640x480@60, the pair the engine solves -- `IF_HB_SP2` 261 with
`VDS_HSCALE` 855 -- reads **no green column at the left at all** when written by
hand into a frozen acquisition, and reads it at **column 4** once the link has
re-acquired. The same two registers, two answers, one of them saying the
source's outermost pixel is off the screen when it is on it. A frozen ladder of
the capture's near edge therefore measures the distance from a stale latch: one
taken that way gave a clean monotonic slope and a single best rung, and the rung
it chose is the one a re-locked measurement refutes.

**THE RE-LOCK THAT WORKS IS A SOURCE MODE ROUND TRIP, AND THE
`PAD_SYNC_OUT_ENZ` TOGGLE IS NOT A SUBSTITUTE.** Both re-acquire the link, and
they land the picture in different places. Re-issuing the source's `MODE` with
automation frozen is the one to use: the source leaves and returns, the encoder
re-acquires, the engine cannot re-solve, and the hand-set register survives -- so
a single-register A/B becomes valid, which nothing else here affords. It agrees
with the acceptance path on both in-scope modes, reading the solve at 4 px and
12 px. The toggle does not: at 800x600@60 the same solve reads **12 \| 0** after
a mode round trip and **1 \| 8** after a toggle, the picture having moved about
ten pixels against the transmitted window with no register touched, which is
[the-shown-window-is-latched-at-lock.md](the-shown-window-is-latched-at-lock.md)
seen from the other end. Read back the register afterwards either way -- a
frozen unit is not an inert one.

The far margin is free of all of this, the two edges moving together.

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
