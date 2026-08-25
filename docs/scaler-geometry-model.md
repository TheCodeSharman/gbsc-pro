# Where the picture lands, and how big it is

The arithmetic that turns a capture window and a scale into the four output
blanking registers. Encoded in
[`tools/gbsc-pro-hwtest/geometry_math.py`](../tools/gbsc-pro-hwtest/geometry_math.py)
and ported to `src/tv5725/`; this page is the evidence behind the constants.

## The model

```
produced    = capture x 1024 / scale
write start = VDS_?B_SP + START_CONST + START_PER_MAG x magnification

VDS_DIS_?B_SP = corner
VDS_DIS_?B_ST = corner + floor(produced) - margin
VDS_?B_SP     = corner - (START_CONST + START_PER_MAG x magnification)
VDS_?B_ST     = raster_total - 2
```

| | horizontal | vertical |
|---|---|---|
| `capture` | `IF_HB_ST2 - IF_HB_SP2`, IF units | `IF_VB_ST - IF_VB_SP`, IF lines |
| `scale` | `VDS_HSCALE` | `VDS_VSCALE` |
| `START_CONST` | **55.0** px | **0.2** lines |
| `START_PER_MAG` | **25.0** px per x | **0.8** lines per x |
| `margin` | 2 px | 3 lines |
| `VDS_?B_SP` floor | **8** — below it the display corrupts | unmeasured, assumed 0 |

`produced` is a simple multiply: no loss term at either end, on either axis.

The corner is not a constant. It is
`VDS_?B_SP + START_CONST + START_PER_MAG x magnification`, so the memory window
moves as the picture is zoomed.

### Why the constants have those values

`START_PER_MAG_H` of 25 output px per unit magnification is 25 *input samples*:
a fixed run-up the horizontal scaler consumes before it writes anything, which is
what an 11-tap filter costs (`IF_HS_TAP11_BYPS`). The vertical reads whole lines
from a line buffer and costs ~2 lines of the same.

Both are **pipeline latency at the start of the write**. The axes are not
different shapes; they have different pipelines.

## Measuring it

Two measurements, and they are independent — the far-edge readings predate the
origin measurement that explains them, and share no data with it.

**`produced`, from the far edge.** The scaler leaves frame buffer beyond the
picture untouched, so a display window wider than the picture shows a band of
unwritten memory. Creep `VDS_DIS_?B_ST` down until the band just vanishes.

**The write start, from the near edge.** TestPat's screen border and outermost
ring flip colour twice a second, so anything flipping is being written this frame
and anything frozen is scratch the scaler has stopped writing to. Park the
display window's near edge *before* the write start, and a band of frozen scratch
shows. Creep up until it goes.

```sh
python3 tools/gbsc-pro-hwtest/measure_produced.py --host <ip> --axis h
python3 tools/gbsc-pro-hwtest/measure_origin.py   --host <ip> --axis h
```

`measure_origin` asks two questions with three points each: does the write start
track `VDS_?B_SP` (slope 1.000, residual 0.00 on both axes) and does it move with
magnification (25.0 px per x horizontally, ~0 vertically).

The two routes agree:

```
from the far edge, backwards   offset = 24.67 x m + 55.23   worst 0.43 px
from the near edge, directly   offset = 25.01 x m + 54.97   worst 0.01 px
```

0.34 px apart on the slope and 0.26 on the intercept, over captures 200..798 and
four magnifications per axis.

**Use three magnifications minimum, and read the residuals.** Two points cannot
disconfirm a line, and a fit that cannot fail has not been tested. Both measure
scripts print residuals for that reason.

**Put content beyond the edge before creeping.** Set the picture to overrun the
raster on all four sides so every boundary has live video either side of it —
otherwise the reading is where the *picture* stops, not where the *screen* does.
The vertical top reads 63 the wrong way and 41 done properly.

## Where the picture is placed, and how big it is made

**Everything is computed from the capture and the raster. Nothing is read back
from the registers that happen to be set.** That rule is the design.

`fit_to_raster` makes the picture as big as the raster allows and returns the
scale that gets it there; `place_picture` centres it:

```
room     = raster - 2 - 2 x (VDS_?B_SP floor + START_CONST)
produced = room x capture / (capture + 2 x START_PER_MAG)
scale    = 1024 x capture / produced          clamped to 256..1023
corner   = (raster - produced) / 2
```

The magnification term makes the size implicit — the write offset grows with
magnification, which depends on the size being solved for — so it is solved, not
iterated. It costs at *both* ends because the picture is centred.

**Maximal and centred, not simply maximal.** Running the picture from the write
floor to the far rail is about 95 px bigger, but it cannot be centred: on the
bench it gives margins of 105 and 5 and hangs 89 px off the right of the screen.
Equal margins mean whatever overscans is lost equally, which is the only kind of
loss a user can reason about.

**Not pinned to any panel edge.** Where a display stops showing is a property of
the display; a scaler cannot know it and does not guess.

## The control: one pad, and it crops

The user gets pan and zoom. Zoom changes **the capture only**; the picture stays
as big as the raster allows and the scale is recomputed to suit. Zoom in captures
less source and magnifies harder, zoom out captures more and magnifies less, and
the picture never changes size underneath them. Both windows are managed for
them; neither is theirs to maintain.

`scale_step` takes **no scale argument at all**, and a test asserts that. The
step is a fixed number of input units, symmetric about the centre, so out and
back returns every register exactly. Proportional steps are not reversible: out
and back use different capture widths and so different step sizes, and the
window walks.

**Every press recomputes every window**, including pan, so a state left odd by an
experiment is corrected by the next press rather than carried forward.

Two bounds: the line wrap going out, and a minimum capture going in — a control
that can crop the capture to nothing is one keypress from a dead picture with no
way back.

### The horizontal capture moves in twos

Measured 2026-08-17, driving `/geometry` and watching the picture. With the
window held 1000 units wide:

```
capture start 162 <-> 163    five toggles    picture still
capture start 162 <-> 164    five toggles    picture moves
```

So **a one-unit shift of the horizontal capture window does nothing**, and the
smallest change the hardware acts on is 2 IF units. Consistent with the low bit
of `IF_HB_SP2` being ignored, which is the reading the rest of the model
supports: a global halving of the register would have shown up as a factor of 2
in `produced = capture x 1024 / scale`, and that fit needs none.

`Tv5725::Axis::captureGranularity()` is the number — 2 horizontally, **1
vertically**, where every unit counts. `Axis::stepUnits()` quantises a press to
it, so a request never rounds below one granule.

**Rounded once, in output pixels.** Quantising to units and then to granules
biases every request upwards: 8 output pixels is 4.73 units at x1.69, which
becomes 5 and then 6, where 4 is the nearer of the two reachable values.

Two things this costs, and neither is avoidable from here. One unit of pan is no
longer one output pixel horizontally — it cannot be, when the hardware's finest
move is 2 units and one unit is already 1.69 pixels. And **no diagnostic on the
board can see a step that is too fine**: the register changes, the IR frame
decodes and dispatches, the dump stays self-consistent, and only the picture
disagrees. An odd step reads as a dead remote.

The capture *start* is not forced onto a granule boundary. Which parity latches
is unmeasured, and it does not affect whether a press moves the picture — only
whether the effective position sits one unit off what the register reads.

Zoom is quantised the same way. It moves both edges, so it moves the start, and
an odd zoom step would shift it by a sub-granule amount for the same reason —
but that is inference from the position measurement, not a separate reading.
Zoom was confirmed working through the OSD afterwards; it was not toggled
one unit at a time the way pan was.

## What one display shows

Measured 2026-08-06, creeping each display edge with the picture deliberately
overrunning the raster on all four sides:

```
horizontal    90 .. 1351  of 1445    margins  90 / 94    span 1261   87.3% shown
vertical      41 .. 1121  of 1126    margins  41 /  5    span 1080   95.9% shown
```

**The two axes are different kinds of limit.** `1121 - 41 = 1080` exactly: the
vertical visible region is precisely the active lines of a 1080-line output, and
the margins are the frame's own blanking — sync plus back porch before active,
front porch after, asymmetric in every video standard. This TV crops nothing
vertically, and any display fed the same raster shows the same lines, so **the
vertical is derivable and needs no measurement.**

The horizontal is not. 34 px between the end of hsync and the visible edge, 94
after, matches no blanking split, and the scaler's own blanking window is far
wider. That is the TV throwing away 6% of the line, and the next TV throws away
something else.

## The output hsync floor

`VDS_HB_SP` below **8** corrupts the display — measured 2026-08-06 by creeping it
down until the picture broke, with `VDS_HS_ST` at 10 and `VDS_HS_SP` at 56.

Left-hand corruption that survives everything else clears by moving `VDS_HS_ST`
and `VDS_HS_SP` from 10..56 to **62..77** — later in the line and less than a
third as wide. `snapshots/hsync-tuned-no-left-corruption-2026-08-06.json` is that
state: hsync 62..77, `VDS_HB_SP` 9, picture 100..1343.

**The floor is not "the window must clear the hsync guard".** That would make it
`VDS_HS_ST - 2`, but the snapshot above is clean with `VDS_HB_SP` at 9 and
`VDS_HS_ST` at 62 — 53 units *before* the pulse, deep inside where such a guard
would be.

So **8 is a number measured at one hsync setting**, the mechanism is unknown, and
the raster's own sync timing is an input to the geometry that nothing here
models. Two candidates remain and they are separable: the pulse **position** (62
vs 10) or its **width** (15 vs 46).

Related and unconfirmed: changing the output hsync width appears to change *which*
HSCALE values glitch, and several glitch while `produced` sits comfortably inside
the memory window. A pure resampling beat is a property of the ratio
`capture x 1024 / HSCALE` and has no reason to care about the output sync pulse,
so if that holds it points at an output-side timing effect rather than a beat.

A hand sweep recorded in `snapshots/hscale-728-best-of-session-2026-08-03.json`
agrees that these are bands and not thresholds: *"artifacts onset ~798 (x1.283),
partially recover ~725, best around 728"* — recovery on the far side of a bad
region.

Separating position from width needs two runs, because the 2026-08-06 change
moved both at once:

1. Hold the pulse width, move its position; sweep HSCALE; record which glitch.
2. Hold the position, change the width; sweep HSCALE again.

## The output front porch

The far end of the line needs a reserve, and the raster's edge is the wrong bound.
Measured on a 1916 px raster by creeping the display window: `VDS_DIS_HB_ST` is
good at **1900** and wrong at **1910**, so the floor is about **16 px**. Below it
three artefacts arrive together — a black bar down the right that no zoom closes,
grey pushed toward red, and a wrapping pixel down the *left* edge. All three clear
together.

`rasterTotal - 2` is not that bound. It exists only because `VDS_DIS_?B_ST` must
stay strictly below the total register and wraps rather than clamps, which left a
front porch of 6 px.

**The reserve is the measured floor and nothing more**, `OutputMode::FrontPorchMinPx`,
16 px. CEA-861's own minimum front porch is an order of magnitude above it -- 64 px
at 108 MHz, 77 at 129.6 -- and conforming to it at this end buys nothing, because
the MS9288A generates its own HDMI blanking from what it samples and never sees
ours.

**What the reserve costs is picture, not blanking.** It bounds where the produced
picture may END, so `fitToRaster` sizes the picture to stop there and the far
margin is then blanked off the end of that. At 1080p on a 2300 px raster, taking
the reserve from CEA's 77 px to the measured 16 moves the display window's close
from 2135 to 2196 and `VDS_HSCALE` from 474 to 461 -- 61 px more picture, 2.7% of
the line.

The floor is measured in PIXELS at one clock, so whether it is really a time is
untested. Sync and back porch remain times, because they place the pulse the
encoder locks to.

**Measured against bypass, which is the only reference for where the panel's
active area ends.** An 800x600 source runs straight through and fills the screen;
the scaled path is compared to it zoomed and panned so colour is against the edge,
because at the default framing the last thing on screen is captured input blanking
and not the output's own edge:

| | right edge, photo column |
|---|---|
| bypass 800x600 | 1155 |
| scaled 1080p, colour against the edge | 1156 |

At CEA's reserve the scaled path stopped 26 columns short of bypass. At the
board's floor it reaches it.

Vertically it is the standard's lines and needs no conversion. For 1080p that
gives an active window of **41..1121**, which is the encoder window measured
independently in *What one display shows* above — 1080 lines to the pixel, from the
standard rather than from the measurement.

`OutputTimings::activeStop` and `activeLinesStop` carry it, `Axis::farBound` applies
it, and `Geometry` holds it between solves because `apply()` reads the raster back
off the chip and no register records a porch. At the bench framing the picture ends
at 2278 of the 2300 px raster, and the display window closes at 2196 -- the far
margin blanking the pipeline's run-out off the end of it.

An `activeStop` of 0 means the raster's own edge, which is what bypass gets —
there is no solved raster to take a porch from.

**The near end is not yet bounded the same way.** `activeStart` is threaded but
always passed 0, so the picture is placed against the write floor rather than
against the back porch. Horizontally the two nearly coincide, 106 against CEA's
140. Vertically they do not: the picture starts at line 3 where the encoder
transmits from 41, so **39 lines are discarded off the top**. Feeding
`OutputTimings::activeLinesStart` in recovers them exactly.

[`investigations/output-front-porch.md`](investigations/output-front-porch.md) has
the three models that fitted the readings and were wrong, and why this is not the
retracted headroom rule returning.

## The two green regions in an IF line

Measured 2026-08-09 by creeping the capture window with the picture on screen.
They are different things:

```
capture start   76   green at the left edge     head green ends 76..90
capture start   91   clean
capture stop  1125   clean                      tail green starts at 1126
capture stop  1126   green at the right edge
```

### The head: derivable, and clipped

A green region ending between 76 and 91 is `HLOW_LEN / 2` = **90.5 units wide
starting at zero**, and `SP_RT_HS_ST` reads **0** — the retiming pulse begins at
the origin, so the input formatter counts from the sync's **leading edge**. The
pulse occupies IF `0 .. 90`.

```
duty      = HLOW_LEN / PLLAD_MD        both in ADC samples
syncUnits = ceil(IF line units x duty)
```

On the bench RiscPC `181 / 2553` = 0.0709 and `ceil(1277 x 0.0709)` = **91**.
`Tv5725::InputLine::measured()` computes exactly that, per solve, so a source with
a 0.121 duty excludes 155 units instead.

**It also explains `SP_RT_HS_SP`.** `gbs-control.ino` writes `PLLAD_MD x 0.93`.
`PLLAD_MD - SP_RT_HS_SP` is 179 against `HLOW_LEN`'s 181 — two samples in 2553 —
so **0.93 is `1 - duty`**, placing the retimed pulse on top of the incoming one.
That is why it is the fallback when the duty cannot be measured: it is the width
the retiming module is already configured for.

### The tail: an output-side artefact, and deliberately not clipped

The green starting at 1126 is **not** the same pulse, and not the same kind of
thing. It is ~120 units against the pulse's 91, and **no multi-bit field in
segments 0, 1 or 5 holds 1126, 2252, 151 or 302** — it is not an input setting.

The capture path stops writing video past IF 1125 and writes `Y=U=V=0`, which
decodes to green. It is neither unwritten frame buffer — genuinely unwritten
memory reads as random colour noise — nor anything `PB_CAP_OFFSET` places;
[`investigations/tail-green.md`](investigations/tail-green.md) carries both of
those as refuted, with the evidence that killed them.

Nothing clips it, and nothing should. **A test asserts the tail reaches the last
capturable unit**, because clipping both ends by `syncUnits` excludes
`1186..1276` — the clean porch *after* the green rather than the green itself —
and costs 91 units of zoom-out reach for nothing. Clipping the capture to hide an
artefact the capture does not produce would crop picture for nothing.

### The last two units of the line are not capture stops

`InputLine::lastCapture()` is `units - 2`, and both excluded units are excluded
for their own reason.

`units` is the wrap point — `IF_VB_ST` rolls at `2 x (VTOTAL + 1)` and
`IF_HB_ST2` at `IF_HSYNC_RST + 1` — and a window written onto it rolls rather
than clamping, which reads as the picture jumping rather than as a capture fault.

`units - 1` is the line reset value itself, and a capture stopping there stops
the input formatter producing pixels at all. Measured 2026-08-17 on a 1265-unit
line with `IF_HSYNC_RST` 1264: a stop of 1264 froze the picture on the last frame
the buffer held, with the raster nothing writes to showing green, every config
register still reading correct and the firmware loop still running; 1263 was
clean. **So the bound is the reset value, not a region before it** — a stop one
unit further back is not safer, and nothing in a register dump distinguishes the
frozen state.

### What is left

The clean capture region measures about `90 .. 1126`, which is 1036 units. The
default capture is 1009 units centred on the IF line, so it is nearly the right
*size* and about 30 units off in *position*.

The sliver at the right of the resting picture was read as the capture overhanging
the tail green. With the tail green now removed by the playback offset, what
remains of it — if anything — has not been re-measured.

## What the IF counter counts

The input formatter has one line counter, and the **line doubler is what sets its
rate**. With the doubler in the path it runs at twice the source's line rate; with
it bypassed it runs at the source's. Both axes are measured against that counter,
so the scan mode decides the unit on both:

| | doubler in path | doubler bypassed |
|---|---|---|
| `IF_HSYNC_RST` | `PLLAD_MD / 2` | `PLLAD_MD` |
| `IF_VB_ST`/`IF_VB_SP` wrap at | `2 x (VTOTAL + 1)`, half-lines | `VTOTAL + 1`, source lines |

RD-5725-1.1 states neither. `IF_VB_ST` and `IF_VB_SP` are documented only as
"vertical blanking start/stop position", 11 bits, with no unit; `IF_HSYNC_RST` is
"total pixel number per line". The wrap point is the only thing that can say, and
both readings are measured:

- **Doubled**, RiscPC 320x256@50, VTOTAL 311: `IF_VB_ST` **624** rolls the
  picture, 623 is stable. 624 = 2 x 312.
- **Bypassed**, RiscPC 640x480@75, VTOTAL 499: every `IF_VB_ST` from 999 down to
  500 leaves the picture untouched, because the counter never reaches any of
  them. 494 gives a picture, at the height a 488-unit capture predicts.

Sizing the vertical window for the wrong one of these puts `IF_VB_ST` past the
counter, where the write enable never falls: the frame buffer takes written and
unwritten lines alternately and the screen is per-line noise. Nothing reports it
and every register reads back correct.

`Tv5725::SourceMeasurement` holds the scan mode and `Tv5725::CaptureWindow` sizes
both lines from it. Half-line counting is how interlace is expressed — a field is
a whole number of lines plus a half — so it is the doubler's presence rather than
the source's own scan that decides.

## The sync processor counts in ADC samples

Three of its registers hold values above the 1277-unit IF line — `SP_RT_HS_SP`
2374, `SP_H_CST_SP` 1667, `STATUS_SYNC_PROC_HTOTAL` 2553, the last being
`PLLAD_MD` echoed back. And `HLOW_LEN` only agrees with the source's own timings
in ADC:

```
as ADC samples   181 / 2553 =  7.09%     AKF50 320x256 hsync: 36 / 512 = 7.03%
as IF units      181 / 1277 = 14.17%     twice the mode's sync width
```

**`SP_CS_CLP_ST`/`SP` are the exception**, at 26 and 150 — small enough to be
either, and misplaced under both readings:

```
as ADC samples ->  IF  13 ..  75    entirely inside the hsync pulse (IF 0..89.8)
as IF units    ->  IF  26 .. 150    64 units of sync, then 60 of back porch
```

The back porch runs IF 89.8..164.6, so the ADC takes its black reference at least
partly from the sync tip. `updateClampPosition()` computes those from
`1 + 0.010 x HTOTAL` and `2 + 0.058 x HTOTAL` in its non-csync branch, constants
that only make sense if the counter's zero is the sync's **trailing** edge — and
`SP_RT_HS_ST` = 0 says it is the leading one. Untested as a cause of anything;
the experiment is two writes and is reversible.

## Still open

- **The zigzag is not HSCALE-banded.** A manual sweep across the corrupted state
  found no value that cleared it. Cause unknown.
- **Whether a badly latched black level skews colour measurements.** Auto-clamp is
  gated on timing stability, so a clamp can latch while timing is unsettled and
  stay latched; a mode change appears to clear it. Until it is settled, any colour
  reading taken after a period of unstable sync is suspect. The test is to
  reproduce the state and force a mode change without touching the source.
  [`investigations/output-front-porch.md`](investigations/output-front-porch.md).
- **Where the MS9288A stops sampling the line.** Estimated at 1817 of a 1901 px
  line from one indirect observation, never measured, and it decides whether a
  conformant front porch costs visible picture. Creeping the display window down
  until a black bar appears measures it.
- **Whether the capture should be centred on the active region rather than the
  IF line.** It would fix the resting sliver, but the active region's edges are
  the thing the chip cannot see.
- **The vertical `VDS_?B_SP` floor is unmeasured.** The horizontal is 8, and the
  code assumes 0 vertically.
- **The vertical capture has no equivalent clip.** Only the hsync pulse is
  excluded; nothing has measured what the vsync serration costs in half-lines,
  and a guess there would crop picture rather than blanking.
- **Whether the horizontal floor is relative to `VDS_HS_ST`** — see above.

## See also

- [`investigations/moving-write-origin.md`](investigations/moving-write-origin.md)
  — why the write start is recomputed and not inherited, and the loss model that
  fitted the readings and was wrong anyway
- [`investigations/output-front-porch.md`](investigations/output-front-porch.md)
  — the far end of the line: three models that fitted and were wrong, and the
  black level that may latch
- [`riscpc-game-modes.md`](investigations/riscpc-game-modes.md) — the mode-side context, and
  earlier geometry work whose formulae this supersedes
- [`tv5725-chip.md`](tv5725-chip.md) — what the status registers do and do not
  measure
- [`rgbhv-bypass-trap.md`](rgbhv-bypass-trap.md) — when no scaling happens at all
- [`photos/2026-08-05-horizontal-geometry/`](photos/2026-08-05-horizontal-geometry/)
  — including photo 16, the frozen band that found the moving origin
