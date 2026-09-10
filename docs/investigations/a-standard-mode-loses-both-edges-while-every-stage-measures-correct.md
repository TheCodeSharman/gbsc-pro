# 800x600@60 loses both edges, and every stage between source and panel measures correct

The bench RISC PC on `vga` at AKF50's 800x600@60, scaled to the solved raster,
shows the PM5544 card with its outermost castellation columns missing at left
and right. The same source in RGBHV bypass shows the card complete.

**It is three separate faults sharing one symptom.** One is closed; two are not.

| | what it is | state |
|---|---|---|
| head blanking | `IF_HBIN_SP` blanks into the capture where the doubler is bypassed | **closed** |
| the line offset | picture sits ~70 IF units later than the arithmetic places it, and its tail wraps | open |
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

## Fault 2: the picture sits ~70 IF units late, and its tail wraps

With the head blanking out of the way a black band remains at the left. Creeping
the capture start with the picture fitted, and reading where content begins:

| `IF_HB_SP2` | 230 | 250 | 270 | 290 | 300 | 310 | 320 | 340 |
|---|---|---|---|---|---|---|---|---|
| content start, photo px | 125 | 93 | 61 | 28 | **11** | 11 | 11 | 11 |

1.63 photo px per IF unit, which is the magnification, and the knee is at
**IF 300**. The card's first pixel is due at IF 230. The 40 px left border ends
there, so the capture already starts inside the picture and 70 units of black
follow it.

**Capturing the whole line shows where they went.** At IF 2..1123 magnified
1.01x, a second castellation column appears at IF ~8..24, before the sync and
porch black band that runs IF ~60..306. That column is the card's *right* edge,
wrapped past the 1124-unit line end: the picture occupies IF ~300..1152 and its
tail reappears at the head. **One offset accounts for both edges** — the left
black band and the right-hand loss are the same fault seen from two ends.

This falsifies a stated premise. `VideoSourceLine.h` says the hsync pulse is at
the HEAD of the IF line, and `firstCapture()` returns `syncUnits` on that basis.
The photograph has picture content before the sync.

**Refuted as the cause**, each swept with the picture fitted inside the panel so
a shift would have been visible:

- `SP_RT_HS_SP`, 950..1110 — no positional effect at all.
- `IF_LINE_ST` with `IF_LINE_SP` following it, 0..128 — no horizontal effect.
  It changes the colour, not the framing. Its 64 is close enough to 70 to be
  tempting and it is not the cause.
- `IF_HBIN_SP` — fault 1, and the offset survives fixing it.

**The offset is not a constant, which is what makes it hard.** 800x600@60 puts
the picture ~70 IF units later than the arithmetic; 640x480@60 puts it about 54
units *earlier*. So it is not a fixed pipeline latency, and no single correction
fits both. Both readings come from the same knee method, and on 640x480 the
knee may be the border rather than the picture — the border is a different
colour there and black on 800x600 — so re-measuring both against a source
feature that is unambiguous at both ends is the first move, before any model.

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
