# What the board can capture

Two independent bounds decide whether a source arrives intact. Both are hard,
and a source that violates the vertical one loses picture silently — nothing
enforces it.

| bound | limit | what happens past it |
|---|---|---|
| horizontal | capture must end before **IF 1125** | the capture path stops writing video and writes `Y=U=V=0`, which decodes to a green band. Active picture there is destroyed |
| vertical | source of more than **535 lines** | RGBHV bypass, entered once and never left. The VDS is out of the path, so nothing is scaled |

The vertical bound is `docs/rgbhv-bypass-trap.md`. The horizontal one is measured
in `docs/investigations/tail-green.md`, which also records the explanations that
turned out to be wrong.

## The horizontal bound is a position, not a width

X = 1125 IF units = 2250 ADC samples, counted from the line start. It does not
move with the capture start, with the source's border or porch timings, or with
the memory clock across a 2:1 sweep. Whatever counts, counts samples.

Because it is a position, the usable fraction of any line is `2250 / PLLAD_MD`,
and the whole line fits only when `PLLAD_MD <= 2250`. That caps the line at 1125
IF units, which then have to cover every pixel the source puts on it:

```
IF units per source pixel = 1125 / htotal
```

So **htotal 1125 is the ceiling** for one sample per source pixel, and 562 for
two.

## Stock AKF50, both bounds applied

28 modes. Four are bypassed on line count. Five of the remaining 24 are scaled but
cannot be captured whole:

| mode | htotal | VTOTAL | IF units per px | usable |
|---|---|---|---|---|
| 1056x250 | 1536 | 312 | 0.73 | 73% |
| 1056x256 | 1536 | 312 | 0.73 | 73% |
| 1280x480 | 1600 | 525 | 0.70 | 70% |
| 1280x480 | 1664 | 520 | 0.68 | 67% |
| 1280x480 | 1680 | 500 | 0.67 | 66% |

**1280x480 is the highest horizontal resolution that is scaled at all** — 1600x600
is bypassed on VTOTAL 625. And there is no setting that delivers it intact: at one
sample per pixel the line runs past IF 1125 and the right third is destroyed, and
at a divider low enough to fit, every pixel gets two thirds of a sample and the
whole line aliases.

**Every mode the bound costs is an outlier**, and declining to support them is a
defensible position rather than a gap. 1056-wide and 1280x480 are unusual
geometries; so is 896x352, the closest survivor at htotal 1100.

**The ordinary modes all clear it with room.** The tightest is htotal 1024 —
640x250, 640x256, 768x288, with 640x200 at 1020 — which leaves **101 IF units of
margin, about 9%**. 640x480 and 640x512 sit further back still at htotal 800..896.

So the bound is real and worth stating, but it is not currently costing a mode
anyone wants. What makes it worth writing down is that the margin on the
commonest modes is 9% rather than a factor.

**800x600 at htotal 1024 would capture perfectly well** — 1.10 IF units per pixel.
It is excluded only by the vertical bypass threshold, which is the bound more
likely to matter in practice.

## The divider is a trade, and it has two floors

Lowering `PLLAD_MD` makes each sample cover more of the line, so 2250 of them
reach further. It costs sampling density and zoom travel, because the zoom ceiling
is a magnification rather than a width: the narrowest slice that still fills the
raster is `rasterTotal / maxMagnification`, and fewer IF units per source pixel
means that slice covers more source.

At the bench source — 512 px per line, 320 active, into a 1916 px raster at 4x:

| `PLLAD_MD` | IF per px | lost at line end | narrowest slice | zoom range |
|---|---|---|---|---|
| 2548 | 2.49 | 150 IF units | 192 source px | 1.67x |
| 2400 | 2.35 | 76 | 204 px | 1.57x |
| 2250 | 2.20 | none | 218 px | 1.47x |
| 2048 | 2.00 | none | 239 px | 1.34x |

Two floors bound the trade:

- **Nyquist.** Below 2 IF units per source pixel the source aliases rather than
  softens. For a 512-px line that is `PLLAD_MD` 2048.
- **The magnification ceiling is a choice, not hardware.** RD-5725-1.1 states no
  minimum for `VDS_HSCALE`; the field is 10 bits and the formula has no floor. So
  zoom travel lost to a lower divider is recoverable by raising
  `maxMagnification`, paid for in interpolation quality, and where that starts to
  look bad only the picture can say.

## What enforces the horizontal bound

Two things, and they compose.

`SourceMeasurement::recommendedDivider()` caps `PLLAD_MD` at 2250, so the line the ADC
delivers is at most 1125 IF units long and the whole of it is inside the limit.
This is a **second** ceiling beside the ADC's 162 MSPS rating, and whichever is
tighter binds. At four-times oversampling the rating is the tighter above about
17.6 kHz, so the write limit binds only at slow line rates — this bench's
15.55 kHz among them.

`InputLine::lastCapture()` clamps the far end of the capture window at
`WriteLimitUnits`. With the divider capped this never fires — it is there for the
lines the divider did not choose, since `SourceMeasurement::adopt()` takes whatever a
custom preset or a bypass switch left in the register.

**The clamp is a measured constant and the cap is what keeps it off real
picture.** X is this board's number; a source whose active picture legitimately
runs further would be cropped by the clamp silently, where the head guard is
derived per solve from the hsync duty. Deriving the far end the same way needs to
know what counts to 2250, and nothing does.

`MemoryMap::captureFits()` bounds the capture against SDRAM, which is a third and
unrelated limit.

## See also

- [`investigations/tail-green.md`](investigations/tail-green.md) — how X was
  measured and what it is not
- [`rgbhv-bypass-trap.md`](rgbhv-bypass-trap.md) — the vertical bound
- [`scaler-geometry-model.md`](scaler-geometry-model.md) — the arithmetic from
  capture window to output registers
