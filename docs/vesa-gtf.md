# VESA GTF — and why it is not the blanking default

`VESA-GTF-1.1.pdf` in this directory is the Generalized Timing Formula, v1.1
(1999). Given an active resolution and a refresh rate it produces a complete
raster — blanking, sync widths, porches and pixel clock — with no lookup table.

This page transcribes the formula so you don't need a PDF reader to use it, and
records the measurement that says **not** to use it for input-side defaults.

## Verdict first

GTF was evaluated as a way to default the capture window on a mode change: it is
the one published standard that maps line rate, which is measurable, onto blanking
duty cycle, which is not. **It does not work for this board's sources.** Measured
against the 28 modes of the stock Acorn `AKF50` mode file:

| predictor | mean abs error | worst |
|---|---|---|
| GTF curve | 7.04 pp | 23.2 pp |
| flat 24 % (`DEFAULT_H_ACTIVE_FRACTION` today) | 7.15 pp | 11.5 pp |
| flat 16 % | 2.97 pp | 18.1 pp |
| 20.3 % below 30 kHz, 16 % above | **2.30 pp** | 13.8 pp |

GTF is no better than the constant already in the code, and both are beaten by a
different constant. **Its gradient also has the wrong sign for these modes**: real
AKF50 blanking *falls* as frequency rises (21.5 % in the 15.6 kHz band, 16.1 %
above 28 kHz) where GTF says it should rise (10.8 % → ~21 %).

The reason is structural, not calibration. Written as an absolute time,

```
blank(T) = (C'/100)*T - (M'/100)*T^2  =  0.30*T - 0.003*T^2
```

is a downward parabola peaking at T = 50 µs, so **the most blanking GTF can ever
express is 7.50 µs, at any line rate at all.** A 15.6 kHz line carries 13.00 µs.
No choice of `C`, `M`, `K` or `J` reaches it.

**And GTF's good domain is the bypass domain.** It describes VESA DMT rasters
well (within ~2 pp, see below) — which are the ≥31 kHz PC modes this scaler passes
through rather than the 15 kHz retro sources it exists to convert. Even that
overlap is thinner than it looks: at 640x480@60, same 800-pixel total and same
25.175 MHz clock, RISC OS spends `116` pixels on blanking and `44` on **border**
where DMT spends `160` and `0`. The RISC PC is not emitting a VESA raster even at
VESA rates.

## What the mode files say instead

A RISC OS MDF states `sync, back porch, left border, display, right border, front
porch` explicitly, so it separates border from blanking — the distinction the
scaler physically cannot make, since a border is black *active* video. That makes
a stock mode file the best available **ground truth for evaluating a predictor**,
which is how it is used here. `tools/gbsc-pro-hwtest/mdf_modes.py` parses them.

It is not a runtime mechanism: the firmware sees sync edges, not mode files, and
must work on sources that have no MDF at all. See *The shape to implement* below.

**Use a stock Acorn file (`AKF50`, `AKF52`, …) for anything general.** The
`RetroScale` MDF in this project is hand-authored: correct for predicting the
bench machine, worthless as evidence about RISC OS timings at large.

For the bench mode, `AKF50` 320x256@50 — 512 pixels at 8 MHz, `VTOTAL` 312:

| | share of line |
|---|---|
| true blanking | 20.3 % (13.00 µs) |
| **border** | 17.2 % (88 px) |
| picture | 62.5 % (320 px) |
| raster-active (border + picture) | **79.7 %** — the most any capture can find |
| today's default, 0.76 × 1.04 over-capture | 79.0 % (**−0.7 pp**) |

**So the blanking default is already right, and blanking was the wrong thing to
chase.** What no formula can supply is the *border*, which is 17.2 % of the line
here and ranges from 3.1 % to 22.0 % across `AKF50`. That is what a user is
actually trimming away, and it is a per-mode authoring choice with no relationship
to line rate — which is why a mode-file lookup beats a curve, and why the vertical
side already needs the measured `DEFAULT_V_ACTIVE_FRACTION_50HZ = 0.82`
(`AKF50` gives 256/312 = 82.1 %).

## What the firmware does now, and what this page is still for

Two answers, and neither is a curve. Where the frame, the field rate and the
hsync duty match a raster the standards state, `Tv5725::SourceTiming` takes that
raster's own active window, both axes. Where nothing matches, `Axis` places the
window across the **envelope** of what real sources put on a line — 11.7% to
98.1% horizontally, 6.1% to 99.4% vertically — so nothing is cropped and what is
captured beyond the picture is black.
[investigations/vesa-modes-are-clipped-by-default.md](investigations/vesa-modes-are-clipped-by-default.md)
carries both, with the measurements.

**Every fraction quoted below as "today's default" is therefore gone**, along
with the centring, the over-capture factor and the 50/60 vertical split. What
this page is still for is the evidence: why no formula supplies the border, why
GTF emits no valid raster at 15 kHz, and the per-standard blanking figures the
envelope was taken from.

## The decision: select PAL or NTSC on field rate

The conclusion of this investigation, superseding every "which curve" question
below: **there is no curve. Select between two standards.**

| | start (sync+bp) | active width |
|---|---|---|
| PAL 625/50 | 16.25 % | 81.17 % |
| NTSC 525/59.94 | 14.48 % | 82.76 % |
| single set covering both, ±0.9 pp | 15.37 % | 82.16 % |

- **Key on field rate, never line rate.** 50 vs 59.94 Hz is a 20 % gap; the line
  rates are 15.625 vs 15.734 kHz, 0.7 % apart and not safely separable.
  `default_active_fractions()` already splits at `field_rate_hz < 55.0`.
- **Sync needs no branch.** It is 4.7 µs in both standards — 7.34 % against 7.40 %.
  Only the back porch meaningfully differs (5.7 µs vs 4.5 µs).
- **No formula connects them.** Fitting a line through the two points and
  extrapolating gives −126 % blanking at VGA rates, because they are 0.44 µs apart
  in line period. They are also not samples of a function: 5.7 µs and 4.5 µs are
  committee decisions, so a fitted curve models the difference between two
  standards bodies. Select; do not interpolate.
- **This restores a distinction the firmware always had.** The twelve shipped
  tables came in `pal_*` and `ntsc_*` pairs and were picked on video mode, so
  selecting on field rate is where that choice already lived.

**The trade, stated honestly.** This drops the "contains all active video" guarantee
for sources that are not broadcast-timed. For the primary user it costs nothing:
consoles target PAL or NTSC, and the broadcast active window is a *superset* of what
a console draws, so its own pillarbox appears as black rather than clipping. A
non-broadcast source pays — `AKF50` 320x256 loses 3.36 % of the line off the left
(17 of 512 px, 5.4 % of picture width) and gains black on the right. That is a trim,
and the RISC PC is a bench source, not the product's audience.

## The default that is actually wrong is the offset, not the width

The goal is a window that is centred, fully contains active video, and is as large
as possible. Stated that way the governing quantity is not blanking width but
**where active video starts relative to the HSync edge**, which is
`sync + back porch`.

Active video as a percentage of the line, 15 kHz sources:

| source | starts | ends | width | centre |
|---|---|---|---|---|
| AKF50 1056x256 | 11.7 % | 94.3 % | 82.6 % | 53.0 % |
| AKF50 320x256 | 12.9 % | 92.6 % | 79.7 % | 52.7 % |
| AKF50 640x256 | 13.1 % | 92.8 % | 79.7 % | 52.9 % |
| AKF50 768x288 | 13.9 % | 92.0 % | 78.1 % | 52.9 % |
| NTSC 525 | 14.5 % | 97.2 % | 82.8 % | 55.9 % |
| PAL 625/50 | 16.2 % | 97.4 % | 81.2 % | 56.8 % |
| **envelope** | **11.7 %** | **97.4 %** | **85.7 %** | **54.6 %** |

**Active video is not centred in the line.** Its centre sits at 53–57 %, never 50 %,
because `sync + back porch` (7.5–10.4 µs) is far longer than the front porch
(1.5–4.75 µs). Horizontal blanking is asymmetric and always has been.

`default_capture_window()` centres its window in the line, which is therefore the
wrong place. At 0.76 × 1.04 it produces `[10.5 %, 89.5 %]`:

| source | left | right |
|---|---|---|
| AKF50 320x256 | 2.4 % wasted on blanking | **3.1 % of picture cropped** |
| AKF50 1056x256 | 1.2 % wasted | **4.8 % cropped** |
| NTSC 525 | 4.0 % wasted | **7.7 % cropped** |
| PAL 625/50 | 5.8 % wasted | **7.9 % cropped** |

The width is very nearly right — 79.0 % against the 79.7 % the bench mode needs.
**It is the placement that loses picture**, and it loses it off the right-hand edge
on every source checked, while wasting capture on back porch at the left.

Start at ~11.7 % of the line rather than centring, and widen to ~86 %, and the
envelope above is contained with nothing cropped. Narrower is available if you
split by field-rate family, at the cost of the guarantee.

**Which standard is the right source for 15 kHz, then:** none of them alone. GTF is
undefined there (above). PAL and NTSC both start *later* than every Acorn mode, so
defaulting to PAL's 16.2 % would crop the left edge of a RISC PC source. The usable
answer is the envelope of real sources, which is wider than any single standard.

**Caveat before implementing.** These are percentages of the line measured from the
HSync leading edge. Translating them into `IF_HB_ST2`/`IF_HB_SP2` needs the capture
write origin, which
[geometry_math.py:118](../tools/gbsc-pro-hwtest/geometry_math.py#L118) records as
never measured — assumed to be `VDS_HB_SP`. Measure that first; a placement rule is
only as good as its origin.

The rest of this page is the formula itself, kept because GTF remains the right
tool for *generating* an output raster (below), and because a negative result is
worth being able to re-check.

## Why it is in this repo: guessing blanking on a mode change

**Blanking cannot be measured.** A border is black *active* video, electrically
identical to back porch, so sync-domain measurement finds the raster and never the
picture inside it (`CLAUDE.md`). On a mode change the firmware can measure the
raster — field rate, `VTOTAL`, and therefore the line rate — but nothing tells it
where active video starts within the line.

GTF is a candidate answer, because §2.4 is exactly the missing relationship: it
maps horizontal period, which *is* derivable, onto blanking duty cycle, which is
what you need.

```
[IDEAL DUTY CYCLE] = [C'] - [M'] * [H PERIOD] / 1000
```

**The duty-cycle form is what makes it usable here, and this is the important
part.** The horizontal axis has no native resolution — the chip sees sync edges,
the source's pixel clock is unknowable, and capture is in ADC sample units you
choose with `PLLAD_MD`. A duty cycle is dimensionless, so it survives all of that:

```
blanking (IF units) = duty_cycle x line_units      # line_units = IF_HSYNC_RST + 1
```

No pixel clock appears. That is why a *fraction* is the right currency for this
problem and an absolute time or pixel count is not.

The existing starting guesses in
[geometry_math.py:179-229](../tools/gbsc-pro-hwtest/geometry_math.py#L179-L229)
already work this way — `DEFAULT_H_ACTIVE_FRACTION = 0.76` and two vertical
constants selected by field rate. GTF's contribution would be to replace a
constant with a curve.

## GTF is undefined below 21.43 kHz

The sync rule in §2.2 settles the question outright. GTF puts the **trailing** edge
of H sync exactly at the midpoint of blanking, so stage 2 reads

```
[H BACK PORCH]  = [H BLANK]/2
[H FRONT PORCH] = [H BLANK]/2 - [H SYNC]
```

`[H SYNC%]` is 8 % of the *total* line, so the front porch goes negative as soon as
the blanking duty cycle falls below `2 x 8 % = 16 %`:

```
30 - 300*T/1000 = 16   ->   T = 46.67 us   ->   f = 21.43 kHz
```

| line rate | duty | sync | back porch | front porch | |
|---|---|---|---|---|---|
| 15.62 kHz | 10.8 % | 5.12 µs | 3.46 µs | **−1.66 µs** | invalid |
| 18.00 kHz | 13.3 % | 4.44 µs | 3.70 µs | **−0.74 µs** | invalid |
| 21.43 kHz | 16.0 % | 3.73 µs | 3.73 µs | 0.00 µs | the floor |
| 31.47 kHz | 20.5 % | 2.54 µs | 3.25 µs | +0.71 µs | ok |

**So GTF is not a candidate for 15 kHz sources — it does not merely predict them
badly, it emits no valid raster there at all.** Every 15 kHz figure quoted for GTF
elsewhere on this page is an extrapolation of a formula outside its own domain.

One nuance worth keeping: GTF's *start* of active video, `sync + blank/2`, comes to
8.58 µs (13.4 % of the line) at 15.625 kHz, which is close to `AKF50`'s measured
12.9 %. It is the blanking *width* and the front porch that are impossible. Getting
the left edge roughly right is not enough to rescue it.

## Where GTF *is* accurate: VESA DMT

This is the case for GTF, and it is real — but read it alongside the verdict
above: these are published **VESA** timings, and no source on this bench emits
them. It is included because it explains *why* GTF looks promising before you
measure the actual sources, and where the formula is genuinely trustworthy.

Checked against DMT/CEA published timings. `err` is percentage points of the line;
`const` is what the flat 0.76 in `geometry_math.py` would have said.

| mode | H period | H blank real | GTF | err | const err | V blank real | GTF | err |
|---|---|---|---|---|---|---|---|---|
| 640x480@60 DMT | 31.78 µs | 20.0 % | 20.5 % | **+0.5** | +4.0 | 45 | 18 | **−27** |
| 800x600@60 DMT | 26.40 µs | 24.2 % | 22.1 % | **−2.2** | −0.2 | 28 | 22 | **−6** |
| 1024x768@60 DMT | 20.68 µs | 23.8 % | 23.8 % | **−0.0** | +0.2 | 38 | 28 | **−10** |
| 1280x1024@60 DMT | 15.63 µs | 24.2 % | 25.3 % | **+1.1** | −0.2 | 42 | 36 | **−6** |
| 1920x1080@60 CEA | 14.81 µs | 12.7 % | 25.6 % | **+12.8** | +11.3 | 45 | 38 | **−7** |
| 720x576@50 PAL | 32.00 µs | 16.7 % | 20.4 % | **+3.7** | +7.3 | 49 | 18 | **−31** |
| 720x480@60 NTSC | 31.78 µs | 16.1 % | 20.5 % | **+4.4** | +7.9 | 45 | 18 | **−27** |

**On DMT, horizontally, it works and beats the constant.** Mean absolute error 3.5
points against the constant's 4.4; excluding 1080p, 2.0 against 3.3. On the
VESA-family modes it is within ~2 points.

**This does not carry over to the sources here.** Against `AKF50` the same curve
scores 7.04 pp — see the verdict. A RISC OS mode at 640x480@60 has the same total
and clock as the DMT mode but spends 44 of DMT's blanking pixels as border, so
matching DMT is not the same as matching what arrives at the ADC.

**Vertically it is wrong every time, and should not be used.** GTF *minimises*
vertical blanking — a 550 µs floor plus a one-line porch — while every real mode
carries more, inherited from legacy timing. It under-predicts by 6 to 31 lines.
The field-rate constants already in `geometry_math.py` are much better, and
vertical is the axis where you least need a model anyway: `VTOTAL` is directly
measurable.

**1080p is outside the model.** CEA's reduced blanking is not a GTF curve, and
neither the formula nor the constant predicts it. If a source is modern enough to
use reduced blanking, this whole approach needs Secondary GTF or CVT-RB instead.

## Below 30 kHz, use the broadcast standards instead

This is the band that matters — 15 kHz retro sources are most of what this board
sees — and it is the band where GTF fails hardest. Blanking figures below are
ITU-R BT.470 (PAL, 12.05 µs) and SMPTE 170M (NTSC, 10.9 µs).

| mode | T | real blanking | GTF | GTF err | flat 24 % err |
|---|---|---|---|---|---|
| PAL/SECAM 625/50 | 64.00 µs | 12.05 µs (18.8 %) | 6.91 µs (10.8 %) | **−8.0 pp** | +5.2 pp |
| NTSC 525/59.94 | 63.56 µs | 10.90 µs (17.2 %) | 6.95 µs (10.9 %) | **−6.2 pp** | +6.8 pp |
| RiscPC, PAL rate | 64.31 µs | 12.05 µs (18.7 %) | 6.89 µs (10.7 %) | **−8.0 pp** | +5.3 pp |
| EGA 640x350@60 | 49.20 µs | 9.84 µs (20.0 %) | 7.50 µs (15.2 %) | −4.8 pp | +4.0 pp |
| VGA 640x480@60 | 31.78 µs | 6.36 µs (20.0 %) | 6.50 µs (20.5 %) | +0.5 pp | +4.0 pp |

**In this band GTF is worse than the flat constant it would replace**, and it
fails structurally rather than by calibration. Written as an absolute time,

```
blank(T) = (C'/100)*T - (M'/100)*T^2  =  0.30*T - 0.003*T^2
```

which is a downward parabola peaking at `T = C'/(2*M'/100)` = 50 µs. **The most
blanking GTF can express is 7.50 µs, at any line rate at all.** PAL needs 12.05 µs.
No choice of `C`, `M`, `K` or `J` in that form reaches a TV-rate line.

### The fix is two constants, not a curve

| standard | line period | blanking | duty | H active fraction |
|---|---|---|---|---|
| PAL / SECAM 625/50 | 64.000 µs | 12.05 µs | 18.83 % | **0.812** |
| NTSC 525/59.94 | 63.556 µs | 10.90 µs | 17.15 % | **0.829** |

Selected by field rate, these land within 0.1 pp on their own standard and 1.7 pp
across it — against 5–7 pp for the current flat 0.76.

**Blanking below 30 kHz does not vary with frequency, so a curve is the wrong
shape for it.** Every 15 kHz source drives a TV and must respect the standard's
flyback interval; what varies is *which standard*, which is field rate. PAL and
NTSC also defeat a GTF-form fit outright — PAL has the **longer** line and **more**
blanking, the opposite sign to GTF's gradient. Fitting the two points implies
M′ = −3.78 against GTF's +300.

This is the mechanism behind the empirical note already in `geometry_math.py`:
horizontal barely moves, vertical splits hard on field rate. **Horizontal blanking
is mandated by the display standard; vertical active height is the source's free
choice.** The RISC PC illustrates both — it sits near the PAL blanking interval
because it drives a PAL-rate raster, while its 256 active lines in a 312-line
frame are entirely its own decision, which is why the vertical side needs the
measured `DEFAULT_V_ACTIVE_FRACTION_50HZ = 0.82` and no formula.

Note that `AKF50` uses **13.00 µs** at 15.625 kHz where broadcast PAL specifies
12.05 µs. Acorn was near the standard, not on it, so the broadcast figure is a
sanity check rather than a source of defaults — one more reason to read the mode
file rather than a standard.

### The shape to implement

Superseding the band-split idea above: **GTF earns no branch.**

| band | horizontal | vertical |
|---|---|---|
| below ~30 kHz | constant ≈ 0.80 active | measured constants (0.82 / 0.95) |
| above ~30 kHz | constant ≈ 0.84 active | measured constants |

Two constants split at 30 kHz score 2.30 pp mean error across `AKF50`, against
GTF's 7.04 and today's flat 0.76 at 7.15.

**Mode files are bench evidence, not a runtime lookup.** The firmware cannot read
an MDF — it sees sync edges — so `mdf_modes.py` is for checking predictions with a
machine in front of you. It is also the wrong shape as a design: the scaler must
accept whatever timings arrive, from any source, and a table keyed on one
machine's mode file fails that on the second source you plug in. This is settled
project direction, not a preference — an earlier attempt went the other way,
hand-authoring a `RetroScale` mode tuned until the picture fitted, and was
abandoned because it fixes one machine rather than the scaler.

So: a constant from measurable quantities, deliberately over-capturing so the
border shows as black edges rather than cropping picture, and then a real trim
control. These are *defaults* the user adjusts, so none of it has to be exact —
and the residual is dominated by the border, which is a per-mode authoring choice
with no relationship to line rate. **No formula can supply it, which is the
argument for the tweak UI rather than for a better curve.**

## It does not know about the headroom rule

GTF hands you `HTOTAL` and a pixel clock; whether the scaler can finish reading a
line inside that period is a separate feasibility question, and lives in
[geometry_math.py](../tools/gbsc-pro-hwtest/geometry_math.py).

## The other use: choosing an output raster

EDID is unreachable — the MS9288A is on no MCU's I²C bus — so every output raster
is chosen blind. GTF is also the principled way to *generate* a mode to send, as
opposed to guessing an input's blanking: it is the curve monitors of that era were
designed against, so a GTF-derived raster is likelier to lock than an invented one.
That is the direction the formula was actually written for, and stage 1 below is
complete for it.

## Default constants (§3)

| symbol | default | what |
|---|---|---|
| `[MARGIN%]` | 1.8 % | overscan margin, as % of active. Off unless requested |
| `[CELL GRAN]` | 8 px | character cell granularity; all H values snap to it |
| `[MIN PORCH]` | 1 | minimum front porch, in lines (V) and cells (H) |
| `[V SYNC RQD]` | 3 lines | vertical sync width |
| `[H SYNC%]` | 8 % | horizontal sync width, as % of total line period |
| `[MIN VSYNC+BP]` | 550 µs | minimum vsync + back porch interval |
| `[M]` | 600 %/kHz | blanking formula gradient |
| `[C]` | 40 % | blanking formula offset |
| `[K]` | 128 | blanking time scaling factor |
| `[J]` | 20 % | scaling factor weighting |

`ROUND(x, 0)` throughout is Excel's — round half **away from zero**, not
banker's rounding. Python's built-in `round()` is wrong here.

## The blanking curve (§2.4, §2.6)

The whole standard rests on one idea: blanking duty cycle is a linear function of
the horizontal period.

```
[IDEAL DUTY CYCLE] = [C'] - [M'] * [H PERIOD] / 1000      # H PERIOD in µs
```

`C'` and `M'` are `C` and `M` scaled by `K` and `J`, so a single coefficient can
re-aim the curve:

```
[C'] = ((C - J) * K / 256) + J
[M'] = K / 256 * M
```

At `K = 256` these collapse to `C' = C`, `M' = M`; at `K = 0`, to `C' = J`,
`M' = 0`. The default `K = 128` gives `C' = 30 %`, `M' = 300 %/kHz`.

**Secondary GTF** (§6) is the same formula with different constants — start
frequency 85 kHz, `C = 40`, `M = 3600`, `K = 128`, `J = 35` — describing displays
that accept reduced blanking above that frequency. It has no definition below its
start frequency.

## Sync position and polarity (§2.2, §2.3)

- H sync is a fixed percentage of the line; **its trailing edge sits exactly
  midway through the blanking period**. That is what fixes the front/back porch
  split, and why `[H BACK PORCH] = [H FRONT PORCH] + [H SYNC]`.
- V sync is a whole number of lines, positioned by a fixed front porch. Interlace
  adds a half line on alternate fields.
- Polarity carries meaning: **default GTF is H-negative, V-positive**; secondary
  GTF is H-positive, V-negative. A monitor reads the pair to know which curve is
  in use. GTF's 640x480 at 31.5 kHz has horizontal timings identical to standard
  VGA, so the polarities were deliberately chosen to differ from VGA's.

## Stage 1, driven by vertical refresh (§7.3)

The path you want when you know the resolution and the refresh rate. Two other
entry points exist — driven by horizontal frequency (§7.4) and by pixel clock
(§7.5) — computing the same parameters in a different order; see the PDF.

```
 1  [H PIXELS RND]      = ROUND([H PIXELS]/[CELL GRAN RND],0) * [CELL GRAN RND]
 2  [V LINES RND]       = interlaced ? ROUND([V LINES]/2,0) : ROUND([V LINES],0)
 3  [V FIELD RATE RQD]  = interlaced ? [I/P FREQ RQD]*2 : [I/P FREQ RQD]
 4  [TOP MARGIN]        = margins ? ROUND([MARGIN%]/100*[V LINES RND],0) : 0
 5  [BOT MARGIN]        = same
 6  [INTERLACE]         = interlaced ? 0.5 : 0
 7  [H PERIOD EST]      = ((1/[V FIELD RATE RQD]) - [MIN VSYNC+BP]/1e6)
                          / ([V LINES RND] + 2*[TOP MARGIN] + [MIN PORCH RND]
                             + [INTERLACE]) * 1e6
 8  [V SYNC+BP]         = ROUND([MIN VSYNC+BP]/[H PERIOD EST],0)
 9  [V BACK PORCH]      = [V SYNC+BP] - [V SYNC RND]
10  [TOTAL V LINES]     = [V LINES RND] + [TOP MARGIN] + [BOT MARGIN]
                          + [V SYNC+BP] + [INTERLACE] + [MIN PORCH RND]
11  [V FIELD RATE EST]  = 1/[H PERIOD EST]/[TOTAL V LINES] * 1e6
12  [H PERIOD]          = [H PERIOD EST] / ([V FIELD RATE RQD]/[V FIELD RATE EST])
13  [V FIELD RATE]      = 1/[H PERIOD]/[TOTAL V LINES] * 1e6
14  [V FRAME RATE]      = interlaced ? [V FIELD RATE]/2 : [V FIELD RATE]
15  [LEFT MARGIN]       = margins ? ROUND([H PIXELS RND]*[MARGIN%]/100
                                          /[CELL GRAN RND],0)*[CELL GRAN RND] : 0
16  [RIGHT MARGIN]      = same
17  [TOTAL ACTIVE PIXELS] = [H PIXELS RND] + [LEFT MARGIN] + [RIGHT MARGIN]
18  [IDEAL DUTY CYCLE]  = [C'] - [M']*[H PERIOD]/1000
19  [H BLANK (PIXELS)]  = ROUND([TOTAL ACTIVE PIXELS]*[IDEAL DUTY CYCLE]
                                /(100-[IDEAL DUTY CYCLE])/(2*[CELL GRAN RND]),0)
                          * (2*[CELL GRAN RND])
20  [TOTAL PIXELS]      = [TOTAL ACTIVE PIXELS] + [H BLANK (PIXELS)]
21  [PIXEL FREQ]        = [TOTAL PIXELS] / [H PERIOD]          # MHz
22  [H FREQ]            = 1000 / [H PERIOD]                    # kHz
```

Step 7 is an *estimate* because `[MIN VSYNC+BP]` is a time, not a whole number of
lines. Steps 8–12 turn that estimate into an exact line count and then back into
an exact period — that iteration is the trick the whole formula turns on.

Note step 19's rounding to a **double** cell (`2*[CELL GRAN RND]`), which is what
keeps the half-blanking split in step 18 below landing on a cell boundary.

## Stage 2, the parameters you actually program (§7.6)

```
[H SYNC (PIXELS)]        = ROUND([H SYNC%]/100*[TOTAL PIXELS]/[CELL GRAN RND],0)
                           * [CELL GRAN RND]
[H FRONT PORCH (PIXELS)] = ([H BLANK (PIXELS)]/2) - [H SYNC (PIXELS)]
[H BACK PORCH (PIXELS)]  = [H FRONT PORCH (PIXELS)] + [H SYNC (PIXELS)]
[TOTAL LINES PER FRAME]  = interlaced ? 2*[TOTAL V LINES] : [TOTAL V LINES]
```

The rest of stage 2 is unit conversion — the same quantities in µs, in character
cells, as duty-cycle percentages — and is only worth reading in the PDF if you
need one of those forms.

## Worked check

Transcription verified against the canonical GTF modes. `1024x768@60`, no
margins, non-interlaced:

| | |
|---|---|
| pixel clock | 64.11 MHz |
| horizontal | 1024 / 1080 / 1184 / 1344 (active, sync start, sync end, total) |
| vertical | 768 / 769 / 772 / 795 |
| H freq | 47.700 kHz |

`640x480@60` → 23.86 MHz, 640 656 720 800 / 480 481 484 497.
`1280x1024@60` → 108.88 MHz, 1280 1360 1496 1712 / 1024 1025 1028 1060.

These match the output of the standard `gtf(1)` utility, which is the usual
independent check if you reimplement this.

## What GTF is not

- **Not DMT.** VESA's Discrete Monitor Timings are a fixed table of named modes;
  GTF is a curve. A mode from the DMT table is generally *not* what GTF computes
  for the same resolution and rate.
- **Not CVT.** CVT (2003) supersedes GTF and adds reduced-blanking modes intended
  for flat panels. If a display is modern enough to want CVT-RB, GTF's blanking
  will be generously long — correct, but wasteful of clock.
- **Not a guarantee.** GTF says what a compliant monitor should accept, and
  nothing on this board can confirm the TV is one. It narrows the guess; it does
  not remove it.

## See also

- [tv5725-chip.md](tv5725-chip.md) — the two timing domains, and why an
  output-side raster and an input-side capture window are different things.
- [geometry_math.py](../tools/gbsc-pro-hwtest/geometry_math.py) — the headroom
  rule, which decides whether a raster GTF proposes is actually reachable.
