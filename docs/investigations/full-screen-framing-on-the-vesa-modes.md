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

**No mode passes.** Measured 2026-10-22 on `vga` into 1920x1080 under
`RetroScaler-Acorn.mdf`, `card_edges.py` for the verdict and `full_margins.py`
for the decomposition. Margins are output pixels at the near and far edge, `-`
an edge the emitted frame does not carry:

| mode | across | down |
|---|---|---|
| 320x256@50 | 4 \| 0 | 3 \| 1 |
| 640x512@50 | - \| 6 | 1 \| 1 |
| 640x480@60 | - \| 14 | 1 \| 4 |
| 800x600@60 | - \| 0 | 0 \| 1 |
| 1024x768@60 | - \| 136 | 1 \| 1 |

**320x256@50's row is not a default framing.** Its key matches no published
raster, so it was answered by the stored entry the table still held --
`/sc?B` is queued for `loop()` and `gbs_unit.reset_framing()` can sample a
framing that has not changed yet. The four DMT rows are defaults, because there
the published active region IS the default. Check the console for
`no framing stored` before believing any row of this table again.

### The error is the ORIGIN, and both symptoms are one number

**THE SPAN IS EXACT ON EVERY MODE**, so nothing is narrow and there is no scale
to chase: `full_margins.py` reads the card's green frame as 640.0/640,
800.0/800 and 1024.0/1024 source pixels. What is wrong is where the window
OPENS, and the source's active video does not arrive where the published raster
states it does:

| mode | `h lead` measured / file | error, source px | far margin predicted | measured |
|---|---|---|---|---|
| 640x480@60 | 140.1 / 144 | -3.9 | 11.7 | 14 |
| 800x600@60 | 216.4 / 216 | +0.4 | ~1 | 0 |
| 1024x768@60 | 225.1 / 296 | **-70.9** | 133 | 136 |

The window opens at the stated start, so the early part of the framebuffer is
before it -- the card's left green column among it -- and the same amount of the
source's own blanking is shown at the right. **The missing border and the black
at the far edge are the same error twice**: the far margin is the lead error
multiplied by the emitted pixels one source pixel occupies.

So a picture "6 px NARROW" at 800x600@60 and "41 px narrow" at 1024x768@60 are
one displacement each and not a magnification error. The scale account below is
superseded.

**1024x768@60 IS THE SOURCE, AND IT IS THE ONLY MODE THAT IS.** Its line total
and frame match the file exactly, 1343.9 against 1344 and 806 against 806, and
it starts its active video 71 source pixels before the stated 296 of
`h_timings:136,160,0,1024,0,24`. The next section is the measurement; what
follows from it is that the engine's origin is sound and this mode's published
raster is not what the machine emits, so **no firmware constant reaches it** and
a framing measurement taken on it is evidence about the RISC PC.
`../bench-sources.md`.

### The origin is sound on every mode but one

**EIGHT MODES AT 60 Hz READ WITHIN ±6 SOURCE PIXELS OF THEIR STATED LEAD, AND
THERE IS NO SYSTEMATIC ERROR TO CORRECT.** Measured on `vga` into 1920x1080
under `RetroScaler-Acorn.mdf`, separate sync, `full_margins.py` at a full
framing so the source's own blanking is on the frame at both ends. Every span
read exact and every output raster was 1125 lines:

| mode | line total | units/px | `h lead` / file | error |
|---|---|---|---|---|
| 320x480@60 | 400 | 3.18 | 68.5 / 68 | +0.5 |
| 360x480@60 | 532 | 2.39 | 128.9 / 126 | +2.9 |
| 640x480@60 | 800 | 1.52 | 138.4 / 144 | -5.6 |
| 720x480@60 | 858 | 1.42 | 121.5 / 122 | -0.5 |
| 640x512@60 | 870 | 1.67 | 172.9 / 170 | +2.9 |
| 800x600@60 | 1056 | 1.27 | 216.4 / 216 | +0.4 |
| 1280x480@60 | 1600 | 0.80 | 276.7 / 276 | +0.7 |
| 1280x720@60 | 1650 | 0.88 | 258.1 / 260 | -1.9 |
| **1024x768@60** | **1344** | **1.07** | **225.0 / 296** | **-71.0** |

The eight span stated leads of 68 to 276, line totals of 400 to 1650, dividers
of 1212 to 1444 and sampling densities of 0.80 to 3.18 units per source pixel,
and their errors are scatter about zero -- mean +0.0, sd 2.9 px -- of the same
size as the sync-width measurement the origin is placed from. **The
LINE-DOUBLED path reads the same**: the bench's own 320x256@50 at `PLLAD_MD`
2200 doubled reads `h lead` 111.7 against 110, +1.7 px, with `v lead` -1.0, so
the result is not a property of the undoubled modes the table happens to hold.

**So a single correction constant has nothing to correct**, and the acceptance
failures on these modes are that scatter against a border one source pixel wide
rather than a displacement a constant can take out.

**WHAT SEPARATES THE SOURCE FROM A MISPLACED ORIGIN IS THE DIVIDER, because a
source-side error is a constant in the SOURCE's own pixels and an origin error
is a constant in ADC SAMPLES.** `/sampleclock?hold=` pins the divider and the
whole engine solves around it, so each row is a complete solve rather than a
hand-set subset. One source mode, 1024x768@60, four dividers:

| held `PLLAD_MD` | units/px | error, source px | error, ADC samples | read with |
|---|---|---|---|---|
| 1200 | 0.893 | -70.7 | -63.2 | `full_margins` |
| 1440 (the engine's) | 1.072 | -71.0 | -76.1 | `full_margins` |
| 1440 | 1.072 | -73.3 | -78.6 | `counter_origin` |
| 1550 | 1.154 | -72.9 | -84.1 | `full_margins` |
| 1700 | 1.265 | -75.3 | -95.3 | `counter_origin` |

The source-pixel column is flat to 4.6 px across a 42% change of divider, and
to about 2 px within each instrument taken alone; the sample column tracks the
divider, ×1.51 against ×1.42. **A constant in ADC samples is disconfirmed** --
so the displacement is a fixed time in the source's own clock, about 1.13 µs at
its 65 MHz, and not a counter placed wrong.

`counter_origin.py`, which carries no anchor at all, agrees with the anchored
reading where the two overlap: -73.3 px against `full_margins.py`'s -71.0 at
the engine's own divider. **So the dongle anchor is not in it either.** The two
instruments overlap at that divider alone -- `counter_origin.py`'s creep is
flaky at this mode's density, landing at 1440 and 1700 and refusing 1200, 1550,
1620 and 1850 on its own `NEVER FULL` and frame-not-found checks, where
`full_margins.py` reads the pair from one capture and landed at 1200, 1440 and
1550. Each table row above is one instrument's reading, not both.

Nothing in the sync processor is mode-specific. Read in one pass at 800x600@60
and at 1024x768@60, all 127 fields of `SP_*`, `STATUS_SYNC_PROC_*`, `IF_H*`,
`IF_V*`, `PLLAD_*` and the decimators are identical but for the values derived
from the divider and the count -- `PLLAD_MD`, `IF_HSYNC_RST`, `IF_HB_SP2`,
`IF_HB_ST2`, `IF_VB_ST`, `SP_RT_HS_SP`, `SP_H_CST_SP`, `SP_H_PULSE_IGNOR`,
`SP_CS_CLP_ST`/`SP`, `PLLAD_FS` and the three status counts.

### The state the aperture walk below was taken at

Measured on the USB capture, 640x480@60 into 1080p at a default framing, with
`OutputMode::TransmittedWindowDelayPx` at 3 and the left green column ON the
frame -- which is not the state above. The
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

**640x480@60 MET THE GOAL AT `TransmittedWindowDelayPx` 3 AND DOES NOT NOW.**
The constant is 6 and banded by field rate, and the same mode reads its left
column off the frame with 14 at the right. Nothing establishes which change
moved it, and the delay is a suspect rather than a lever: it slides
`activeStop`, and the divider ceiling is taken off it.

**640x480@60 MEETS THE GOAL FROM THE ENGINE'S OWN SOLVE**, with no hand-set
register: `card_edges.py` reports `across 0 (0|0) down 0 (0|0)`, and the emitted
frame carries no blank column at any edge. Two placement constants get it there,
`OutputMode::HsyncStartPx` at 13 and `OutputMode::TransmittedWindowDelayPx` at 3.

| mode | blank left \| right | |
|---|---|---|
| 640x480@60 | 0 \| 0 | passes |
| 800x600@60 | 0 \| 6 | the picture is 6 px NARROW |
| 1024x768@60 | 38 \| 3 | the picture is 41 px narrow |

**SUPERSEDED BY THE SPAN MEASUREMENT ABOVE, WHICH READS EVERY MODE'S ACTIVE
COUNT EXACT.** What follows was the reading when the deficit was taken from the
emitted margins alone, which cannot tell a short picture from a displaced one.

**THE PLACEMENT CONSTANTS GENERALISE AND THE SPAN DOES NOT.** One value of
`HsyncStartPx` brought the near edge flush on 640x480@60 and on 800x600@60, the
latter from 12 blank columns -- so a per-mode offset is not needed, which is what
a single constant was suspected of hiding. What remains on the two larger modes
is a picture NARROWER than the frame rather than misplaced, and the deficit grows
with the resolution: 0.3% at 800x600@60 and 2.1% at 1024x768@60. That is a scale
question and it is not what these two constants reach.

**AND THE BORDER IS SMEARED BY THE INTERPOLATOR, so a flush edge does not look
like a hard line.** At 640x480@60 the green dominance runs 43, 43, 73, 73, 51, 49
across the first six columns -- a one-source-pixel feature spread over about six
emitted ones with its peak two or three in. It measures as present at column 0 and
reads on a panel as absent, which is worth knowing before chasing the last pixel.

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
[the-transmitted-window.md](the-transmitted-window.md)
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
  [the-transmitted-window.md](the-transmitted-window.md).
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
- **Anything that lets a placement constant reach the SAMPLING clock.** The
  transmitted window's delay slides the whole window, so it slides `activeStop`,
  and `VideoPath::dividerCeilingForOutput()` sized the divider from that -- which
  made the constant untunable rather than merely coupled. Moving it two samples
  moved `PLLAD_MD` two, which moved the capture, the scale, the stride and the
  write origin, so the aperture that was correct before the change was wrong
  after it. Each attempt to correct it started the loop again. The ceiling takes
  the delay back off the published raster's far edge now: the delay is a
  compensation for a chain that starts carrying video late, and it says nothing
  about how much capture the raster can hold. It costs the divider the delay's
  own width -- 1454 to 1444 here -- and buys a constant that can be measured.
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

## What remains, and the order it has to be done in

**The scale is not in it and neither is the output chain.** The display window
is 87.1% of the line at the 1916-unit 50 Hz raster and 87.2% at the 1596-unit
60 Hz one -- CEA's own active fraction -- and `produced` matches it to within
1 px on both axes at both. `IF_HB_SP2` is where the published raster says, 228,
274 and 316 against 229, 274 and 317. So the origin is the whole of it, and the
work is in this order because each step decides what the next one may assume.

**STEPS 1 AND 2 ARE DONE, AND THEY REMOVE STEP 3 AS IT WAS WRITTEN.** The
origin is sound: eight modes at 60 Hz read within ±6 source pixels of their
stated lead with no systematic error, 1024x768@60 is the source, and both
results are in "The origin is sound on every mode but one" above with the
measurements behind them. **There is no displacement for a correction constant
to take out**, so a step that puts one where the origin is derived would be
fitting a constant to scatter.

1. **Find what the acceptance is actually failing on, now that the origin is
   not it.** `card_edges.py` reads the near edge off the frame on modes whose
   lead is within 2 source pixels of its stated value, so the border is being
   lost to something other than where the window opens -- the one source pixel
   itself, at a sampling density that straddles it, is the first candidate, and
   the instrument's own `whole` reading is what sizes it. Measure before
   reaching for a lever.
2. **Then the vertical**, which is a different shape from the horizontal and
   must not be merged with it. `v lead` reads -5.1, -5.1, -6.1, -1.2, -3.7,
   -3.2, -5.1 and 0.0 lines across the eight conformant modes above, in the
   table's own order, so unlike the horizontal it IS systematically early --
   mean -3.7 lines, and never late. That is a term worth deriving, and a
   measurement spanning both axes is what licenses one constant for both.
   1024x768@60 reads -0.5 there, so **the source's horizontal displacement does
   not reach its vertical** and the mode is usable for the vertical question.
3. **A margin is the only lever that reaches a one-pixel border, and it is
   charged four times over.** `Axis::captureMargin` is charged in `fitToRaster`,
   in `minimumCapture` and in `maximumCapture` as well as at the capture start,
   so raising it moves the scale, the zoom floor, the divider ceiling and the
   memory window together -- which is why the 2 px it buys at 640x480@60 came
   back as 40 at 800x600@60. **`TransmittedWindowDelayPx` is not a lever
   either**: it slides `activeStop` and the divider ceiling is taken off it.
   Pin whatever lands with a host test through the fake-Wire seam;
   `card_edges.py` needs the bench and the suite does not.
4. **Re-run the acceptance on both sync types**, and confirm the entry really
   was forgotten before reading any row -- the console says `no framing stored`.
   **1024x768@60 is out of scope for it**: the source does not emit the raster
   the file publishes, so the mode cannot pass and its row is evidence about
   the RISC PC.

Two things are NOT part of this and must not be folded into it: composite's
horizontal repeat at 800x600@60 and 1024x768@60, and the whole-capturable-region
default an unpublished source falls back to. Both are in `docs/known-issues.md`.

## The instrument, and five ways it has lied

Every one of these produced a confident wrong answer that reached a commit.

**`full_margins.py` IS A 1080p INSTRUMENT, AND IT SAYS NOTHING WHEN IT IS NOT
AT 1080p.** `ENCODER_TOTAL` is hardcoded 2200, so a dongle column is a known
fraction of a scaler pixel only while the sink is locked to the 1125-line
raster. Above about 62 Hz of source field rate the engine falls back to 1024p
to stay under the clock ceiling and the raster becomes 1066 lines -- measured
1446x1066 at 1024x768@70 and 1344x1066 at @75 -- and every figure the tool
prints then comes off the wrong ruler. It reports it: the green pair's `span
off` read +237.7 px and +207.3 px on those two, and `v active` read 809.9
against 768. **Read `out` and the span residual before reading a margin**, and
compare only within one output raster. `../known-issues.md` carries the
unrecorded fallback itself.

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
[the-transmitted-window.md](the-transmitted-window.md)
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

## Widening the output hsync pulse beyond the standard buys nothing

`OutputMode::HsyncStartPx` offsets the pulse and its stop is the start plus the
standard's own duration, so the width never shortens. Whether a wider pulse
helps was measured directly, at three widths over 120 frames each:

| pulse width, units | emitted edge | spread | frames lost |
|---|---|---|---|
| 30 | 44.50 px | 0.1 px | none |
| 32 | 44.50 px | 0.1 px | none |
| 36 | 44.50 px | 0.1 px | none |

So the sink neither moves its window nor drops a frame for a pulse wider than
the standard states, and the width stays the standard's duration.
