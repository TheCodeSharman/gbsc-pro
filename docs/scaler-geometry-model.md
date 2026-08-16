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
| `capture` | `IF_HB_ST2 - IF_HB_SP2`, samples | `IF_VB_ST - IF_VB_SP`, **half-lines** |
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

### The tail: not derivable, and deliberately not clipped

The green starting at 1126 is **not** the same pulse. It is ~120 units against
the pulse's 91, it starts 151 units before the end of the line, and **no
multi-bit field in segments 0, 1 or 5 holds 1126, 2252, 151 or 302** — it is not
a setting.

Nothing clips it. **A test asserts the tail reaches `units - 1`**, because
clipping both ends by `syncUnits` excludes `1186..1276` — the clean porch *after*
the green rather than the green itself — and costs 91 units of zoom-out reach for
nothing.

### What is left

The clean capture region measures about `90 .. 1126`, which is 1036 units. The
default capture is 1009 units centred on the IF line at `134 .. 1143`, so it is
nearly the right *size* and 30 units off in *position*, and overhangs the tail
green by 17. That sliver at the right of the resting picture is the remaining
fault, and bounding it needs a number nobody can derive yet.

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

- **Where the tail green comes from.** ~120 units starting 151 before the end of
  the line, on a source whose sync pulse is 91 and lives at the head. Not a
  register value, and **not a boundary in the mode**: IF 1126 is source pixel
  451.5, which is 21.5 px into AKF50's 44 px right border, with the display
  ending at 1072.5 and the front porch starting at 1182.2. No single origin
  offset reconciles it with the head, which is already pinned. So it is not the
  source's blanking.

  **Untested: the tail green is the head's HSYNC pulse, reached by the capture
  window wrapping past the end of the line.** That would make it the same pulse
  after all, seen from the other side, and it fits what the window does — the
  progressive stop follows the line length and may roll past it. Two numbers
  argue against it as it stands and are what to settle first: the tail is ~120
  units against the pulse's 91, and it stops around 1246 rather than running to
  the line's end at 1276. The cheap discriminator is whether its width tracks
  `HLOW_LEN` — change the sampling divider or drive a source with a different
  sync duty, and a wrapped pulse must move with it while an unrelated region
  will not.
- **The zigzag is not HSCALE-banded.** A manual sweep across the corrupted state
  found no value that cleared it. Cause unknown.
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
- [`riscpc-game-modes.md`](investigations/riscpc-game-modes.md) — the mode-side context, and
  earlier geometry work whose formulae this supersedes
- [`tv5725-chip.md`](tv5725-chip.md) — what the status registers do and do not
  measure
- [`rgbhv-bypass-trap.md`](rgbhv-bypass-trap.md) — when no scaling happens at all
- [`photos/2026-08-05-horizontal-geometry/`](photos/2026-08-05-horizontal-geometry/)
  — including photo 16, the frozen band that found the moving origin
