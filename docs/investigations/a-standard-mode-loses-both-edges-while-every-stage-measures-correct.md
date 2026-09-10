# 800x600@60 loses both edges, and every stage between source and panel measures correct

The bench RISC PC on `vga` at AKF50's 800x600@60, scaled to 1920x1080, shows the
PM5544 card with its outermost castellation columns missing at left and right.
The same source in RGBHV bypass shows the card complete. **The cause is not
known.** What follows is what has been measured, and the five explanations that
are refuted.

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

In IF units, with the line at 1124: sync 0..136, back porch 136..187, left
border 187..230, active 230..1083, right border 1083..1124.

## Every stage measures correct

| stage | measured | expected |
|---|---|---|
| line rate | 37,879 Hz | 40 MHz / 1056 exactly |
| sync duty | 12.19% | 128/1056 = 12.12% |
| capture, maximum | IF 138..1122 | contains back porch, both borders and all 800 active |
| `produced` against the display window | 1462 against 1462 | fits to +1 |
| display blanking | 101 | the write start, `VDS_HB_SP` + 55 + 25 x magnification = 8 + 55 + 38 |

With the capture zoomed to its maximum the top and bottom castellations return
and the left and right ones do not, so **the capture cannot be what removes
them** — there is nothing left to capture.

## What is refuted

**The television does not overscan.** The 320x256 source at the same output
resolution, on the same set, shows every castellation and both yellow corner
markers. A set cropping ten percent a side could not.

**The output display blanking is not clipping it.** Dropping `VDS_DIS_HB_SP`
from the write start to the memory window's 8 revealed no further content, only
the colour corruption that an open window past the write start always produces.

**The encoder is not holding a stale lock.** Toggling `PAD_SYNC_OUT_ENZ` 0-1-0
to force re-acquisition changed nothing.

**`produced` is not larger than the window.** It fits to one pixel on both axes,
so the solver is not overdriving the raster.

**The picture is not partly off-panel.** Blanking a known 362 px of the 1462 px
picture — 24.8% — produced a black strip measuring about 26% of the visible
picture area. The photo-column to output-pixel mapping is therefore close to
1:1 and essentially the whole produced picture reaches the panel.

## What was fixed on the way, and is not this

The framing was intermittently solved against the wrong window because
`SourceKey` quantised the field rate into one-hertz buckets and this source's
rate crosses a boundary. `docs/investigations/two-spellings-of-scaling-rgbhv.md`
is a different fault; this one is in the commit that gave `SourceKey` a
tolerance. Fixing it moves the capture from the envelope's 971 IF units to DMT's
853 and holds it there, which improves the picture materially and does not
complete it.

## It is not the bypass route's residual, which is closed

The HD bypass channel had a left band of its own, about 2.4% of the panel. That
one is **closed** -- `HD_HS_ST`, the sync pulse's leading edge, corrected by the
channel's own 40 counts, with `HD_HB_ST` closing a second band at the right.
`one-bypass-route-carries-rgbhv.md` has the measurement.

**They share a symptom and nothing else.** This fault is on the SCALING path,
where there is no HD channel in the picture at all and the sync the sink
triggers on is the VDS's. Reaching for `HD_HS_ST` here reaches a register that
is not in the path.

## A second source shows it, and narrows what they share

The Wii on `ypbpr` at 480p loses the left the same way, measured against
pass-through at the same camera position: the horizontal rules of the Wii's
Screen menu reach both panel edges in pass-through and start a fifth of the way
in when scaled, running off the right.

So it is not a property of 800x600, of the RISC PC, of `vga`, or of a
monitor-definition file. What the two sources share is that both run **above
31 kHz and are NOT line doubled**, where the bench's everyday 15 kHz source is
doubled and frames correctly. `SourceMeasurement::lineDoubled()` is the term
that separates them, and no capture-window default has ever been measured
against a source where it is false.

That is a candidate rather than a conclusion -- two sources is not a
characterisation, and the doubling changes the IF's units, the divider and the
line counter together, so which of them carries the error is not established.

## Where to look next

The remaining assumption nobody has tested is that
`produced = capture x 1024 / scale` describes what the memory-to-playback path
actually delivers at this magnification. `docs/scaler-geometry-model.md` records
that four models of that relationship were proposed in one evening and three
refuted, and that a good fit is not evidence the quantities are what they seem.

The measurement that would settle it is a left-edge creep: raise
`VDS_DIS_HB_SP` one step at a time and record the output pixel at which the
card's leftmost visible feature disappears. That gives the card's left edge in
output coordinates, to compare against where the arithmetic puts it — IF unit
187 for the border, 230 for the active picture.
