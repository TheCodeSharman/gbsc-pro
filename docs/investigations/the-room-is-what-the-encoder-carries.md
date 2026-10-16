# The room is what the encoder carries, so a picture past it is not emitted

`VDS_DIS_?B_ST` is the **last shown** unit, not the first blanked one, and the
emitted units stop at the active region's last. Two things follow: flooring the
aperture onto the partially written unit is right and there is no later unit to
reach, and a produced picture longer than the room loses its far end outright.

## What was measured

`vga` at 640x480@60 into 1080p, automation frozen, the card drawn with the
animation off, read off the USB HDMI capture. The raster is 1916x1125 with
active lines 41..1121; the capture is 480 lines at `VDS_VSCALE` 455, which
produces 1080.26 output rows and ends at raster row **1120.76**.

The memory and display far ends were stepped **together** — `VDS_VB_ST` and
`VDS_DIS_VB_ST` to the same value — so the fetch always covered what the
aperture showed. An aperture opened past the memory window shows a unit the
playback stage never fetched, which is stale whatever the write did, and a probe
that moves only the aperture cannot tell that from a unit the write missed.

| `VDS_?B_ST` | last lit emitted row |
|---|---|
| 1119 | 1078 |
| 1120 (solved) | 1079 |
| 1121 | 1079 — no change |

So emitted row *k* is raster row *k* + 41, the 1080 emitted rows span raster
41..1120 inclusive, and a step of the far end lights exactly one more row until
the active region is exhausted.

## The partially written unit is shown, and it carries picture

At the solved far end the last shown row is 76% written. Per-row green
dominance over the picture's width, bottom of the frame:

| emitted row | 1076 | 1077 | 1078 | 1079 |
|---|---|---|---|---|
| green dominance | +51.0 | +137.7 | +144.3 | **+60.5** |
| mean luma | 190.6 | 155.7 | 107.8 | 41.9 |

Row 1079 is the card's border at reduced amplitude rather than stale memory or
black, which is what the capture margin pays for: the window opens
`Axis::captureMargin` units beyond the picture so the partly filled unit has a
sample under it.

**A step past it changes nothing**, to a quarter of a grey level — the strongest
difference between the 1120 and 1121 states is +0.28 luma, against +41.9 for the
step that lit row 1079.

## So the aperture is not where the loss is

Rounding the aperture inward costs at most the fraction of the last unit, and
that unit is shown. The horizontal width-parity step-back costs nothing either
at a solved overrun: it takes the far end from the clamp at `activeStop`, which
is already one past the last emitted column, to the last emitted column itself.

What costs picture is the **fit**. `fitToRaster()` rounded the scale to nearest,
so `produced` could exceed the room by up to half a step, and those units fall
outside the active region:

| mode / axis | produced | room | over | source pixel | surviving |
|---|---|---|---|---|---|
| 320x256@50 vertical | 1081.01 | 1080 | +1.01 | 4.22 px | 76% |
| 320x256@50 horizontal | 1673.43 | 1672 | +1.43 | 5.23 px | 73% |
| 640x512@50 vertical | 1081.01 | 1080 | +1.01 | 2.11 px | **52%** |
| 800x600@60 vertical | 1079.79 | 1080 | -0.21 | 1.80 px | 100% |

The framing acceptance wants three quarters of the source's outermost pixel, so
the overshoot fails wherever the magnification is low enough that a whole output
unit is a large share of a source pixel.

**The premise it rested on is refuted.** The scale was left rounding to nearest
on the reasoning that the unit an overshoot loses is in the front porch. It is
not: the aperture cannot reach past the last emitted unit, so the overshoot is
discarded with the card's border in it.

## What the fit does instead

The scale rounds **up**, so `produced` lands at or below what it was fitted to
and within one step of it. One step is `produced x magnification / 1024` of
picture — about 2.2 output rows on the bench 1080p vertical — and the whole of
the source's outermost pixel survives at both ends in exchange.

`docs/scaler-geometry-model.md` is the arithmetic the fit is built on, and
`docs/investigations/full-screen-framing-on-the-vesa-modes.md` is the acceptance
this is judged against.
