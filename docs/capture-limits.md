# What the board can capture

**One bound decides whether a source arrives intact**, and it is horizontal: the
capture path writes about **1034 IF units from wherever the window starts** and
then writes `Y=U=V=0`, which decodes to a green band, and active picture there is
destroyed. Nothing in hardware enforces it, so the loss is silent.

It is a WIDTH, not a position in the line, and the difference decides the fix:
the window may sit anywhere, but no window may be wider than that -- so the
divider has to keep the line short enough that one window still spans it.

It is measured in `docs/investigations/tail-green.md`, which also records the
explanations that turned out to be wrong.

**There is no vertical bound.** This page carried one — a source of more than 535
lines — and it was a firmware gate, not a property of the part. An RGBHV source
of 627 lines scales cleanly and full screen with a measured divider and a healthy
`HPERIOD_IF`; what put it in bypass was a line-count test the sync watcher no
longer makes. `docs/rgbhv-bypass-trap.md` has what was measured.

## The horizontal bound is a position, not a width

**Superseded.** X is a capture width counted from the start of the window, not a
position — see `investigations/tail-green.md`. This section is kept because the
usable-fraction arithmetic below still bounds what a line can carry.

X = 1125 IF units = 2250 ADC samples, counted from the line start. It does not
move with the capture start, with the source's border or porch timings, or with
the memory clock across a 2:1 sweep. Whatever counts, counts samples.

Read as a position, the usable fraction of any line is `2250 / PLLAD_MD`. The
width reading gives the same ceiling on what a line can carry, because a window
of about 1024 units is all there is however it is placed:

```
IF units per source pixel = 1024 / htotal
```

So **htotal 1024 is the ceiling** for one sample per source pixel, and 512 for
two. A source wider than about 512 active pixels cannot be sampled above Nyquist
at all, whatever the divider does -- which is why fine vertical detail on a
VESA-class source aliases, and why passing such a source through unscaled is the
only way to carry it intact.

## Stock AKF50, the bound applied

28 modes. Five cannot be captured whole:

| mode | htotal | VTOTAL | IF units per px | usable |
|---|---|---|---|---|
| 1056x250 | 1536 | 312 | 0.73 | 73% |
| 1056x256 | 1536 | 312 | 0.73 | 73% |
| 1280x480 | 1600 | 525 | 0.70 | 70% |
| 1280x480 | 1664 | 520 | 0.68 | 67% |
| 1280x480 | 1680 | 500 | 0.67 | 66% |

**There is no setting that delivers 1280x480 intact**: at one sample per pixel the
line runs past IF 1125 and the right third is destroyed, and at a divider low
enough to fit, every pixel gets two thirds of a sample and the whole line
aliases.

**Every mode the bound costs is an outlier**, and declining to support them is a
defensible position rather than a gap. 1056-wide and 1280x480 are unusual
geometries; so is 896x352, the closest survivor at htotal 1100.

**The ordinary modes all clear it with room.** The tightest is htotal 1024 —
640x250, 640x256, 768x288, with 640x200 at 1020 — which leaves **101 IF units of
margin, about 9%**. 640x480 and 640x512 sit further back still at htotal 800..896.

So the bound is real and worth stating, but it is not currently costing a mode
anyone wants. What makes it worth writing down is that the margin on the
commonest modes is 9% rather than a factor.

**800x600 at htotal 1024 captures perfectly well** — 1.10 IF units per pixel, and
measured on the bench at VTOTAL 627 it scales sharp and full screen. It used to
be excluded by the line-count gate, which is the entry this page carried as the
vertical bound.

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

**X is not a position.** The bound is a capture WIDTH of about 1034 units
counted from the start of the window, the same on both scan modes; it read as a
position only because every measurement of it held the window's start fixed.
`VideoSourceLine::maxCaptureWidth()` bounds the window by it.
`investigations/tail-green.md` has the measurements.

`SourceMeasurement::recommendedDivider()` then holds the divider where the whole
line stays *reachable*: `VideoSourceLine::framableIfLine()` inverts
`capturable()` from the measured sync duty, the pulse polarity and the capture
lag, so a window opened to `maxCaptureWidth()` still spans the line end to end.
Without it the picture stops growing before the line does when the user zooms
out, and the far end is reachable only by giving up the near one.

The bound is per source, because every term of it is:

| | duty | pulse | lag | IF line | `PLLAD_MD` |
|---|---|---|---|---|---|
| 800x600@60 | 0.122 | at head | 72 | 1250 | 1250 |
| 320x256@50, doubled | 0.071 | at head | 0 | 1103 | 2206 |
| an inverted pulse, doubled | — | behind the origin | 0 | 1025 | 2050 |

An inverted pulse is the tightest: the sync interval is already behind the
origin, so nothing is guarded off the head and the whole line counts against the
limit. A doubled line carries no lag, because `IF_HBIN_SP` is the FIFO's own
reset and places the picture itself.

The 11-bit `IF_HSYNC_RST`, `IF_HB_ST2` and `IF_HB_SP2` cap the line at 2047
units above all of it — `SourceMeasurement::IfLineUnitsMax`. A line past that
wraps rather than failing.

This is a ceiling beside the ADC's 162 MSPS rating, and whichever is tighter
binds. **The rating is never the tighter one for a source this board sees**: it
allows 4095 dividers below 39.6 kHz and about 2532 at 1280x1024@60's 63,960 Hz,
where the capture width has already stopped the line well short. RGBHV bypass
runs at 1856 whatever the source: it writes nothing to memory, so no capture
bound reaches it.

**The rating is read at the oversampling INSTALLED, not the one asked for.**
`Adc::oversampleFor()` reduces a request the crossover row refuses, and the row
is chosen from the divider's own clock — so budgeting for the request is
circular. Reserving the whole rating for four times puts that clock at 40.5 MHz,
one step over `postDividerFor()`'s 40 MHz row, where two is what installs and
the part converts at 81 MSPS: the rating reserved and half of it spent.

| clock (`PLLAD_MD` x line rate) | `PLLAD_KS` | oversampling available |
|---|---|---|
| >= 80 MHz | 0 | none |
| 40..80 MHz | 1 | 2x |
| 20..40 MHz | 2 | 4x |
| < 20 MHz | 3 | 8x |

`modeOversample_` is 4 everywhere, so what is installed falls out of the divider
rather than being chosen. Measured on 800x600@60: a 42.9 MHz clock at
`PLLAD_MD` 1124 and a 51.5 MHz one at 1250 are the same row, so raising the
divider there costs no oversampling.

`VideoSourceLine::lastCapture()` clamps the far end of the capture window at
`WriteLimitUnits`. With the divider capped this never fires — it is there for the
lines the divider did not choose, since `SourceMeasurement::adopt()` takes
whatever a bypass switch left in the register.

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
- [`rgbhv-bypass-trap.md`](rgbhv-bypass-trap.md) — the line-count gate that was one, and what replaced it
- [`scaler-geometry-model.md`](scaler-geometry-model.md) — the arithmetic from
  capture window to output registers
