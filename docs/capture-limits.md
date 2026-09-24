# What the board can capture

**THERE IS NO CAPTURE BOUND.** The green band at the end of the line, which
every version of this page was built around, is produced by the VDS's one-line
delay -- `VDS_D_RAM_BYPS`, carried as the picture-quality option
`uopt->wantVdsLineFilter`, and now defaulted off. It sits downstream of the
capture, so with the delay bypassed the whole line arrives at every divider up
to 3200.
[`investigations/the-tail-green-is-the-vds-line-filter.md`](investigations/the-tail-green-is-the-vds-line-filter.md)

The constants that bounded the divider against it -- `WriteLimitUnits`,
`CaptureWidthLimitUnits`, `framableIfLine()` and `maxCaptureWidth()` -- are gone
with it. What remains is the sampling arithmetic, which bounds what a line can
carry however the divider is chosen, and the three ceilings that are real.

**There is no vertical bound either.** This page carried one -- a source of more
than 535 lines -- and it was a firmware gate, not a property of the part. An
RGBHV source of 627 lines scales cleanly and full screen with a measured divider
and a healthy `HPERIOD_IF`. `docs/rgbhv-bypass-trap.md` has what was measured.

## The three ceilings that are real

| | what it is | where |
|---|---|---|
| the crossover row | the ADC clock a given oversampling survives | `Adc::maxCkoFor()` |
| the line counter | eleven bits, so 2047 IF units -- measured on the picture, since the register is wider | `InputFormatter::LineCounterMax` |
| the divider field | `PLLAD_MD` is twelve bits | `Adc::DividerMax` |

`SamplingClock::recommendedDivider()` takes the most samples the ceilings allow,
and oversamples only where that costs none of them. The kept count is the only
thing that carries the source's pixels; oversampling acts on the CONVERSION rate
and buys freedom from aliasing rather than resolution.
`docs/sampling-table.md` has what each mode lands on.

**Which ceiling binds depends on the scan mode.** An IF unit is two ADC samples
on a doubled line and one on an undoubled one, so the line counter reaches
4094 samples doubled and 2047 undoubled -- which is why 800x600@60 undoubled
lands on 2006 from the counter while 320x256@50 doubled lands on 2508 from the
row.

**The rating is read at the oversampling INSTALLED, not the one asked for.**
`Adc::oversampleFor()` reduces a request the crossover row refuses, and the row
is chosen from the divider's own clock -- so budgeting for the request is
circular. `Adc::maxDivider()` answers at the ratio the row will actually
install; asking at the 162 MHz row, which installs none, returns the register
maximum and nothing else stops it. Measured: the engine solved 4012 that way and
the screen was a solid green block.

| clock (`PLLAD_MD` x line rate) | `PLLAD_KS` | oversampling available |
|---|---|---|
| >= 80 MHz | 0 | none |
| 40..80 MHz | 1 | 2x |
| 20..40 MHz | 2 | 4x |
| < 20 MHz | 3 | 8x |

So sampling density is bought with conversion quality, one for the other. The
twelve deleted preset tables all sat at 2553..2559, hard against the 4x row.

**RGBHV bypass runs at 1856 whatever the source**: it writes nothing to memory
and the IF is out of the path, so none of this reaches it.

**`MemoryWindow::captureFits()` bounds the capture against SDRAM**, which is a
separate and unrelated limit.

## The divider follows the measured line rate

Where the removed cap flattened it, the divider is now a function of the rate:
about 4 counts per 25 Hz on a 15.6 kHz doubled line. A rate wobble therefore
rewrites `PLLAD_MD` and re-latches the ADC PLL. **What that costs on a live
picture is not measured.**

## What one window can carry

The horizontal axis has no native resolution -- the chip sees sync edges, not
pixels -- so this is the only statement that can be made about a source's
detail surviving:

```
IF units per source pixel = samples per line / htotal
```

**htotal 1024 is the ceiling for one sample per source pixel at 1024 units, and
512 for two.** A source wider than about 512 active pixels cannot be sampled
above Nyquist at a 1024-unit window, which is why fine vertical detail on a
VESA-class source aliases, and why passing such a source through unscaled is the
only way to carry it intact.

Of the stock AKF50's 28 modes the widest lines are the ones to watch: 1056x250
and 1056x256 at htotal 1536, and 1280x480 at htotal 1600, 1664 and 1680. The
ordinary modes are all at htotal 1024 or below -- 640x250, 640x256 and 768x288
at 1024, 640x200 at 1020, 640x480 and 640x512 at 800..896 -- so the density they
get is whatever the divider choice above buys them. This page used to list the
wide five as uncapturable; they were not, and the cap that made them look so is
the one that has gone.

## The divider is a trade, and it has two floors

Lowering `PLLAD_MD` makes each sample cover more of the line. It costs sampling
density and zoom travel, because the zoom ceiling is a magnification rather than
a width: the narrowest slice that still fills the raster is
`rasterTotal / maxMagnification`, and fewer IF units per source pixel means that
slice covers more source.

At the bench source -- 512 px per line, 320 active, into a 1916 px raster at 4x:

| `PLLAD_MD` | IF per px | narrowest slice | zoom range |
|---|---|---|---|
| 2548 | 2.49 | 192 source px | 1.67x |
| 2400 | 2.35 | 204 px | 1.57x |
| 2250 | 2.20 | 218 px | 1.47x |
| 2048 | 2.00 | 239 px | 1.34x |

Two floors bound the trade:

- **Nyquist.** Below 2 IF units per source pixel the source aliases rather than
  softens. For a 512-px line that is `PLLAD_MD` 2048.
- **The magnification ceiling is a choice, not hardware.** RD-5725-1.1 states no
  minimum for `VDS_HSCALE`; the field is 10 bits and the formula has no floor.
  So zoom travel lost to a lower divider is recoverable by raising
  `maxMagnification`, paid for in interpolation quality, and where that starts
  to look bad only the picture can say.

## What the choice has not been judged on

**No photograph has scored any of it.** The host oracle gives 320x256@50 doubled
2230 -> 2508 and 800x600@60 undoubled 1250 -> 2006, and a density-against-
oversampling judgement has to be made on the picture at each row rather than in
the arithmetic. `docs/known-issues.md` carries it as open.

## See also

- [`investigations/the-tail-green-is-the-vds-line-filter.md`](investigations/the-tail-green-is-the-vds-line-filter.md)
  — what the band actually is, and the thirteen candidates it is not
- [`investigations/the-tail-band-is-not-a-capture-width.md`](investigations/the-tail-band-is-not-a-capture-width.md)
  — why no constant in either unit fitted it
- [`investigations/tail-green.md`](investigations/tail-green.md) — how the
  supposed bound was measured
- [`rgbhv-bypass-trap.md`](rgbhv-bypass-trap.md) — the line-count gate that was
  one, and what replaced it
- [`scaler-geometry-model.md`](scaler-geometry-model.md) — the arithmetic from
  capture window to output registers
