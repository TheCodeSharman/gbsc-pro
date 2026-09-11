# 800x600@60 loses both edges, and every stage between source and panel measures correct

The bench RISC PC on `vga` at AKF50's 800x600@60, scaled to the solved raster,
shows the PM5544 card with its outermost castellation columns missing at left
and right. The same source in RGBHV bypass shows the card complete.

**It is three separate faults sharing one symptom.** One is closed; two are not.

| | what it is | state |
|---|---|---|
| head blanking | `IF_HBIN_SP` blanks into the capture where the doubler is bypassed | **closed** |
| the line offset | the IF origin is ~72 units late, and one sync width early where hsync is inverted | **corrected in placement**; the tail past the write limit remains |
| the transmitted window | the produced picture is wider than the encoder transmits | open |

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
one ADC sample and LAG is ~72 ADC samples. The offset column is what a model
without the sync term has to explain: it changes sign. The LAG column is what
one bit of extra state reduces it to.

**The sync term is decided by a bit the chip already reports.**
`STATUS_SYNC_PROC_HSPOL` reads 1 on AKF50's `sync_pol` 0 and 2 modes and 0 on
its `sync_pol` 3 modes, `sync_pol` bit 0 being hsync. Where the pulse is
positive-going the IF origin is its leading edge; where it is inverted the
origin is the trailing edge, so the sync interval is already behind the origin
and the picture arrives one sync width sooner.

### What the engine does with it

`VideoSourceLine` carries the lag and which end of the pulse the line is counted
from. `firstCapture()` is `lag + (syncAtHead ? syncUnits : 0)`, and `videoAt()`
maps a position a standard states as a fraction of ITS line onto this one.
`ActiveImage::place()` puts an untuned axis through `videoAt()`; placing it at
the stated fraction of the IF line directly was where the wrong premise lived,
and correcting `firstCapture()` alone never reaches it because the default start
sits above that floor. `SourceMeasurement::measureHsyncPositive()` reads
`STATUS_SYNC_PROC_HSPOL`, and `CaptureWindow::readRasters()` is handed it the way
it is already handed `HLOW_LEN`.

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

## Fault 3: the produced picture is wider than the encoder transmits

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

Nothing on the board can measure the encoder's active window — the MS9288A is on
no MCU's I²C bus and EDID is unreachable — so the open question is what sets its
width, and whether `Geometry::solveRaster()` can be made to solve a raster that
fits inside it. Sweeping the raster total against the painted end would answer
the first.

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
