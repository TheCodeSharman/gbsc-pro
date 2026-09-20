# 800x600@60 loses both edges, and every stage between source and panel measures correct

The bench RISC PC on `vga` at AKF50's 800x600@60, scaled to the solved raster,
shows the PM5544 card with its outermost castellation columns missing at left
and right. The same source in RGBHV bypass shows the card complete.

**It is three separate faults sharing one symptom.** One is closed; two are not.

| | what it is | state |
|---|---|---|
| head blanking | `IF_HBIN_SP` blanks into the capture where the doubler is bypassed | **closed** |
| the line offset | the video sits 6.4% of a line behind the counter's origin where the doubler is bypassed, and nothing behind it where it is in | **closed**: the placement translates by it |
| the transmitted window | the emitted active window is wider than what reaches the panel, on the two short rasters | open; `VDS_HSYNC_RST` is the lever |

## The source, exactly

AKF50's `BaseModes/RiscPC/AKF50` states it, so none of this is inferred:

```
# 800 x 600 (60Hz)
pixel_rate: 40000
h_timings: 128,48,40,800,40,0        # sync, back porch, L border, active, R border, front porch
#    VESA: 128,88, 0,800,0,40
v_timings: 4,23,0,600,0,1
```

Total 1056 pixels at 40 MHz, so the line rate is **37,879 Hz** — which is what
the engine measures. **The front porch is zero**: the right border runs to the
end of the line. RISC OS spends 40 pixels a side on border where DMT spends
none, and takes them out of the porches.

At `PLLAD_MD` 1124 one source pixel is 1.0644 IF units, so the card's 800 active
pixels occupy IF 230..1082 and the engine's capture window lands on exactly
those two numbers.

## Every stage measures correct

| stage | measured | expected |
|---|---|---|
| line rate | 37,879 Hz | 40 MHz / 1056 exactly |
| sync duty | 12.19% | 128/1056 = 12.12% |
| capture window | IF 230..1082 | the standard's active window, to the unit |
| `produced` against the display window | 1541 against 1541 | fits to +1 |
| `STATUS_SYNC_PROC_HTOTAL` | 1125 | the divider is latched |

## Fault 1: the head blanking, closed

`IF_HBIN_SP` is two things and `IF_LD_RAM_BYPS` decides which. With the
line-double FIFO in circuit it is that FIFO's line reset: moving it pans the
whole picture, the span staying constant. With the FIFO bypassed it is a
**blanking edge in the capture window's own units**, so any value it holds crops
the left of the picture on top of the crop the capture window already applies.

Swept on 800x600@60 with the picture fitted inside the panel, recording where
content starts:

| `IF_HBIN_SP` | 2 | 100 | 170 | 200 | 272 | 340 | 400 | 550 |
|---|---|---|---|---|---|---|---|---|
| content start, photo px | 174 | 174 | 174 | 174 | **192** | 264 | 325 | 471 |

Flat below the knee, 1:1 above it. Every scaling table shipped 136..272 and
`BringUp` inherited 272, which sits inside the picture on this mode. The cost by
mode, all at `PLLAD_MD` 1124:

| mode | picture starts | 272 crops |
|---|---|---|
| 800x600@60 | IF 230 | the outermost castellation column |
| 640x480@60 | IF 194 | the whole left colour-bar column, ~8.6% of picture width |
| 384x288@70 | later than 272 | nothing |

**The bench's everyday 15 kHz source is line-doubled**, where the register is a
line reset and 272 is load-bearing — at 2 the picture shifts right and runs off.
That is why the fault went unseen for as long as it did.

`InputFormatter::applyScanMode()` now owns the field alongside the other three
that decide the scan mode: `NoHeadBlanking` where the doubler is bypassed,
`LineDoubleReset` where it is in. Zero is not available — against
`IF_HBIN_ST` 0 it blanks the whole line and the screen goes black.

Refuted as ways to take the block out of the path: `IF_HS_INT_LPF_BYPS` = 1
changes the picture by one photo pixel and leaves `IF_HBIN_SP` just as live;
`IF_SEL_HSCALE` = 0 shifts the picture but does not disconnect it either. Only
the interpolator and low-pass filter are bypassable — the block's blanking and
line-reset timing are always in circuit.

## Fault 2: the origin is one pipeline lag late, and one sync width early on inverted hsync

The IF line's origin is not where the capture arithmetic places it, and the
error has two terms. In IF units, the source's first active pixel lands at

```
measured = sync + back porch + border + LAG - (hsync inverted ? sync : 0)
```

with **LAG ≈ 72 IF units**, constant across four modes spanning three
resolutions, both hsync polarities and sync widths from 64 to 128 source pixels.

| mode | `sync_pol` | sync, src px | knee, IF | `sync+bp+border`, IF | offset | sync, IF | LAG |
|---|---|---|---|---|---|---|---|
| 800x600@60 | 0 | 128 | 302.1 | 229.9 | **+72.2** | 136.2 | +72.2 |
| 800x600@56 | 0 | 72 | 277.9 | 208.6 | **+69.3** | 79.0 | +69.3 |
| 384x288@70 | 2 | 68 | 356.5 | 281.0 | **+75.5** | 127.4 | +75.5 |
| 640x480@75 | 3 | 64 | 211.8 | 227.5 | **−15.7** | 85.6 | +69.9 |

All four at `PLLAD_MD` 1124 with the line doubler bypassed, so one IF unit is
one ADC sample. The offset column is what a model without the sync term has to
explain: it changes sign. The LAG column is what one bit of extra state reduces
it to.

> **IT IS A FRACTION OF THE LINE, NOT A COUNT OF SAMPLES AND NOT A TIME**, and
> one divider cannot tell the three apart -- which is why the table above reads
> as a constant. A second measurement at another divider and another line rate
> separates them:
>
> | | samples | of the line | as time |
> |---|---|---|---|
> | 800x600@60, `PLLAD_MD` 1124 | 72 | 0.0641 | 1.69 us |
> | 320x256@50 at 480p, `PLLAD_MD` 1880 | 118 | 0.0628 | 4.02 us |
>
> The fractions agree to 2%; the counts disagree by 64% and the times by 2.4x.
> `VideoSourceLine::CaptureLagFraction` is 0.0640 and the lag is computed per
> line from it, so a mode change re-derives it with the divider.
>
> **And that settles the doubled path.** The second reading is the whole
> displacement between the two scan modes, so once the bypassed line accounts
> for all of it the doubled line's lag is zero -- which `IF_HBIN_SP` placing
> the picture itself already implied, and which is what keeps a doubled output
> untouched by any of this.
>
> **THE FRACTION IS 0.0539, AND IT IS MEASURED AS THE DIFFERENCE IT IS USED
> AS.** The readings above are ABSOLUTE -- a knee against each mode's stated
> timings -- and the paragraph above presses them into service as the scan-mode
> difference by assuming the doubled lag is zero. Measured directly, by taking
> the same feature on the same source in both scan modes, the difference is
> 0.0539 and not 0.0640. The knee's systematic biases -- the aperture's far-end
> inset, the interpolation, the edge threshold -- are shared by both readings
> and fall out of a difference where they do not fall out of an absolute
> reading; against the mode file each counter is out by a further 0.010 to
> 0.020 of a line, which is that bias rather than a second finding.
>
> **THE FRAME HAS ONE TOO, AND IT IS NEGATIVE.** The undoubled frame delivers
> video one and a half SOURCE LINES EARLY where the undoubled line delivers it
> late. The two pipelines are not the same one and nothing requires them to
> agree in sign. `VideoSourceLine::FrameLagLines`, and
> `docs/known-issues.md` carries what one source cannot settle about either
> constant.
>
> Measured on the bench, RiscPC X320 Y256 F50, automation frozen, the capture
> window crept a unit at a time until the source's flashing border entered the
> picture, with `RetroScaler-Acorn.mdf` naming the feature -- active video at
> 110..430 of 512 and lines 36..292 of 312:
>
> | the card's edge | doubled | undoubled |
> |---|---|---|
> | top / bottom, source lines | 30.0 / 288.5 | 28.5 / 287.0 |
> | right, fraction of the line | 0.8300 | 0.8839 |
>
> The symptom it closes: one stored framing took different picture at 480p and
> 576p than at 1080p, the source's border standing down the right and across the
> bottom while the matching content fell off the left and the top.

**The sync term is decided by a bit the chip already reports.**
`STATUS_SYNC_PROC_HSPOL` reads 1 on AKF50's `sync_pol` 0 and 2 modes and 0 on
its `sync_pol` 3 modes, `sync_pol` bit 0 being hsync. Where the pulse is
positive-going the IF origin is its leading edge; where it is inverted the
origin is the trailing edge, so the sync interval is already behind the origin
and the picture arrives one sync width sooner.

> **THE SYNC TERM IS WITHDRAWN, AND MUST NOT BE REINSTATED.** It was measured
> against a chip that saw the source's own polarity, and `SP_HS_INV_REG` is now
> written before the line is counted, so every source reaches the counter as a
> low-active pulse at the head and one origin serves them all. Left in, the
> term was applied a second time to the very sources the normalisation had
> already corrected: at 640x480@60 the capture floor sat 181 units early, which
> is a black bar down a fifth of the screen and the right of the picture off
> the end of the line. What survives here is the LAG column and the four-mode
> table it rests on.
> `the-capture-floor-followed-a-normalised-polarity.md`

### What the engine does with it

`VideoSourceLine` carries the lag. `firstCapture()` is
`lag + headBlanking + syncUnits`, and `videoAt()` maps a position a standard
states as a fraction of ITS line onto this one by adding the lag.
`fractionAt()` inverts it, so one mapping joins a position in the SOURCE to a
position in the counter and every caller goes through it:
`ActiveImage::place()` puts both a tuned framing and an untuned axis through
`videoAt()`, and `clampToLine()` seeds back through `fractionAt()`.

**A LAG APPLIED ONLY TO THE FLOOR IS NOT APPLIED AT ALL.** The floor bounds
where a window may open, and a framing placed above it never meets it -- which
is the state anchoring the framing to `units()` left behind, `firstCapture()`
having been the only term carrying the lag into the start. The symptom is the
source's border down the left of every undoubled output while the doubled one
looks right, and no register reads wrong.

The control bound moves with the lag too. `CaptureWindow::reachOn()` is
`lastReachable()`, the last position a framing may NAME, rather than
`lastCapture()`, the last unit the counter may open on: a framing is a
proportion of the source and the bound has to be in the same units, or the far
end of the pan gains a dead zone one lag wide. `SourceMeasurement::readSource()` reads `STATUS_SYNC_PROC_HSPOL` to decide the
normalisation and nothing else; the polarity reaches no solver.

**The lag is not applied where the line doubler is in circuit.** `IF_HBIN_SP` is
that FIFO's own line reset there and places the picture itself: measured on
320x256@50, the picture starts at IF 159 against a nominal 242 -- negative, on a
positive-going pulse -- and the picture is complete. Applying a lag to it would
move a picture nothing is wrong with.

Measured across the change, default framing, `/sc?B` in both states:

| mode | `IF_HB_SP2` before | after | `ch` before | after |
|---|---|---|---|---|
| 800x600@60, positive | 231 | **271** | 984 | 913 |
| 640x480@60, inverted | 203 | **144** | 992 | 1051 |
| 320x256@50, doubled | 132 | 132 | 1043 | 1043 |

The inverted mode gets 59 units of its own picture back, and lands within about
six source pixels of where AKF50 puts active -- the DMT-against-AKF50 border
difference `docs/investigations/vesa-modes-are-clipped-by-default.md` records and
leaves to the user. The doubled mode does not move at all.

### What is left: the tail is past the write limit

On 800x600@60 the picture occupies IF 302..1152 and nothing is captured past
1125, so its last ~29 units cannot be reached by any window. The default is
right-clamped to 271..1123 rather than placed at 302, which is why the left band
shrinks by about half rather than closing: 64 photo px of it went, against 124
that were there.

Closing it needs the line to end a lag before the limit, which means capping the
divider at `WriteLimitUnits - CaptureLagUnits` where the doubler is bypassed --
1124 to 1052 on this mode, 6.4% of the horizontal sampling density, on every
undoubled source. That is a picture-quality trade rather than a correctness one.

`SP_RT_HS_ST` is the cheaper thing to try first and has never been swept: it is
the retiming start, reads 0, and if it moves the line's origin the lag can be
cancelled outright. `SP_RT_HS_SP` was swept over 950..1110 with no positional
effect, which says nothing about the start.

**Raising the divider instead does not work as stated, and the reason is worth
knowing.** The lag is a fixed count of ADC samples, so sampling the line twice
as finely halves it measured in source pixels -- and the ADC has the headroom,
allowing about 4013 on this mode against the 1124 it runs. Two things stop it:
the input formatter's line and window registers are 11 bits, so the line cannot
exceed 2047 units whatever the divider, and the tail green returns once the
capture window grows past about 1280 units. `docs/investigations/tail-green.md`
has the sweep and what it refutes.

**An unrelated fault is open on this mode**: the picture carries a
high-frequency horizontal wobble at the default solve. It is not the capture
lag -- it predates that work -- and it has not been diagnosed.

**Refuted by these measurements**, each of which fitted a subset:

- *No single correction fits both modes.* One does; it needs the polarity bit.
- *The offset is half the sync width.* 800x600@56 predicts 248.1 IF under it and
  measures 277.9, at a slope of 2.17 photo px per IF unit — 65 photo pixels out.
- *The offset is one sync width.* 640x480@75 predicts 141.9 and measures 211.8.
- *640x480@60 puts the picture 54 units early while 800x600@60 puts it 70 late,
  so it is not a fixed pipeline latency.* Both readings are of the real effect;
  the sign is the sync term.

**The everyday 15 kHz source cannot show it.** 320x256@50 is `sync_pol` 0 and
line doubled, so one IF unit is half an ADC sample and the lag is ~36 IF units,
against borders of 44 source pixels a side. The error stays inside the border.

### The instrument

`PATTERN CARD` rather than `PATTERN PM5544`. The plain card's outermost
concentric band is bright, `H/32` source pixels wide, and starts at active pixel
0 against a screen border `PROCpatinit` forces black — one feature, the same
feature on every mode. PM5544's outermost feature is a castellation whose colour
and cell width both depend on the mode, so a brightness threshold locates a
different source pixel on each one, which is what produced offsets that appeared
not to share a model. The card's outer ring flashes yellow against white, which
costs about 2 photo pixels of edge position and does not reach the geometry.

The measurement is the knee, driven by the engine's own pan control so the
capture origin is read from `/geometry` rather than poked:

- Zoom in until the pan has range past the expected knee, park the origin at its
  earliest, then pan later in fixed steps.
- Below the picture's true start the band's edge tracks the origin 1:1. At it,
  the band pins to the display window's left edge and the readings break into a
  sawtooth of the card's own band pitch. The knee is where the fitted line meets
  the floor.
- Slope and floor both come from the state being measured, so **no photo column
  to output pixel mapping is needed** and the encoder re-locking between source
  modes does not matter. Measured photo pixels per output pixel ran 0.83 to 1.11
  across these four states, which is why an absolute mapping cannot be carried
  between them.

Repeatability is ±0.2 photo px, measured over seven shots at a clamped pan.
`tools/gbsc-pro-hwtest/photo_profile.py` is the column profile and edge finder.

**640x480@60 is not independently pinned.** The model puts its picture start at
IF 133.6 and `firstCapture()` is 131, so the engine cannot pan early enough to
put the origin before the picture, and there is no sloped region to fit. What is
measurable there is consistent: the outer bands leave the head of the window
between origin IF 227 and 243, which places the picture start between IF 121 and
138.

## Fault 3: the emitted active window is wider than what reaches the panel

Differencing `VDS_DIS_HB_ST` against the panel gives `photo = 0.913 x output -
106`, and the painted region ends at **output ~1378** — measured twice, at a
1668 raster and at a 1679 one, so it is roughly constant in absolute output
pixels rather than as a fraction of the raster. The engine fits 1541 px of
picture into the 1668 raster, so about a quarter of it is never transmitted.

**The output hsync position chooses which part survives.** Sweeping
`VDS_HS_ST`/`VDS_HS_SP` on 800x600@60:

| `VDS_HS_ST` | 0 | 100 | 200 | 300 | 450 |
|---|---|---|---|---|---|
| content left | 125 | 35 | 20 | 1 | 1 |
| content right | 1155 | 1151 | **1055** | 1091 | 863 |

At 200 the card's right colour-bar column appears — the content is not lost in
capture, it is outside the transmitted window, and moving the sync slides the
window over the picture. It is not a usable lever as it stands: at 200 the pulse
sits inside the display window, which is the left-edge corruption
`docs/scaler-geometry-model.md` records.

**The television's overscan is not the explanation**, and neither is the earlier
comparison that appeared to rule overscan out: it compared two sources at
*different* output rasters, so it could not have settled anything. What the
bypass reference does establish is that an 800x600 source passed through
untouched fills the panel completely, so the panel paints what it is sent.

**THE VDS LINE LENGTH IS THE LEVER, AND IT IS OURS.** What reaches the panel is
a fixed FRACTION of the emitted line rather than a fixed count of emitted
pixels, so `VDS_HSYNC_RST` sets the picture's size on the panel one for one.
Measured at 480p by moving it with everything else held, fitted on ten
colour-bar edges:

| `VDS_HSYNC_RST` | predicted scale, as a fraction of the line | measured |
|---|---|---|
| 2057 -> 2097 (+1.94%) | 0.9809 | **0.9803** |
| 2057 -> 2017 (-1.94%) | 1.0198 | **1.0167** |

**`VDS_DIS_HB_ST` is not the lever.** Walked from 1967 to 1667, a 17% narrowing
of the display window, the content neither moved nor changed size and the
blanked strip's edge tracked the register linearly at 0.930 photo px per output
pixel. It clips, and nothing more.

**THE MODEL IS RIGHT FOR FOUR OUTPUTS AND WRONG FOR THE TWO SD ONES.** Swept
across every mode, against the panel's painted area taken from an 800x600
pass-through at the same camera position (photo 51.7..1564.3). The painted
span is found by differencing: clip `VDS_DIS_HB_ST` to two values inside the
painted area, take the blanked strip's edge in each, and the line through them
gives where the panel's edge falls in output pixels.

| mode | our total | standard's | ratio | painted fraction | `activePx/totalPx` | measured / standard |
|---|---|---|---|---|---|---|
| 1080p | 1920 | 2200 | 0.87 | 0.8726 | 0.8727 | **0.9999** |
| 1024p | 2026 | 1688 | 1.20 | 0.7574 | 0.7583 | **0.9988** |
| 960p | 2160 | 1800 | 1.20 | 0.7105 | 0.7111 | **0.9991** |
| 720p | 2160 | 1650 | 1.31 | 0.7753 | 0.7758 | **0.9994** |
| 480p | 2057 | 858 | 2.40 | 0.7997 | 0.8392 | **0.9529** |
| 576p | 2073 | 864 | 2.40 | 0.7807 | 0.8333 | **0.9368** |

So the standard's fraction is exactly what the chain takes wherever our raster
is within about 1.3x of the standard's, and 5 to 6 percent less at the 2.4x the
two SD modes run. The two SD errors are NOT the same size -- 4.7% against 6.3%
-- so no single constant expresses it and no explanation of the boundary has
been found. What is established is that the fraction is a property of the mode,
measurable per mode from the panel, and that four of the six need nothing.

Applied by hand, the corrected fraction brings both SD modes to the size 1080p
paints: `VDS_HSCALE` 725 -> 766 with the window at 1953 fits 576p at 0.992 of
the 1080p frame against 1.061 before, and 697 -> 739 with the window at 1869
fits 480p at 0.990 against 1.050. `HSCALE` and both windows follow from the
fraction, so nothing about the raster or the display clock moves.

**TWO TRAPS IN MEASURING IT.** Clipping the display window far enough in
disturbs the PICTURE rather than just blanking it -- an interference pattern of
the kind a capture offset against the fetch produces -- so a differencing pair
is only valid where the frames still correlate left of the strip. At 1080p and
576p a 300 px clip fails that and a 150 px one passes. And on a mode whose
window overruns the panel, a small clip is entirely off-panel and changes
nothing visible, so the clip has to land inside the painted area to say
anything at all.

**THE RATIO IS REFUTED AND THE FRACTION IS STABLE.** Measured against the panel
on all six modes, and on both SD modes at a second raster: the transmitted
window starts exactly at `activeStart` everywhere, the deficit is entirely at
the right, and it does not diminish when the raster ratio is taken to 1.50x and
1.80x. What the chain takes is a fixed fraction of the line, held to 0.3% across
a 33% change of raster -- it is simply not the standard's fraction on the two SD
modes. `the-transmitted-window-is-a-per-mode-fraction.md`.

Nothing on the board can measure what the encoder transmits -- the MS9288A is on
no MCU's I²C bus and EDID is unreachable -- but the fraction does not have to be
explained to be solved for: it is measurable from the panel per output mode, and
the raster is ours to choose.

## Two measurement traps this cost

**A photo-column to output-pixel mapping does not survive an encoder re-lock**,
and a bypass round trip or an output raster change is enough to cause one. Every
absolute comparison here was re-derived by differencing `VDS_DIS_HB_ST` in the
state being measured.

**The knee is the measurement, not the absolute position.** Sweeping a blanking
edge until the content stops moving needs no mapping at all, which is why it
survives an encoder re-lock and a bare photograph does not. It only works with
the whole picture inside the panel: clipped at both ends, the content edges are
pinned and the knee is invisible — which is what made the first 640x480 sweep
unreadable.
