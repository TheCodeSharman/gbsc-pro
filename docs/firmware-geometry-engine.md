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
| `…/src/tv5725/Geometry.cpp`, `Controls.cpp` | the only two that touch registers or Arduino — everything else is pure arithmetic and host-compiles |
| `test/test_axis.cpp`, `test_scale.cpp`, `test_input_line.cpp`, `test_active_image.cpp` | host-compiled unit tests, one per class, `make -C test` |
| `test/test_geometry*.cpp` | what `Tv5725::Geometry` writes, asserted field by field, `make -C test geometry` |

`geometry_math.py` is the reference implementation and stays that way. It holds
every bench measurement from 2026-08-03 to 2026-08-06 as an acceptance test, and
the pan/zoom pads built on it produced a pixel-perfect picture unaided.

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

`Geometry`'s public surface is what the sketch may reach, and nothing else:

| | |
|---|---|
| `modeChanged(mode, oversample)` | the source is about to change mode; nothing is solved here |
| `poll()` | drives whatever is outstanding, on every pass of `loop()` |
| `sourceInterrupted()` | the chip latched a disturbance; arms a re-measure |
| `enterBypass()` | video routes around the VDS, so there is no solve coming |
| `framing()` | the framing the user has reached, read only |
| `capturableOn(axis)` | the region the last solve ran against — the denominator |
| `originUnitsOn(axis)` / `extentUnitsOn(axis)` | that framing in input units |
| `pan(dx, dy)` / `zoom(dh, dv)` | one press, in OUTPUT PIXELS |
| `resolve()` | re-derive every register from what is held, without moving the framing |
| `reset()` | back to the default framing |
| `sourceFieldRateHz()` / `sourceLineRateHz()` / `sourceLowLineRate()` | the source as the last solve measured it |

The sequence a mode change runs — sampling, raster, clock, windows — is private,
because running one step alone skips the rest of it and each depends on the one
before. `poll()` is where the order lives.

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

**And since 2026-08-13 the raster is computed too.** `Geometry::solveRaster()`
derives both totals, both sync pulses and the display clock seed from the frame
height and the measured field rate, so the preset table's raster bytes are
overwritten on every mode change. Measured 1436 x 1126 at 80.85 MHz before,
1915 x 1126 at 107.81 MHz after — a third more horizontal resolution, and the
end of the last register group a preset was still the authority for.

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

**The raster sets how far the zoom can go, so widening it is not free.** The
capture cannot go below `Axis::minimumCapture` — `ceil(raster / maxMagnification)`
— without leaving a bar, while the default capture is a property of the input
line alone, so the two do not track. Measured 2026-08-13, when both axes still
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

**That argument has since expired and nobody has re-run it.** `AxisHorizontal`
and `AxisVertical` both magnify 4.0x now and `scaleMin` is derived as
`Scale::Unity / maxMagnification`, so the 2298 raster floors at 575 rather than
1123 and leaves real travel. Raising `EngineCeilingHz` to 129.6 MHz would buy a
third more horizontal resolution; it is a live bench experiment rather than a
settled no.

**The picture is centred on the raster, never pinned to a panel edge.** Where a
display stops showing is a property of the display — `PANEL_VISIBLE_LEFT` was
carried as 127 and measured 90 on the bench TV.

**`VDS_HB_SP` has a floor of 8** (measured at one output hsync setting only).
`AxisVertical`'s `windowStopMin` is 0 and is an *assumption* — nobody has crept
it.

**The capture may not take the hsync pulse.** `Tv5725::InputLine` carries the
wrap point *and* what is unusable on it, and `InputLine::measured()` derives the
second
from the source: `ceil(units x HLOW_LEN / PLLAD_MD)`, excluded at the **head**
only, because `SP_RT_HS_ST` is 0 and the input formatter counts from the sync's
leading edge.

**The part cannot minify, and both ends of the capture are bounded by that.**
`VDS_?SCALE` divides 1024 and tops out at `Scale::Max`, so the least
magnification it can express is 1.001. `Axis::minimumCapture()` stops a zoom
cropping past what the magnification can put back; `Axis::maximumCapture()` stops
a framing taking more than the output can show, because a capture past it
produces a picture past the room and the far end is simply not drawn. Without the
ceiling the control reads as dead in BOTH directions -- zoom-out is at the bound
of the capturable region, and zoom-in only trims capture that is already
off-screen.

The same bound decides the line doubler. Doubling turns a 311-line source into
622 units, which 720p and 1080p hold and 480p and 576p do not, so
`SourceMeasurement::lineDoublingFor()` takes what the output can show as well as
what the source sends. It is asked of the mode requested rather than the raster
last solved, because the scan mode is settled before `solveRaster()` runs.

**An untuned axis is placed, not guessed twice.** Where active video sits inside
the line cannot be measured — a border is black active video, electrically
identical to back porch — so `ActiveImage` places the first window itself and
`clampToLine()` seeds the framing from what it placed, which is why a default
framing saved and restored produces identical registers.

Two sources for that placement. `Tv5725::SourceTiming` matches the frame, the
field-rate bucket and the hsync duty against thirteen DMT and CEA-861 rasters,
and a source running one is placed on the standard's own active window, both
axes, with no over-capture added. A source matching none takes
`Axis::activeFraction()` over-captured and centred in the line. A source that
keeps a standard's raster while spending its back porch on border sits a few
pixels left of where the standard says and is the user's to trim — see
[investigations/vesa-modes-are-clipped-by-default.md](investigations/vesa-modes-are-clipped-by-default.md).

`Tv5725::InputSignal` pairs the two lines into the rectangle the source
presents, and `Tv5725::CaptureWindow` is the rectangle placed inside it: both
axes together, holding the signal it must stay within, and clamping the framing
on the way in. The window and the framing are therefore taken from one
placement, so they cannot be given different bounds — one unit between them is
the dead zone.

**The tail is deliberately unbounded and there is a test saying so.** There is
green there too, but it is not the sync pulse and nothing derives its position;
a guard there excluded clean porch and cost zoom-out reach. See
[scaler-geometry-model.md](scaler-geometry-model.md).

**The memory window IS the display window.** `VDS_?B_ST` equals
`VDS_DIS_?B_ST`, allocating nothing spare: memory past the picture is memory the
playback stage still walks, and taking the whole raster showed as artefacts down
the left edge. It took everything until 2026-08-09, and the headroom rule that
reserved a margin instead is retracted — see CLAUDE.md.

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

`Geometry::readCapture()` refuses when the output raster reads under 64. In RGBHV
bypass the video path does not go through the VDS at all, `VDS_?SYNC_RST` reads
0, and there is no geometry to solve — writing one would write into a path
nobody is using. See [rgbhv-bypass-trap.md](rgbhv-bypass-trap.md).

**The engine measures only what it scales, so in bypass it can answer nothing
about the source.** Neither bypass switch reaches `doPostPresetLoadSteps()`, so
`Geometry::modeChanged()` never fires for a bypassed mode and no poll measures
one; `enterBypass()` calls `SourceMeasurement::forgetSource()` so the last
scaled mode's rate cannot be read as this one's. A reader on the bypass path
asking `sourceLowLineRate()` therefore gets a truthful "nothing measured", not
the source in front of it.

What is left there is `rto->videoStandardInput`, and on that path it is honest:
it carries the mode `getVideoMode()` detected immediately before the switch, and
a scaled RGBHV source — the one whose number does not carry its line rate, since
it is filed as 480p — cannot be in HD bypass at all, because taking that branch
clears the pass-through preference. So the sync processor's SD settings do not
all read the same place. On the scaling path they ask the measured rate; in
bypass they ask the byte, and the sketch routes the question on
`rto->outModeHdBypass`.

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
with the line count, never a line count that is simply wrong. `enterBypass()`
drops the pending flag, since neither bypass switch reaches
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
