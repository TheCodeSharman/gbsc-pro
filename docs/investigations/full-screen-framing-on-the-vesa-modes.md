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

**SIX OF THE TEN ACCEPTANCE CASES PASS, AND EVERY FAILURE IS ONE SYMPTOM: THE
NEAR GREEN COLUMN IS NOT ON THE EMITTED FRAME AT ALL.** Five modes on each sync
type, `card_edges.py` for the verdict, on `vga` under `RetroScaler-Acorn.mdf`
with every stored entry forgotten and the allowance taken from each mode's own
capture grid. Margins are output pixels at the near and far edge, `-` an edge
the emitted frame does not carry, `out by` is the worst edge's distance from
the design, and `design` is what the design itself puts across:

| mode | emitted | across | whole | out by | down | whole | out by | design, across |
|---|---|---|---|---|---|---|---|---|
| 240x352@70 sep | 1280x1024 | - \| 10 | 0.00 \| 1.00 | 10.0 | 1 \| 3 | 0.97 \| 1.00 | 1.0 | -0.4 \| -0.1 |
| 320x256@50 sep | 1920x1080 | 4 \| 0 | 1.00 \| 0.55 | 3.8 | 3 \| 1 | 1.00 \| 0.99 | 3.3 | -1.4 \| +1.1 |
| 640x480@60 sep | 1920x1080 | - \| 12 | 0.00 \| 1.00 | 5.0 | 1 \| 4 | 0.50 \| 1.00 | 0.6 | -17.8 \| +17.0 |
| 640x512@50 sep | 1920x1080 | - \| 6 | 0.00 \| 1.00 | 6.0 | 1 \| 1 | 0.99 \| 1.00 | 1.1 | +0.3 \| +0.0 |
| 800x600@60 sep | 1920x1080 | 2 \| 0 | 0.48 \| 1.00 | 3.2 | 0 \| 1 | 1.00 \| 0.91 | 1.0 | -0.6 \| -0.1 |
| 240x352@70 cs | 1280x1024 | - \| 6 | 0.00 \| 1.00 | 6.0 | 6 \| - | 1.00 \| 0.00 | 6.0 | -0.4 \| -0.1 |
| 320x256@50 cs | 1920x1080 | 6 \| 1 | 1.00 \| 0.10 | 5.5 | 3 \| 1 | 1.00 \| 0.99 | 3.3 | -1.4 \| +1.1 |
| 640x480@60 cs | 1920x1080 | - \| 12 | 0.00 \| 1.00 | 5.0 | 3 \| 1 | 1.00 \| 0.56 | 4.5 | -17.8 \| +17.0 |
| 640x512@50 cs | 1920x1080 | - \| 6 | 0.00 \| 1.00 | 6.0 | 30 \| 79 | 0.99 \| 1.00 | 76.9 | +0.3 \| +0.0 |
| 800x600@60 cs | 1920x1080 | 2 \| 0 | 1.00 \| 0.11 | 2.1 | 0 \| 0 | 1.00 \| 1.00 | 1.8 | -0.6 \| -0.1 |

**640x480@60's near border is off the frame BY DESIGN**, DMT's raster being 6
source pixels later than the one the machine emits, so both its cases pass:
judging each edge against what the design puts there is what stops that
reading as a fault.

**WHAT FAILS IS 240x352@70 AND 640x512@50, ON BOTH SYNC TYPES, AND NEITHER HAS
A BY-DESIGN OFFSET.** Their near border is off the frame with black at the far
edge, which is a window placed late -- 6 output pixels at 640x512@50 is about
3 capture units and 10 at 240x352@70 is about 8. Both are beyond the granule,
so neither is the quantisation the rest of the set is at. 640x512@50 on
composite additionally rolls in 30 of 40 frames, which is the composite
instability in `../known-issues.md` and not a framing.

**240x352@70's NEAR EDGE MOVES BETWEEN ACQUISITIONS.** The 63-mode sweep reads
it at 1 column of black with 9 at the far edge, where the acceptance reads it
off the frame with 10 -- the same window, the sink's own sub-pixel landing
either side of a threshold.

**A ROW IS ONLY A DEFAULT FRAMING IF THE STORED ENTRY WAS FORGOTTEN.** An entry
is recalled on acquisition, `/sc?B` is queued for `loop()`, and
`gbs_unit.reset_framing()` can sample a framing that has not changed yet. Check
the console for `no framing stored` before believing any row.

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

**1024x768@60 WAS THE ENGINE MATCHING THE WRONG PUBLISHED RASTER, AND SO WAS
EVERY OTHER MODE'S ERROR.** The machine runs an Acorn monitor definition and
emits its timings, not the standard's: AKF60 states 1024x768@60 as
`128,36,60,1024,60,36` of 1344 at 65 MHz, picture at 224, where DMT 16 states
`136,160,0,1024,0,24`, picture at 296. One total, one clock, one frame, 71
pixels apart. `SourceTiming`'s Acorn array held AKF50 alone, which has no
1024x768 at all, so the source matched DMT's row and the window opened 71
source pixels late.

**A SOURCE IS NOT NON-CONFORMANT BECAUSE IT DISAGREES WITH THE RASTER THE
ENGINE CHOSE FOR IT.** That reading was published here and is withdrawn. The
divider measurement below stands and is what a wrong-raster match predicts --
the displacement is a fixed time in the source's own clock, because it is the
distance between two real rasters. `../bench-sources.md`.

### Every mode is emitting its monitor definition's raster, and the origin is sound

Measured on `vga` into 1920x1080, separate sync, `full_margins.py` at a full
framing so the source's own blanking is on the frame at both ends. Every span
read exact and every output raster was 1125 lines. `h lead` is where the card's
green frame actually starts, in the source's own pixels from the counter's
origin; the row is what the machine turned out to be running:

| mode | units/px | `h lead` | the raster it is emitting | error |
|---|---|---|---|---|
| 320x256@50 | 2.15 | 111.7 | AKF50, 110 | +1.7 |
| 320x480@60 | 3.18 | 68.5 | AKF50, 68 | +0.5 |
| 360x480@60 | 2.39 | 128.9 | AKF50, 126 | +2.9 |
| 640x480@60 | 1.52 | 138.4 | AKF50, 138 | +0.4 |
| 720x480@60 | 1.42 | 121.5 | CEA 2/3, 122 | -0.5 |
| 640x512@60 | 1.67 | 172.9 | AKF91, 170 | +2.9 |
| 800x600@60 | 1.27 | 216.4 | both state 216 | +0.4 |
| 1280x480@60 | 0.80 | 276.7 | AKF50, 276 | +0.7 |
| 1280x720@60 | 0.88 | 258.1 | CEA 4, 260 | -1.9 |
| 1024x768@60 | 1.07 | 225.0 | AKF60, 224 | +1.0 |

**THE ERROR AGAINST THE RIGHT RASTER IS +0.8 SOURCE PIXELS, MEAN, OVER A SPREAD
OF 1.5** -- across stated leads of 68 to 276, line totals of 400 to 1650,
dividers of 1212 to 2200, densities of 0.80 to 3.18 units per source pixel, and
the line-doubled path as well as the undoubled one. So **the counter's origin is
correct and there is no displacement for a correction constant to take out**.
Every figure that read as an origin error was the engine placing the window from
a raster the machine was not running.

**WHAT MADE IT LOOK LIKE AN ENGINE FAULT IS THAT TWO MODES READ MUCH WORSE THAN
THE REST.** 640x480@60 read -5.6 against DMT's 144 and 1024x768@60 read -71.0
against DMT's 296, and those are exactly the two modes in the set where an
Acorn entry exists and differs from the standard's. The eight that looked fine
were the ones where the two authorities agree or where only one states the mode
at all.

**A MEASURED DUTY CANNOT SEPARATE TWO ACORN MODES IN ONE RESOLUTION FAMILY.**
Acorn spends a near-constant share of the line on sync across a family, so
360x480@60's 12.030% and 640x480@60's 11.750% sit 0.28 points apart against a
measurement worth 0.30. The whole family is one key, which is what the
`-- one key with` notes in `SourceTiming`'s Acorn array record. 360x480's rows
are gone for a sharper reason -- its duty is nearer DMT's 640x480@60 than
AKF50's own is, so the only source that could reach it was one running the
standard, framed 45 pixels out. `../known-issues.md`.

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

## What the error turned out to be, and what is left

**The scale is not in it and neither is the output chain.** The display window
is 87.1% of the line at the 1916-unit 50 Hz raster and 87.2% at the 1596-unit
60 Hz one -- CEA's own active fraction -- and `produced` matches it to within
1 px on both axes at both. `IF_HB_SP2` is where the published raster says, 228,
274 and 316 against 229, 274 and 317. So the origin is the whole of it.

**THE ORIGIN IS SOUND AND NO CORRECTION CONSTANT IS WANTED**, because there is
no displacement left to take out: ten modes read within +0.8 source pixels of
the raster they are emitting, mean, over a spread of 1.5, and what the framing
error was is the engine matching the wrong published raster. "Every mode is
emitting its monitor definition's raster" above has the measurements.

What shipped is two things. The generated monitor definition carries no VESA DMT
set, so it is the file the machine actually runs -- that is the whole of why
1024x768@60 read 71 pixels out, and `../bench-sources.md` has it. And the
firmware carries the Acorn definitions past AKF50, which answer where no
standard states a mode or states it with a different polarity pair.
**The standard keeps priority where one states the same resolution**, and the
offset that costs is known per mode rather than guessed at.

Measured after flashing, margins across with stored entries forgotten:
1024x768@60 from 136 output pixels of black at the far edge to **1**, held
across the revert because its polarity pair reaches the Acorn row. 640x480@60
sits at 12, which is DMT answering by priority. 800x600@60 is 1 and unchanged,
both authorities putting its picture at 216.

### The engine is centred on its design, and what is left is the grid

**THE DEFAULT FRAMING IS AT ITS QUANTISATION FLOOR, SO THERE IS NO TERM LEFT TO
DERIVE.** 63 source modes swept at 1080p and judged off the emitted frame,
`framing_residuals.py`, pooling each axis over the states whose matched row on
THAT axis is the raster the machine emits -- against that row's own stated
picture area, and the design's own offset taken off first:

| axis | states | displacement | sd | range | per edge, capture units |
|---|---|---|---|---|---|
| horizontal | 27 | **+0.05 source px** | 1.61 | -6.04 .. +2.45 | +1.39, sd 1.63 |
| vertical | 21 | **-0.04 lines** | 0.79 | -2.21 .. +1.06 | +0.16, sd 0.72 |

**THE AXES ARE POOLED SEPARATELY BECAUSE THE TWO AUTHORITIES CAN DISAGREE ON
ONE OF THEM**: 1024x768@60 is 71 source pixels out across and half a line down,
so one verdict for both axes throws away every such mode's good axis. Pooling
the vertical over the horizontal filter is what reads it as -0.19.

Centred on both axes. The spread is 2 to 3 ADC samples, which is the capture
granule plus the retime origin's own rounding, and **nothing predicts it**:
sync duty, sampling density, line doubling, oversampling ratio, field rate,
divider, sync polarity, magnification and the sync width's own measurement
error were each regressed against it and the best R2 is 0.15. Two readings of
one `SourceKey` differ by 1.1 samples, so about a sample of the spread is the
instrument.

**THE VERTICAL TERM THE PLAN ASKED FOR IS REFUTED. Do not derive a vertical
constant.** The `v lead` figures that licensed it -- -5.1, -5.1, -6.1, -1.2,
-3.7, -3.2, -5.1 and 0.0 lines, mean -3.7 and never late -- come from
`full_margins.py` at FULL framing, whose absolute lead carries its own model.
Measured off the emitted frame at the default framing the same quantity is
-0.04 lines over 21 modes. The two instruments disagree by about three lines
and the emitted frame is the one the acceptance reads.

**A refit of `SourceVideoDelayPixels` is rejected.** The undoubled subset alone
trends with sampling density -- 23 states, slope +1.46 samples per unit of
pixelSamples, R2 0.51 -- and the four doubled states want the opposite sign, so
a per-doubling refit is a two-parameter fit on four points at one end. A good
fit is not evidence the quantities are what you think.

**`SourceTiming::hsyncExtent()` is unused, and replacing the measured duty with
it has no deficit to fix.** It states a published raster's own sync width
exactly, which is the obvious replacement for the measured duty in
`retimeStopFor()`; the measurement above says the measured duty is costing
nothing. Noted so it is not reached for twice.

**The sync width the engine uses is 2.6 samples narrower than the mode file
states**, mean, sd 0.66, where the raw `STATUS_SYNC_PROC_HLOW_LEN`
reads about 1.3 samples WIDER. That is `InvertedPulseWidthSamples` = 5
overshooting on a high-active source. It is nearly constant, so it is not what
the spread is, and it moves the origin by the same 2.6 samples on every mode --
which the centred mean says is already absorbed.

**The one outlier is 896x352@60**, a doubled mode at 2.00 samples a pixel whose
matched row is its own, reading -6.04 source px. It is four times the next
worst and nothing explains it.

### A wholeness verdict near its threshold is unresolved

**`whole` IS REPEATABLE WITHIN AN ACQUISITION AND NOT ACROSS ONE**, so it
carries about 0.3 of the source pixel that no framing change can remove.
Twelve clips over three source-mode re-locks at 320x256@50 give
`4 (4|0) whole 1.00|0.58` every time, to the pixel; the same state reads 0.84
at the far edge after a different sequence, and 800x600@60 reads `0.49|1.00` on
separate sync and `1.00|0.12` on composite with identical registers and
identical margins. That is the sink's own sub-pixel landing, which is
[the-transmitted-window.md](the-transmitted-window.md) again. **Take the
margins as the measurement**, and read a wholeness figure within about 0.3 of
its threshold as saying nothing either way.

### The acceptance is stated on that grid, and what it leaves

**THE GOAL IS NOW IN CAPTURE UNITS.** `card_edges.allowance_for()` takes
`Axis::captureGranularity` out of the C++ and magnifies it by the window's own
extent against the frame it became, so the allowance follows the mode's
sampling density: one granule is 3.8 emitted px at 800x600@60 and 5.6 at
320x256@50. The margin and the surviving fraction of the border are ONE budget
per edge, because they cannot both be met at a fixed size -- 3 emitted px of
black and 0.25 source px of clip are the same allowance only where one source
pixel is twelve -- and an edge off the frame fails whatever the allowance,
that being the goal itself.

What is left is the two modes in the table above, on both sync types, whose
window is placed 3 and 8 capture units late with no by-design offset to
account for it. Both are beyond the granule, so neither is the quantisation
the rest of the set is at, and **the question is what those two modes have
that the others do not** -- 240x352@70 is the shortest source line in the file
and the only case emitting at 1280x1024, and 640x512@50 is the only undoubled
50 Hz key.

**1280x720@60 still cannot pass**: it is CEA 4 and the `Cea` array holds only
480p and 576p, so it matches nothing and falls back to the whole capturable
region. Adding the row is what reaches it.

**A margin is the only lever that reaches a one-pixel border and every one of
them is spent.** `Axis::captureMargin` is charged in `fitToRaster`, in
`minimumCapture` and in `maximumCapture` as well as at the capture start, so
raising it moves the scale, the zoom floor, the divider ceiling and the memory
window together -- which is why the 2 px it buys at 640x480@60 comes back as 40
at 800x600@60. `TransmittedWindowDelayPx` is not a lever either: it slides
`activeStop` and the divider ceiling is taken off it.

Two things are NOT part of this and must not be folded into it: composite's
horizontal repeat at 800x600@60 and 1024x768@60, and the whole-capturable-region
default an unpublished source falls back to. Both are in `docs/known-issues.md`.

## The instrument, and the ways it has lied

Every one of these produced a confident wrong answer that reached a commit.

**THE DECOMPOSITION'S ABSOLUTE TERMS CARRY TWO MODEL ERRORS**, the write-start
constants and the encoder window, so `dCapture` is not a source-pixel
measurement: it reads -4.0 source px at 320x256@50 where the emitted frame says
-0.7. **Use the black counted at the emitted edges** -- `black_cols` -- for
anything at the one-pixel level, and the decomposition for attribution only.

**A MIRRORED CONSTANT WITH A TEST THAT COMPARES IT TO ITSELF IS WORSE THAN NO
TEST.** `framing_decomposition`'s copy of `TransmittedWindowDelayPx` sat at 20
against the firmware's 6 while the test asserted the modelled window against
the copy, so the drift was invisible and two other tests had taken the stale
answer as an absolute. Every `dModelApplied` verdict in a sweep taken before
`firmware_constants.py` parsed the C++ is 14 raster units out.

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
