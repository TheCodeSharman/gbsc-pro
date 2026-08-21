# The tail green

A green band appears at the right of the picture when the capture window is
panned far enough right. It pans **with** the picture, so it is captured content
at a fixed position in the source line, not an artefact of the output raster.

Measured on a RiscPC with a stock AKF50 mode file, 320x256@50, at
`PLLAD_MD` 2528 and `IF_HSYNC_RST` 1264 — a 1265-unit line.

## Capturing the hsync pulse produces green

Shown directly. With the capture window written by hand to `62..1062` — a start
of IF 62, inside the hsync pulse at IF 0..88.9 — a green band appears on the
**left**. Move the start clear of the pulse (`114..1114`) and the left edge is
clean.

The head never shows this in normal use because `InputLine::firstCapture()`
returns `syncUnits`, 96 in this configuration, and the solver keeps the window
clear of it. Writing `IF_HB_SP2` directly bypasses that guard.

## Where the tail green starts

Capture stop crept one bracket at a time, window width held at 1000 units:

| capture stop | source px | result |
|---|---|---|
| 1062 | 430.0 | clean |
| 1114 | 450.9 | clean |
| 1128 | 456.4 | green, ~1 source px wide |
| 1142 | 462.2 | green, thin |
| 1171 | 474.0 | green |
| 1263 | 511.2 | green, wide |

The band's width tracks `stop − X` exactly as a fixed start predicts, which puts
**X at IF 1125–1126**.

AKF50's own line for reference, `h_timings: 36, 30, 44, 320, 44, 38` over 512
source px, at 2.4707 IF units per source px:

```
sync          0 ..  36 px      IF    0.0 ..   88.9
back porch   36 ..  66         IF   88.9 ..  163.1
left border  66 .. 110         IF  163.1 ..  271.8
display     110 .. 430         IF  271.8 .. 1062.4
right border 430 .. 474        IF 1062.4 .. 1171.1
front porch 474 .. 512         IF 1171.1 .. 1265.0
```

X = 1125.5 is source px 455.6, which is 25.6 px into the 44 px right border. It
is not a boundary in the mode.

## Refuted

- **Not the source's blanking.** X sits inside the right border, not at the
  border or porch edge, and the border either side of X renders black.
- **Not the ADC's black level.** The right border renders black immediately
  before the band, so the black reference is correct there. This also disposes of
  the standing suspicion of `SP_CS_CLP_ST`/`SP` — they are measurably inside the
  sync tip at IF 13..74, which is worth fixing on its own terms, but a wrong
  black reference would spoil the whole blanking interval rather than start part
  way through the border.
- **Not sync-on-green.** `ADC_SOGEN` is 1 with `SP_SOG_MODE` 0 — the ADC's
  green-channel slicer running while the sync processor is on separate sync.
  Setting `ADC_SOGEN` = 0 does not remove the band.
- **Not the game's border colour.**
- **Not `VDS_VSYN_SIZE1`/`SIZE2`.** They are the only two registers holding 1126,
  and they are the output vertical raster — 1125 lines plus one — so they cannot
  bound a horizontal input capture.

## Open: absolute, or relative to the line end

`scaler-geometry-model.md` records X = 1126 measured at `PLLAD_MD` 2553, on a
1277-unit line. X = 1125.5 at `PLLAD_MD` 2528, on a 1265-unit line, agrees on an
**absolute** IF position rather than a fixed offset from the line end, which
would have moved by 12 units.

**That pair does not settle it.** The two dividers differ by 1%, moving the line
end by 12 units against a measurement uncertainty of ±3. A fixed offset from the
end is not excluded.

**The experiment:** drive `PLLAD_MD` well away from 2528 — far enough that the
line length moves by several times the uncertainty — and re-measure X by the same
creep. If X stays near 1126 it is a hardware limit inside the input formatter; if
it tracks the line end it is the wrapped sync after all, and `syncUnits` bounds
it.

Whatever writes `PLLAD_MD` must run before `latchPLLAD()`. A divider written
after the latch leaves the PLL on the old value with every register
self-consistent and the screen green. CLAUDE.md, "Reading `PLLAD_MD` back does
not tell you what the ADC is doing".

## A clamp is the wrong fix, and must not be reinstated

The obvious repair is to clamp `lastCapture()` at X so the window can never reach
the band. It is rejected.

X is a number measured from one source. Another mode's active picture may
legitimately extend past that IF position, and the clamp would crop it — trading
a cosmetic artefact at the edge for lost picture, silently. The head guard is
legitimate precisely because `syncUnits` is *derived*,
`ceil(units × HLOW_LEN / PLLAD_MD)`, recomputed per solve, so it tracks any
source. An absolute constant does not.

If the experiment above shows X tracks the line end, the same derived `syncUnits`
bounds the tail and the fix becomes principled. Until then the band stays, and
the real question is why blanking digitises as green rather than black.

## See also

- [`../scaler-geometry-model.md`](../scaler-geometry-model.md) — the two green
  regions in an IF line, and the arithmetic the capture window is solved from.
