# The transmitted window: where it starts, how wide it is, and what places the picture

What the chain carries out of our raster is a window with two quantities, and
they are not the same kind of thing.

- Its **WIDTH** is the output mode's own active fraction of the line,
  `T x carriedPx / totalPx`. Confirmed on every measurement taken, across six
  output modes, rasters from 1134 to 2402, both field rates and both inputs.
- Its **NEAR EDGE** is the standard's blanking as a **fraction** of whatever
  line it receives, plus a residual of about **6 samples**. Not a duration, not
  a fixed count, not a per-raster table.

`OutputMode::solve()` states both that way, and
`OutputMode::TransmittedWindowDelayPx` is the residual.

**Where the picture then lands on the panel is chosen DOWNSTREAM of the board
and latched when the link acquires.** It is not in any register, and a register
poke cannot test it.

## The near edge is a fraction

Six openings, read off the USB HDMI capture with the aperture held AWAY from the
opening, scored against `T x (syncPx + backPorchPx) / totalPx`:

| output | T | field Hz | opens | fraction | residual |
|---|---|---|---|---|---|
| 1080p | 1590 | 60.35 | 145.00 | 138.76 | **+6.24** |
| 1080p | 1600 | 60.02 | 145.19 | 139.64 | **+5.55** |
| 1080p | 1602 | 59.94 | 146.05 | 139.81 | **+6.24** |
| 1080p | 1916 | 50.08 | 173.78 | 167.21 | **+6.57** |
| 1080p | 1916 | 50.08 | 172.50 | 167.21 | **+5.29** |
| 720p | 2402 | 59.94 | 384.70 | 378.50 | **+6.20** |

Two inputs, two output standards, both field rates. Mean 6.01, spread 1.28.

**The display clock is pinned near `EngineCeilingHz` on every raster**, so a
duration converts to nearly the same sample count however long the line is: a
duration base solves 161 at a 1916-unit raster and 161 at a 1590-unit one, where
the sink's own start moves 27 samples between the two.

What it fixes, on `vga` at 320x256@50 into 1080p with `VDS_Y_OFST` lifting the
picture so the source's border cannot be counted as ours:

| edge | duration base | fraction base |
|---|---|---|
| right | **14 columns ours** | **0** |
| left | 0 | 2 |
| top, bottom | 0, 0 | 0, 0 |

Validated as a pure register change first, all four window registers moved
together with `PAD_SYNC_OUT_ENZ` dropped and driven at each step so the sink
placed its window for the state under test:

| delta | aperture | left | right |
|---|---|---|---|
| +6 | 167..1837 | 0 | 8 |
| +10 | 171..1841 | 0 | 3 |
| **+12** | **173..1843** | **1** | **1** |
| +14 | 175..1845 | 3 | 0 |

The engine solves 174, one unit past the measured optimum, which is the aperture
ceiling onto the unit the write filled.

## The width is the mode's fraction

One output mode across six rasters, driving the source's field rate so the
raster is the only thing moving:

| raster | window width | the mode's fraction |
|---|---|---|
| 1914 | 1671.79 | 1672 |
| 1706 | 1489.96 | 1488 |
| 1592 | 1390.99 | 1388 |
| 1370 | 1197.99 | 1195 |
| 1330 | 1163.01 | 1159 |
| 1134 | 991.26 | 988 |

Within 4 units of 1200 to 1700, and a couple of those are the instrument: the
changed strip is bounded where the difference clears a threshold, so each edge
reads a pixel wide and the span comes back that much long.

**The two SD modes carry a different fraction, and it is not a defect.** The
sink names the format it receives, and on those it does not name an SD one:

| our raster | the sink reports | measured fraction | DMT's own |
|---|---|---|---|
| 525 lines at 50 Hz | **640 x 480 @ 50 Hz** | 0.804 | 0.800 |
| 625 lines at 50 Hz | **800 x 600 @ 50 Hz** | 0.786 | 0.781 |

The encoder matches the line COUNT against a table, finds VESA DMT's 640x480 and
800x600, and transmits those. The vertical follows the same table: the chain
paints raster lines 25..624 of 625, which is DMT 800x600@56's window exactly,
where CEA 576p's 576 lines from line 44 left 19 lines of the painted area black
across the top. `Mode576p` states 600 active lines with 19 of back porch and 1 of
front.

**The horizontal does NOT follow that table**, and why is open. Crept at 576p the
first painted column is raster 321 against our `activeStart` of 317, where DMT
800x600@56's 72 + 128 of blanking would put it at 404. The encoder cannot know
our pixel clock, so it has to find the line's active region by measurement while
the line count can be looked up -- consistent with both halves, and not
established.

## How `OutputMode` states it

Each standard's own raster is carried in the standard's own pixels at the
standard's own clock, and two different kinds of quantity come out of it:

| | kind |
|---|---|
| sync pulse | DURATION, converted to the clock the line runs at -- the pulse is load-bearing for the LINK |
| back porch | FRACTION of the line, plus the residual above |
| active width | FRACTION of the line, `T x carriedPx / totalPx` |

**The front porch is never stated.** It is what the total leaves, and
`OutputMode::FrontPorchMinPx` is the floor under it. The back porch is what
gives way when the line is too short to hold the porch and the active span
together, because nothing downstream reads it -- taking the shortfall out of the
SPAN throws picture away and makes one source reach the panel differently at
different field rates.

**What a front porch stated as a duration cost**: a framing set flush to the
panel at 1080p spilled off the right at 1024p. The stored proportions are
byte-identical at both and the engine filled each mode's stated active region to
within 0.2%, so neither was the fault -- the active region itself was 3.6% too
wide at 1080p and 5.4% too wide at 1024p, and the difference between the two
errors is what made a framing look resolution-specific.

## The picture's position is chosen downstream, and latched

**The instrument is the STV9426 overlay and it needs no probe.** The television's
menu is generated on the board from `HS_OUT`/`VS_OUT` and keyed into the video at
U13, downstream of the VDS, so it rides the SYNC timebase while the picture rides
the VDS's counter. One photograph carries both.

At two landings they move together: overlay −100.78 px / +26.58 rows against
picture −101.01 px / +26.43 rows, agreeing to 0.23 px and 0.15 rows with the
camera controlled at lag 0, `r = 0.998`. Video cannot have moved relative to sync
inside the scaler, so the analog frame is identical and the position is chosen
after it.

**It is not in the registers, and that is constructed rather than found.** A full
`snapdiff.py --save` either side covers all 1536 addresses, and writing every
differing byte so the dump reports 0 differing leaves the picture where it was --
as do resets of the video blocks, the SDRAM, the phase adjusters and the sync
processor.

**A poke cannot test what the encoder decides at acquisition.** Poking a frozen
engine made a leading-blanking artefact appear to follow the distance from the
output hsync pulse to the first written pixel, clean at 136 output pixels and
present at 138, repeating six times:

| encoder acquired | poked to | green | |
|---|---|---|---|
| 136 | 136, its own | 27.0 | artefact |
| 136 | 140 | 30.2 | artefact |
| 136 | 132 | 3.5 | **clean** |
| 140 *(earlier run)* | 136 | 3.7 | **clean** |

136 is clean under one acquisition and dirty under another. What is clean is
whatever sits about four pixels below the value the encoder **acquired**, never a
particular value: the poke moves our content relative to a sampling window fixed
at acquisition, so the artefact leaves the sampled region. It is displaced, not
cured, and the real cause was on the input side --
`VideoSourceLine::DoubledHeadBlankingUnits` 17 where the bench needs 20.

**The test that distinguishes them is a build.** Four independent poke
experiments agreed with each other because they shared the fault; internal
consistency is no defence.

## The scaling path does not re-place on a re-lock, and pass-through does

Two instruments on the same three modes, scaling path, 1080p -- `full_margins.py`
converts a dongle column through the encoder's line total and so carries any
latch, `counter_origin.py` creeps the capture's own start until the feature
leaves and so asks the counter directly:

| mode | `full_margins` | `counter_origin` |
|---|---|---|
| 640x480@60 | +0.5 samples | +1.4 |
| 1024x768@60 | −1.3 | −1.9 |
| 1280x1024@60 | +16.1 | +17.0 |

They agree within a sample over a 19-sample error range, and each pair is two
INDEPENDENT acquisitions in separate sessions whose rasters moved between modes,
so the sink re-acquired between every reading. A margin taken at 100% framing on
the scaling path can therefore be read as the counter's own.

**The mechanism for the difference**: in pass-through the SOURCE's timing reaches
the encoder, so the sink locks to a different HDMI mode per source, while on the
scaling path every `Tv5725::OutputMode` states a standard's timings and a raster
moving four units is a small analog change inside a mode the sink already
recognises. The ~150 column pass-through figure is that difference. The scope is
those conditions: all three sit at 1080p and the comparison crosses no
output-resolution change.

**`PAD_SYNC_OUT_ENZ` is what re-places it when anything does.** A source `MODE`
round trip never toggles the pad and never moves the sink's window; it is the
re-acquisition of the CAPTURE. So a measurement of where the SINK cuts the line
needs a pad toggle in the state under test, and a measurement of the capture
needs the round trip.

## Pass-through: the encoder places the picture and our window only paints black

`PAD_BLK_OUT_ENZ` is 1, so HBOUT and VBOUT are off and the encoder takes the
analog RGB with HSOUT/VSOUT alone. `HD_HB_SP` therefore cannot state where active
video is -- it can only paint black over video the channel sends.

**So a published raster has no job on this path**, and `HdBypass` no longer reads
`SourceTiming` horizontally. What the edge decides is how much blanking sits
inside the emitted active area, which the encoder shows as a bar:

| `HD_HB_SP` | 147 (the raster's sync extent) | 238 (the envelope) | 290 |
|---|---|---|---|
| black at the right | 251 columns | **56** | 117 |

238 read 56 on three consecutive forced re-locks, so the response belongs to the
register rather than to the landing. It is **not monotonic**, which is why no
offset constant is fitted.

**The one live bound is that the envelope may never reach where a source puts
video** -- 0.117 of the line against the Wii 480p's 122/858 and the AKF50
800x600's 176/1056 -- because bypass has no framing control to give cropped
picture back with.

### The border reaches three edges and not the left

`HD_MATRIX_BYPS` 1 leaves the picture green and renders the source's border dark
magenta at **G exactly 0**, which separates our blanking from the source's border
in one register. It is the pass-through counterpart of lifting the picture with
`VDS_Y_OFST`, which reaches nothing on this path.

Measured on the Wii at 480p into 1080p, `HD_HB_SP` 238, the frame filled at
1920x1078:

| edge | border, in emitted pixels |
|---|---|
| left | **none** -- column 0 is picture, G 239 |
| right | ~50 |
| top | ~3 |
| bottom | ~40 |

The picture is **DISPLACED by a border width, not stripped of both**: ~50 emitted
columns is roughly 19 source pixels against the 15 the source's border spends.
Removing the vertical window entirely through a re-lock leaves the bottom bar
where it was, and the registers are live -- blanking 120 lines puts 188 rows of
black at the top. It is the source's safe-area border, about 15 px left, 17 right,
15 lines top and 9 bottom, with the encoder's content origin moving all of it to
one edge of each axis. Nothing on the board can reach it.

**Non-black video earlier in the line DOES move the placement.** `BORDER ON` puts
video between the sync and the standard's active start, and the whole border then
arrives while the far end of the picture leaves the panel -- which reads as a
framing fault and is the source doing what it was asked. So a solve is judged with
the border OFF, and the border is turned on only to make an edge measurable.

**Why the left border does not arrive is OPEN.** The mechanism offered for it --
the encoder skipping a BLACK border -- is refuted by the matrix bypass above: the
border is not black in that state and the window still does not reach it.

## Refuted models

Every one of these was measured, and several were convincing.

**The window starts exactly where our blanking ends.** Six output modes against
the panel put the left edge on `activeStart` to within 2.2 px. Read off the
EMITTED frame instead of through a fitted photo-column mapping, the window opens
about 20 units later. Superseded for the near edge; the width finding stands.

**A fixed count of ~20 samples after the back porch.** Four output modes gave
+19.5, +20.3, +19.8, +20.8 -- which reads as a constant of the chain. The
procedure put the picture hard against the aperture's edge, and on a raster whose
own position is AFTER the aperture the sink lands its window ON the aperture, so
the walk extrapolates to the aperture the constant itself placed and returns the
value already in force. The same quantity with the aperture held away reads 171.7
and 173.78 against 159.5. **A measurement of this constant must put BLACK at the
aperture's edge, or leave the aperture well past the sink's own position, or it
measures itself.**

**The window latches from our `VDS_DIS_HB_SP`.** Re-measured at 320x256@50 into
1080p, the window sits at our unit 161.6 whether the register is 143 or 200,
across a source mode round trip and across a pad toggle; the toggle moves it ~11
columns between two landings and onto neither value. **The discriminator is the
black at the LEFT** -- a window that followed to 200 paints our first column at
column 0 and shows none, and 43 columns of it say the window opens 38 units
before our aperture and we are blanking into it. Every reading that supported the
latch was taken by walking a blanking register into the picture and extrapolating
back, which recovers the window's start only when nothing of ours is already
blanking it.

**The window latches onto the first non-black content.** Refuted three ways:

- Panned to expose 80 units of the source's blanking inside the display window,
  so the line carries blank, then a black border, then picture. A first-content
  lock predicts the window jumps past the border and it DISAPPEARS; it remained,
  at panel column 138 before and 138 after, 88 columns wide against 87. The link
  was verified to have dropped -- mean 31.2 with no content during the hold
  against 144.7 before and 145.8 after -- so a real re-acquisition happened and
  the window did not move.
- With the aperture held open and the capture panned 4 units (11.1 emitted
  columns), every region of the frame was asked which shift explains it: the
  picture moved **11** and the strip past the aperture moved **0**,
  byte-identical at both pans.
- A pan of ten capture units moves the window about ten OUTPUT units at both
  2.43x and 1.27x magnification, where the content it would be reading moves 24.3
  and 12.7. Nothing downstream of the VDS can see capture units.

**A per-output, per-field-rate table of delays.** The 28-sample move between the
50 Hz and 60 Hz rasters of one output is what a DURATION base produces: the base
sits at 139 samples whatever the raster, so a 50 Hz line 316 units longer grows
the fraction by 27 and the fixed base reports that growth as a jump. **There is no
band to tabulate.**

**A raster-ratio threshold behind the SD shortfall.** `EngineCeilingHz` lowered to
45 MHz and reflashed puts both SD modes at 1.50x and 1.80x, inside the window the
boundary was supposed to lie in, and the deficit is undiminished -- the painted
fraction holds to 0.3% across a 33% change of raster. The cause is the VESA
identification above.

**Our own output sync pulse, as a position or a width.** `VDS_HS_SP` walked 16,
32, 48, 64 with a re-lock at each gives start 159.52, stop 1830.61 and width
1671.09 at every one, to the digit. The pulse is nevertheless load-bearing for the
LINK: a fifth either way at 800x600@60 drops it outright -- panel mean 149.6 with
picture against 31.1, 30.9 and 31.4 with none -- and restoring the standard's
width brings it straight back, `r = 1.0000`.

**Our display blanking re-finding active video.** `VDS_DIS_HB_SP` swept 126 units
at 640x480@75 moved the right-hand 55% of the panel by 0 px with `r = 1.0000`
throughout; it painted black inside a window that did not move. Where our window
is narrower than the encoder's, we hide our own picture.

## What is still open

- **Two sources sit off their raster's band.** 360x480@60 at 525 lines and
  60.15 Hz opens 14 to 16 units early at both 1080p and 720p, twice at 720p; and
  320x256@50 into 720p at T 2156 opens 344.6 twice against 302.0 twice for
  640x512@50, two units of T and 0.08 Hz away on the same clock. Into 1080p those
  two agree. The output raster, clock and sync of each pair are alike to every
  instrument here.
- **Where the window lands with picture at the edge and the aperture far from the
  sink's own position.** Both 60 Hz rows of the fraction table have the aperture
  176 units after the own position, aspect shaping having narrowed it, and open 13
  units BEFORE it. 640x480@60 is the one source measured in all three conditions
  and the direction is that **the further the aperture, the earlier the window
  opens** -- 158.5 with black at the edge, 151.9 with picture and the aperture at
  ~160, 145.19 with picture and the aperture at 335. Nothing separates the
  aperture's distance from the aspect shaping that produced it.
- **Which downstream part chooses the position.** The MS9288A and the television
  are both there, and the USB capture dongle is a third sink with a latch of its
  own. Splitting them needs the same two states read on two sinks at once.
- **How the chain sees the edge it opens on.** Every blanking amplitude on this
  path is zero -- `VDS_UV_BLK_VAL` 0, `VDS_SYNC_LEV` 0, `HD_BLK_GY_DATA`/`_BU_`/
  `_RV_` 0 behind `HD_SEL_BLK_IN` 0 -- and on the panel true blanking and captured
  black border are indistinguishable, one flat profile at 37.5 with no step at the
  boundary. A pedestal of a code or two would be crushed by the television long
  before a camera saw it. **The panel is the wrong instrument for a level
  question; the scope is.**
- **The source's own leading blanking is now visible** at the left where the sink
  used to clip it, 18 columns on the bench 320x256@50. Nothing of ours is in it,
  so it is a framing question about whether a mode's border belongs in the
  capture.

## Instrument traps, each of which produced a wrong answer that looked like a finding

- **Do not find the edge by the first lit column.** The card's concentric bands
  put whole black columns inside the picture, so a brightness test runs past the
  blanking edge to the next bright band and reads tens of pixels late -- arriving
  as a jump in an otherwise straight line rather than as an artefact. Difference
  against the previous step and take the strip whose width the step size predicts.
  The card animates, so a flashing block can lie further out than the strip; its
  width is what tells them apart.
- **Put picture hard against the edge being walked.** A source with its own border
  leaves black there and blanking black changes nothing, which reads as a register
  that does nothing rather than as a framing that cannot be measured.
- **A photo-column mapping does not survive an output excursion**, so clips must
  span the line rather than its right-hand end: fitted on right-hand clips alone
  the mapping is an extrapolation of ten to one, and dropping one contaminated
  point moved the panel's left edge by 180 output pixels. A clip outside the
  painted area is not a measurement at all -- the strip pins to the panel rather
  than tracking the register.
- **Freeze automation first**, or the solver rewrites the windows underneath the
  sweep. A frozen unit is not an inert one: read the register back afterwards.
- **The dongle needs ten seconds or so** to deliver a usable frame after a pad
  toggle or a mode change, so a clip taken too early is all black and reads as a
  dead output.

## Instruments

`tools/gbsc-pro-hwtest/hdmi_capture.py` for the emitted frame and `setfield.py`
for the edges. `transmitted_window.py` does the width sweep including driving the
source's field rate to walk the raster. `framing_sweep.py --tier S --probes sink`
measures the sink's position per raster and `framing_report.py --sink` prints it.
`full_margins.py` and `counter_origin.py` are the two margin instruments above,
and `dark_time.py` times a transition off the dongle.
