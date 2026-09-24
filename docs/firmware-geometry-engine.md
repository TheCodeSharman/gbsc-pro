# The firmware geometry engine

How the firmware turns a capture window into the eight output blanking
registers, and the rules that keep it correct.

The *measurements* behind the model are in
[scaler-geometry-model.md](scaler-geometry-model.md). Read that first; this page
is about the code.

## Layout

| File | What |
|---|---|
| `…/gbs-control/src/tv5725/` | the driver. One class per file; every caller includes the classes it names, and there is no umbrella header |
| `…/src/tv5725/VideoPath.cpp` | the sequencing, and the only part of the cluster that writes registers — everything it derives from is pure arithmetic and host-compiles. `test/Makefile`'s `HOST_GEOMETRY_SRC` is the exclusion list |
| `test/test_capture_window.cpp`, `test_output_window.cpp`, `test_scale.cpp`, `test_axis.cpp`, `test_memory_window.cpp` | host-compiled unit tests, one per class, `make -C test` |
| `test/test_video_path_windows.cpp`, `test_video_path_raster.cpp` | what a solve writes, asserted field by field over the fake bus |

`geometry_math.py` is scaffolding, not the reference, and is slated for
deletion. Nothing asserts that the firmware equals it.

## The classes, and the one question each answers

```
SourceTiming     the raster the SOURCE runs
OutputTiming     the raster WE run -- the OutputMode rendered in VDS units

CaptureWindow::horizontal()  -> BlankingTiming    which part of the source
MemoryWindow                                      the SDRAM region it occupies
OutputWindow::horizontal()   -> OutputMapping     how it maps onto the raster
```

`MemoryWindow` is the seam: capture writes a region and playback reads it, and
every input to it is capture-side, so nothing about the output raster reaches
across.

`Axis` is the token the whole cluster is parameterised by — the grid a window
may move on and the step a press turns into. Where the picture LANDS is
`OutputWindow`'s, and that arithmetic is private to it: the cases assert the
four registers an axis comes out with, not the steps that produced them.

## The model

```
magnification = 1024 / VDS_?SCALE            (BYPS means 1:1)
produced      = capture x magnification      a simple multiply, both axes
write start   = VDS_?B_SP + startConst + startPerMag x magnification
                horizontal 55 + 25m, vertical 0.2 + 0.8m
```

Both captures are in the input formatter's own units, and **what a unit is
depends on the scan mode**, on both axes. See "What the IF counter counts" in
[scaler-geometry-model.md](scaler-geometry-model.md).

## The control surface

Two pads, both acting on the **capture window only**:

- **pan** — `/sc?+` `/sc?-` horizontally, `/sc?*` `/sc?/` vertically.
- **zoom** — `/sc?z` `/sc?h` both axes, `/sc?I` `/sc?O` horizontal only,
  `/sc?5` `/sc?4` vertical only.

Two more that move no framing: `/sc?U` re-derives every register from what the
engine holds, and `/sc?B` returns the framing to default and re-solves from the
source. **`/sc?@` is not free** — `'@'` is the value parked in `serialCommand`
to mean "nothing pending", and the switch is guarded on it.

The OSD and IR remote do not call these directly: they write into the
`serialCommand` / `userCommand` globals and `web_service()` picks them up on the
next 300 ms tick. So fixing the character handlers fixes web, OSD and remote at
once.

**The two pads move opposite edges, and nothing else.** The pan places the near
edge of the capture rectangle -- its top-left corner -- and the zoom moves the
far one, so a framing is found in one pass of each: pan until the source's
picture reaches the top-left of the screen, then zoom until it reaches the
bottom-right. A zoom that kept the rectangle's CENTRE moved the corner the pan
had just placed, and neither control converged.

Two consequences. **Zoom-out no longer opens the near edge**, so reaching the
whole capturable region takes a pan first -- the zoom stops when the far edge
reaches the end of the line. And **zoom-in stops where the MAGNIFICATION stops**,
at `Axis::minimumCapture()`: past it `VDS_?SCALE` is already at `Scale::Min`
and a tighter crop is a smaller picture rather than a closer one. Measured
holding the key before that stop existed, the scale pinned at 342 while the
capture fell 574 -> 16 units and the window marched to the corner of the source,
leaving a 48 px patch on a black screen.

`minimumCapture()` is taken against the ROOM the raster offers rather than its
total, and charges the write origin where the write floor binds, because that is
what `fitToRaster()` solves against -- against the total it stops the zoom a
tenth of the line short of the magnification the axis allows. A framing that
arrives from the table already below the stop is left where it is rather than
widened, so the press moves nothing instead of moving the wrong way.

A press carries a magnitude in **output pixels**: `/sc?<pad>=<n>`, or the pad's
own `ControlSteps` value when none is given. `Axis::stepUnits()` converts it to
capture units against the scale the last solve produced and never returns less
than one granule, so `/sc?<pad>=1` is the smallest move the hardware acts on
whatever the magnification happens to be.

**The state is a proportion; the step is a unit.** `PanAndZoom` holds an origin
and an extent per axis as fractions of the capturable region, which is what
carries a framing across a mode change and what the framing table stores. A
press still moves a whole input unit, because the proportion is kept on the
current mode's grid — every value a control produces is an exact multiple of
`1 / capturable` — so out and back returns the identical proportion rather than
one that denormalises to the same unit by luck.
[framing-presets.md](framing-presets.md) states that as a requirement, and it is
what a proportional step off the *current* capture could not give: those are not
reversible, because out and back use different widths and the window walks.

**There is no route that sets the framing outright.** `/geometry` reads it; it
moves through the pads alone, so nothing — instrument, panel or test — can
arrange a state the person at the OSD cannot reach. `gbs_unit.framing_to()`
walks one field to a value that way and reports where it landed, because a solve
clamps the framing it is given and not every value is reachable.

`/geometry` reports the framing three ways, because the engine is the only thing
holding the denominator to convert between them:

| | |
|---|---|
| `oh` `eh` `ov` `ev` | the window in input units — what the instruments and the measurements in [scaler-geometry-model.md](scaler-geometry-model.md) speak |
| `ch` `cv` | the capturable region those are taken against |
| `poh` `peh` `pov` `pev` | the proportion itself, in ten-thousandths — the state |

It is **behind `GBS_DEBUG`**: nothing on the product path reads it, only the
bench instruments and the hardware suite. A build without it answers 404.

## What the sketch may call

`Tv5725::VideoPath` is the engine's geometry half, and this is its whole
surface. **The engine is the new code, all of it** -- `VideoSourceAcquisition` and
every `Tv5725::` class -- as against the legacy sketch; what the sketch may reach
is `VideoSourceAcquisition`, which calls the rest.
[video-source-acquisition.md](video-source-acquisition.md)

| | |
|---|---|
| `inputTimingsChanged(oversample)` | the source is about to change mode; nothing is solved here |
| `setOutputMode(mode)` | what the output should do -- a resolution, `ModeBypass`, or 0 for a custom preset. Re-solves from what is held, measures nothing; pass-through is entered and left through this one call |
| `poll()` | drives whatever is outstanding, one pass, and says what it reached |
| `solvedLines()` / `solvedLineRateHz()` | what the last solve ran against |
| `framing()` | the framing the user has reached, read only |
| `capturableOn(axis)` | the region the last solve ran against — the denominator |
| `originUnitsOn(axis)` / `extentUnitsOn(axis)` | that framing in input units |
| `pan(dx, dy)` / `zoom(dh, dv)` | one press, in OUTPUT PIXELS |
| `sourceMeasured(reading)` | the hsync pulse, taken by the layer that measures; every window solved until the next one comes off it |
| `resolve()` | re-solve every register from what is held, without moving the framing and without measuring |
| `reset()` | back to the default framing |

**The tick, the gate and the source EVENT are not on that list.** `loop()` calls
`VideoSourceAcquisition::poll(millis())`, which decides whether the pass may run at
all, whether the source has moved, and arms the change itself -- so what is left
here is the solving half, and it takes no clock-shaped argument at all. A cadence
reached for down here is an input no host test can set, and the steadiness run
behind the source event is advanced by the reading it is taken from, so it can
have only one owner.

**Nor is what the source is running.** `sourceFieldRateHz()`,
`sourceLineRateHz()` and `sourceLowLineRate()` are `VideoSourceAcquisition`'s: the half
coordinating the measurement is the one that can answer, and this half is handed
the reading to derive registers from.

The sequence a mode change runs — sampling, raster, clock, windows — is private,
because running one step alone skips the rest of it and each depends on the one
before. `poll()` is where the order lives.

**The two are different events and take different entry points.** A source mode
change invalidates every measurement, so `modeChanged()` freezes capture and the
poll that follows measures the new source. An output change invalidates none of
them, so `outputChanged()` re-solves scan mode, sampling, raster, clock and
windows from state already held. The scan mode is on that list because **the
line doubler is a property of the output as much as of the source**: what
decides it is whether the doubled frame fits the raster.
docs/investigations/an-output-change-is-not-a-source-change.md

**A preference names a resolution and NOTHING ELSE, and the source gets no say
in it.** `Tv5725::OutputChoice` carries the user's preference and
`resolve()` returns the `OutputMode` it names.

**The source's field rate used to choose between two of them**, swapping 960p
against 1024p and 480p against 576p to whichever member matched the rate — the
taller at 50 Hz, the shorter at 60. That is the rate standing in for the
source's ACTIVE LINE COUNT, which only holds where rate and line count are
locked together by a broadcast standard. On a machine that programs arbitrary
modes the same 256-line raster came out at two different output resolutions
depending on a quantity that had not moved, so it is deleted rather than fixed.

**What was underneath it is worth having, and it is not the rate.** An output
whose active height is a whole multiple of the source's scales without
resampling — 480 is 2x240, 960 is 4x240 — and the engine measures the line
count directly. An option keyed on that is open, and would be a new feature
rather than a restoration: it needs a rule for what to do when no available
height is a multiple, which the pairs never had.

**A mode change is covered by a capture freeze, and every way out of `poll()`
releases it.** The windows land seconds after the load, once the source has
settled, so releasing at load time uncovers the *previous* mode's geometry
applied to the new source. `modeChanged()` takes the freeze; the poll that lands
the windows releases it, and so do the two ways the poll can stop without
landing them — a mode with no timings, and bypass. Missing one leaves the
picture a still frame with nothing left to unstick it.

## Rules

**Compute the geometry, never inherit it.** Read the capture and the raster;
derive everything else. Every geometry fault of 2026-08-06 was a violation:
inheriting the corner put 41 px of the previous frame down the left of the
screen, and inheriting the picture size froze a picture at 620 lines that no
zoom step could grow.

### "Registers are an output" is two rules

They are separable, and reading them as one blocks work that is correct.

**Single owner.** Every field is written by exactly one place. Two writers on a
field is the defect, whichever value is right.

**Derive once.** Nothing reads a register back to derive something else, so a
value computed for one solve cannot leak into the next.

**A true measurement is the exception to the second, never to the first.** A
read is legitimate when the chip is reporting the SOURCE — the line count, the
field rate, the hsync polarity. It is illegitimate when it is reporting a value
the engine itself wrote. `STATUS_SYNC_PROC_*` is where most of them live, but
the test is what the register is reporting, not which family it belongs to.

**Writing a register from a measurement is an ordinary solved write.** It obeys
the first rule like any other, and it does not touch the second.

**Prefer normalising the hardware over branching the solve.** Where the chip can
be configured so a source property stops varying, configure it and delete the
variable. A polarity carried into the solve is an input every later calculation
may depend on; a polarity normalised at the boundary is one the solve cannot get
wrong.

`HdBypass::readSourceSyncEdges()` and `applyChannelSyncEdges()` are the worked
case. Three things there are the pattern, and the scaling path has none of them:

- the four polarity statuses are read as source measurements, together, in one
  place;
- each polarity is gated on whether the processor found an edge to take it from,
  because a polarity read off a status the processor could not fill is a coin
  toss;
- the register that follows from it is written on every pass, not only when the
  value moved, so nothing downstream can disagree with it.

**And since 2026-08-13 the raster is computed too.** `VideoPath::solveRaster()`
derives both totals, both sync pulses and the display clock seed from the frame
height and the source's field rate, so the preset table's raster bytes are
overwritten on every mode change. Measured 1436 x 1126 at 80.85 MHz before,
1915 x 1126 at 107.81 MHz after — a third more horizontal resolution, and the
end of the last register group a preset was still the authority for.

### The rate comes from the KEY, so it repeats

`solveRaster()` reads `SourceKey::rateHz()`, not the reading the pass took. The
reading wanders on a source that is standing still: measured across four mode
changes of one unchanged 800x600 source, it settles at 60.38 Hz after one and
60.72 after the next, and the horizontal total moved 11 px with it -- two solves
of one source landing on two framings.

The key carries a whole number of hertz and is sticky, replaced only when the
arriving key differs, so a later reading inside the tolerance keeps the rate the
key was established with. `VideoPath::adoptSourceKey()` therefore runs BEFORE
`solveRaster()`; run after, the raster was generated against the previous
source's key.

**Nearest hertz, not truncated.** Real modes are built to be "60 Hz" and land on
and just above the integers -- 13 of the 63 in the bench monitor definition sit
exactly on one -- so a boundary at the integer runs through the middle of the
cluster and any downward wander drops a whole hertz. At the half hertz it falls
in the gaps: 3 of the 63 come within 0.15 Hz of a boundary (54.4833, 69.5398,
71.4286) and none is a mode this bench runs.

**The cost is up to half a hertz of accuracy, and the frame time lock pays it.**
The bench RiscPC at a true 50.08 Hz now solves a 1920 raster where it solved
1916. FrameSync closes on frame TIME continuously, so a raster in the right
ballpark is steered exact; one that jumps between solves of the same source is
not.

**The identity tolerance stays wider than the rounding, and that is what it is
for.** 60.38 and 60.72 round to different hertz and must still be one source, or
the stored framing swaps under drift.

**The field rate has to be right, and 40..100 Hz was nowhere near tight
enough.** A raster solved at the wrong rate is out by the ratio of the rates,
and `solveRaster()` runs during a preset load while the source is still
settling, so the reading is transient. Three boots of identical firmware landed
on three different rasters. Working backwards from what the bench measured:

| raster | implied rate | |
|---|---|---|
| 1918 | 50.01 Hz | the source really is 50.08 |
| 1915 | 50.09 Hz | |
| 1761 | 54.47 Hz | transient |
| 1740 | 55.12 Hz | transient |
| 1436 | 66.79 Hz | transient |

Every one is exactly `horizontalTotalFor(108 MHz, 1126, thatRate)` — nothing was
wrong except the rate, and 9% of the horizontal resolution was decided by when
the sample landed. The cross-check is the source's own line count, which the
sync processor counts every field and mode detect already splits on: a PAL-like
source runs ~312 lines and an NTSC-like one ~262, so the line count picks the
nominal rate and the measurement only has to agree with it within 2%.

**The zoom step takes no current-scale argument.** Its existence *was* the bug.
`test_geometry_math.py:744` asserts by reflection that the Python reference's
`scale_step` has no such parameter; C++ cannot make that assertion, so the
behavioural tests carry it instead — a step crops and the picture stays full
size, and five steps out then five back return exactly to `(264, 1062)`.

**Never write `VDS_HSYNC_RST` or `VDS_VSYNC_RST` FROM A GEOMETRY SOLVE.** They
change the mode the TV locks to, and FrameSync steers the frame time
continuously — a solve that moved them would fight it, every pad press.

`solveRaster()` is the one writer, once per mode change, from `poll()`, which
drives the whole sequence itself. Everything else, `pan()` and `zoom()`
included, must leave them alone. The ordering is
`docs/investigations/preset-abandonment-audit.md`'s and is not optional: raster,
clock, windows, rate steer **last**. It is expressed once, inside `poll()`,
rather than assembled by the caller — the display clock reads the seed the
raster just chose, and every window is sized against the raster it lands on.

**Widening the raster costs picture quality, not zoom travel.** The capture
cannot go below `Axis::minimumCapture` — `ceil(raster / maxMagnification)` —
without leaving a bar, while the default capture is a property of the input line
alone, so the two do not track. That threshold used to CLAMP the framing, which
is why the travel column below reads as it does; it no longer does, and a
capture past it letterboxes instead. Measured 2026-08-13, when both axes still
magnified at most `1024/500 = 2.048x`:

| ceiling | raster | min capture | zoom travel from default |
|---|---|---|---|
| 129.6 MHz | 2298 | 1123 | **0 presses** — the default sat on the clamp |
| 108 MHz | 1915 | 936 | 16 presses |
| 81 MHz | 1436 | 702 | ~61 presses |

**The htotal search is gone.** `applyBestHTotal()`, `runAutoBestHTotal()` and
`snapToIntegralFrameRate()` hunted for a horizontal total by nudging it and
re-measuring the output frame rate. `solveRaster()` computes the same quantity
directly — `clock / fieldRate / frameLines` — so the search was a second answer
to a question already answered, and the two did not agree: across three boots of
identical firmware the engine computed 1918 every time while the search settled
at 1915, 1436 and 1740.

That is why `OutputMode::EngineCeilingHz` is 108 MHz and not the 129.6 MHz the
part demonstrably runs at — a usability limit, since both were judged "works,
sharp" on the bench.

**That argument has since expired and nobody has re-run it.** The floor is
`Axis::minimumCapture()`, the room the raster offers over `Scale::Min`, so the
2298 raster floors at 721 rather than 1123 and leaves real travel. Raising `EngineCeilingHz` to 129.6 MHz would buy a
third more horizontal resolution; it is a live bench experiment rather than a
settled no.

**The picture is centred on the raster, never pinned to a panel edge.** Where a
display stops showing is a property of the display — `PANEL_VISIBLE_LEFT` was
carried as 127 and measured 90 on the bench TV.

**`VDS_HB_SP` has a floor of 8** (measured at one output hsync setting only).
`AxisVertical`'s `windowStopMin` is 0 and is an *assumption* — nobody has crept
it.

**The capture may not take the hsync pulse.** `Tv5725::VideoSourceLine` carries
the wrap point *and* the sync interval on it, `VideoSourceLine::forDuty()`
deriving the second from the source as `ceil(units x HLOW_LEN / PLLAD_MD)`. The
line is data; `CaptureWindow` is what turns it into bounds, excluding the pulse
at whichever end the measured polarity puts it.

**THE FRAMING IS A PROPORTION OF THE INPUT, AND NO OUTPUT QUANTITY MAY REACH
IT.** `PanAndZoom` holds where the window starts and how far it runs as
fractions of the capturable region, so 0 is the first unit the capture can reach
and 1 the last, and `PanAndZoom::clampOn()` — extent in `[0, 1]`, origin at or
above 0, `origin + extent` at or below 1 — is the whole bound. `CaptureWindow`
takes no `OutputRaster` at all, which is what makes that true by construction
rather than by discipline.

It was not always. The clamp used to place the window against the raster
and then seed the framing back from what it placed, so every solve at a new
output resolution rewrote the stored proportions — a framing tuned at one
resolution meant a different part of the source at the next, and
`FramingTable::remember()` persisted the rewritten value. Measured: the
horizontal extent could not be cropped below 514 units at 480p or 479 at 1080p,
and the same framing produced different windows on the two.

**The part still cannot minify, and that is expressed where the registers are
solved rather than where the framing is held.** `VDS_?SCALE` divides 1024 and
tops out at `Scale::Max`, so the least magnification it can express is 1.001;
`Axis::fitToRaster()` clamps the scale between that and `Scale::Min`. A capture
too small for the raster therefore letterboxes and one too large has its far end
cropped — both visible, both undone by one press back, and neither able to touch
the framing.

**A SOURCE TALLER THAN THE CHOSEN OUTPUT MODE IS CROPPED, AND THAT IS THE PART
RATHER THAN THE SOLVE.** 1280x1024@60 is the first bench source tall enough to
reach it: into `Mode960p` the 1024 active lines want 0.936x of a 958-row active
window, `VDS_VSCALE` sits at `Scale::Max`, and the engine keeps 957 lines
centred — `/geometry` reports `ov` 75 and `ev` 957 against a source active at
41. Into `Mode1080p` the same source takes `ov` 41 and `ev` 1024, the whole
published active region, at a magnification of 1.05. **The output resolution is
a user preference and names nothing about the source**, so nothing here chooses
a taller mode on the source's behalf; reading the crop as a raster the solver
sized wrongly is the mistake to avoid. What the CONTROL does is a separate question, and zoom-in stops at
`Axis::minimumCapture()` rather than pressing on into the letterboxed range:
past it the crop no longer magnifies, so the press has nothing left to do.

The same bound decides the line doubler. Doubling turns a 311-line source into
622 units, which 720p and 1080p hold and 480p and 576p do not, so
`SourceMeasurement::lineDoublingFor()` takes what the output can show as well as
what the source sends. It is asked of the mode requested rather than the raster
last solved, because the scan mode is settled before `solveRaster()` runs.

**An untuned axis is placed, not guessed twice.** Where active video sits inside
the line cannot be measured — a border is black active video, electrically
identical to back porch — so `CaptureWindow::place()` places the first window
itself and `clampFramingTo()` seeds the framing from what it placed, which is
why a default framing saved and restored produces identical registers.

Two sources for that placement. `Tv5725::SourceTiming` matches the frame, the
field-rate bucket and the hsync duty against thirteen DMT and CEA-861 rasters,
and a source running one is placed on the standard's own active window, both
axes, with no over-capture added. A source matching none takes
`Axis::activeFraction()` over-captured and centred in the line. A source that
keeps a standard's raster while spending its back porch on border sits a few
pixels left of where the standard says and is the user's to trim — see
[investigations/vesa-modes-are-clipped-by-default.md](investigations/vesa-modes-are-clipped-by-default.md).

`InputFormatter::capturableLine()` and `capturableFrame()` state the two
counters the source presents, and `Tv5725::CaptureWindow` is the rectangle
placed inside them: both axes together, holding the two lines it must stay
within, and clamping the framing on the way in. The window and the framing are
therefore taken from one placement, so they cannot be given different bounds —
one unit between them is the dead zone.

The lines themselves are data. Where a window MAY sit in one —
`firstUnitOn()`, `reachOn()`, `capturableOn()` — and how a framing proportion
maps into it — `videoAtOn()` and its inverse `fractionAtOn()` — are
`CaptureWindow`'s, because placing the rectangle is its job.

**The tail is deliberately unbounded and there is a test saying so.** There is
green there too, but it is not the sync pulse and nothing derives its position;
a guard there excluded clean porch and cost zoom-out reach. See
[scaler-geometry-model.md](scaler-geometry-model.md).

**The memory window IS the display window.** `VDS_?B_ST` equals
`VDS_DIS_?B_ST`, allocating nothing spare: memory past the picture is memory the
playback stage still walks, and taking the whole raster showed as artefacts down
the left edge. It took everything until 2026-08-09, and the headroom rule that
reserved a margin instead is retracted — see CLAUDE.md.

## The framing reproduces at every output size, and nothing clamps

The framing is proportions, so changing the output resolution must land the
picture on the same fraction of whatever raster it gets. The floor the control
stops at is `Axis::minimumCapture()`, which follows the raster's own room, so
the reachable range is raster-independent by construction: a smaller output
shrinks what the scaler produces with it,
lowering the magnification and moving AWAY from the floor. **A clamp anywhere is
a defect in the arithmetic**, and the way it shows is a picture that will not
fill a smaller output.

Measured on the bench across the whole walk, one source throughout:

| output | raster | `VDS_HSCALE` | `VDS_VSCALE` | `poh` | `peh` | `pov` | `pev` | fill |
|---|---|---|---|---|---|---|---|---|
| 1080p | 1916 x 1125 | 546 | 533 | 498 | 9316 | 611 | 9357 | 0.934 |
| 1024p | 2022 x 1066 | 516 | 561 | 498 | 9316 | 611 | 9357 | 0.936 |
| 960p  | 2156 x 1000 | 483 | 599 | 498 | 9316 | 611 | 9357 | 0.938 |
| 720p  | 2156 x 750  | 483 | 802 | 498 | 9316 | 611 | 9357 | 0.938 |
| 576p  | 2070 x 625  | 504 | 481 | 498 | 9316 | 613 | 9355 | 0.936 |
| 1080p | 1916 x 1125 | 546 | 533 | 498 | 9316 | 611 | 9357 | 0.934 |

The horizontal proportions are identical at every one, the vertical moves two
ten-thousandths at 576p alone, and the round trip returns the entry values
exactly. The scale never approaches its 256 floor.

### THAT IS THE ENGINE'S HALF, AND IT IS NOT THE PICTURE

**A REGISTER IS NOT A PICTURE.** Photographed at the same framings, fully zoomed
out so borders and blanking are on screen, the black border does NOT hold. The
right border is 0.169 of the frame at 1080p and 0.053 to 0.066 at every other
output, and the card's aspect moves with it; top and bottom hold. 1080p repeated
at the end of the walk reproduces its own numbers, so the differences are real.

The reason is above the table: **the raster's own shape varies by nearly 2:1** --
1.70 at 1080p, 1.90, 2.16, 2.88, and 3.31 at 576p. The horizontal total comes
from the display clock and the field rate, the vertical from the output's frame
height, and nothing relates the two. So the same fraction of the raster is not
the same picture, and the invariant this section states is the engine being
self-consistent rather than the framing reproducing on screen.

Where the last step happens is NOT established -- whether the encoder maps a
2156-sample line onto the active width the way it maps a 1916-sample one. **Do
not file that against the MS9288A without measuring it.** It is on no I2C bus,
and that attribution has been reached for rather than measured twice before.

**COMPARE ONE SOURCE ONLY.** `SourceKey` is the line count and the field rate,
so changing the INPUT mode is a different key and a different framing, and two
input modes say nothing about this invariant.

**And the comparison is at the precision `/geometry` states, not exact.**
`VideoPath::calculateInputFormatterRegisters()` hands the framing to the capture
window and takes back what whole units could express, so every solve
re-quantises it by a unit or two. A clamp moves it by hundreds.

## Rounding is `lrintf`, not `lroundf`

`lrintf` is round-half-to-**even** under the default rounding mode, which is what
Python's `round()` does. `lroundf` is round-half-away-from-zero.

Centring hits an exact `.5` tie whenever `rasterTotal - produced` is odd, which
is about half of all captures. Either rule is physically fine; the two
implementations disagreeing is not. Written with `lroundf`, a 186-point grid
disagreed on 16, every one by exactly one pixel.

## Write ordering

Both windows bounding the headroom are two independent registers, so moving
either takes two writes with a state in between, and one of the two orders is
wrong. A mis-ordered memory-window slide dipped headroom to −86 px on the bench.

The solver always takes the whole memory window, so the only edge that can
narrow it is `VDS_?B_SP` moving up. That makes the safe order fixed:

1. far edges to maximum — can only add headroom
2. near edges **down**, if down is where they are going
3. the picture: capture registers, `VDS_?SCALE`, `BYPS` cleared
4. near edges **up**
5. the display window

## Bypass

`VideoPath::rasterSolved()` is false until both output raster axes reach 64,
and a solve stops short of the geometry until then. In RGBHV bypass the video path does not go through the VDS
at all, `VDS_?SYNC_RST` reads 0, and there is no geometry to solve — writing one
would write into a path nobody is using. See [rgbhv-bypass-trap.md](rgbhv-bypass-trap.md).

**Bypass measures nothing, and the held measurement is deliberately kept
across it.** Neither bypass switch reaches `doPostPresetLoadSteps()`, so
`inputTimingsChanged()` never fires for a bypassed mode and no poll measures
one — so what `sourceLowLineRate()` answers there is the rate from the mode that
preceded bypass. That is the fact the caller wants: whether the display can show
this source at all is asked *while* bypassed, and discarding the reading does not
remove the stale fact, it only moves the question to something that cannot
answer it.

What is left there is `rto->videoStandardInput`, and on that path it is honest:
it carries the mode `getVideoMode()` detected immediately before the switch, and
a scaled RGBHV source — the one whose number does not carry its line rate, since
it is filed as 480p — cannot be in HD bypass at all, because taking that branch
clears the pass-through preference. So the sync processor's SD settings do not
all read the same place. On the scaling path they ask the measured rate; in
bypass they ask the byte, and the sketch routes the question on
`Tv5725::VideoRoute`.

## The sampling divider

`Tv5725::SourceMeasurement` owns `PLLAD_MD`, `IF_HSYNC_RST` (= `MD`/2) and `SP_RT_HS_SP`
(= 93% of `MD`) off one held value, and computes it from the measured line rate.
It must be written **before** `latchPLLAD()`; after it, the register reads the new
value while the PLL still runs the old one.

`RecommendedPercent` is 98, taken from the deleted tables: they shipped
2269..2559, the 1080p pair at 2553 and 2558, both 98% of the 162 MSPS rating. 85
computes 2210 for this bench against the table's 2553.

Three failure modes, all measured 2026-08-15 with the tables gone, all producing a
solid green screen with every register self-consistent:

| fault | symptom | fix |
|---|---|---|
| rate read while the source settles | 311 lines / 50.08 Hz solved as ~57.9 Hz → `PLLAD_MD` 2204 | `lineRateFrom()` cross-checks rate against the line count within 2%, else returns 0 |
| refused, then a fallback adopted | `271 lines x 49.22 Hz -> line rate 0`, `PLLAD_MD` 1856 every cold boot — the literal `bypassModeSwitch_RGBHV()` writes | `poll()` retries the whole sequence until the source settles, and `Adc` latches the divider it writes |
| divider correct, `SP_RT_HS_SP` stale | written once by `doPostPresetLoadSteps()`, which the deferred retry never re-enters | one quantity, one owner — `SourceMeasurement` writes all three |

The cross-check is necessary and not sufficient: it catches a rate disagreeing
with the line count, never a line count that is simply wrong. Entering
pass-through drops the pending flag, since neither bypass switch reaches
`doPostPresetLoadSteps()` and a later retry would move the divider under a bypass
that chose its own.

## Testing

```sh
make -C test                                           # host unit tests
```

The drift check is the one that matters. Ported unit tests cannot catch a port
that is wrong the same way on both sides, so it compiles the test binary with
`--dump` and diffs a grid of solved geometry against `geometry_math.py`
directly.

**Walks, not samples.** The zoom and pan grids feed each step into the next, so
a one-unit divergence compounds rather than averaging out. **Include odd
deltas**: the zoom step splits near/far as `magnitude/2` and the remainder,
which are equal on even numbers — a walk of only even steps cannot see which
edge got the odd pixel, and a mutation swapping them passed until `-33` and `15`
were added.
