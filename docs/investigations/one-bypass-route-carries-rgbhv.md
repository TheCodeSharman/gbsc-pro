# The HD bypass channel carries an RGBHV source, so two routes are one too many

The firmware has two bypass switches reaching two DAC routes, and the standard
byte decides which. `docs/retiring-mode-detect.md` §3c wants one output mode
over one route. What stood in the way was an untested assumption: that the HD
bypass channel is for HD broadcast standards and cannot carry an arbitrary PC
raster.

**It can.** Measured on the bench, RISC PC on `vga` at 800x600@60 -- RGBHV,
separate sync, `STATUS_SYNC_PROC_VTOTAL` 679, the engine measuring 40662 Hz and
solving a divider of 1124 on the scaling path.

## The two routes

`Tv5725::Chip` makes them mutually exclusive, each switch clearing the others.

| | `DAC_RGBS_BYPS2DAC` | `DAC_RGBS_ADC2DAC` |
|---|---|---|
| RD-5725-1.1 | "enable HD bypass channel to DAC directly" | "ADC (with decimation) to DAC" |
| reached by | `setOutModeHdBypass()`, standards 5, 6, 7, 13 | `bypassModeSwitch_RGBHV()`, standard 15 |
| in the video path | the HD bypass channel | nothing between ADC and DAC |
| colour conversion available | `HD_MATRIX_BYPS`, `HD_DYN_BYPS` | the decimator's `DEC_MATRIX_BYPS` only |

**Neither route is a colour space**, which the `DAC_RGBS_` prefix on both
invites. Both switches call `HdBypass::applyColourPath(rto->inputIsYpBpR)`, so
the YPbPr decision follows the input selection on either. What differs is
that on `ADC2DAC` those `HD_*` writes are not in the video path at all, and the
one converter that is -- the decimator's -- is bypassed unconditionally. So as
configured, `ADC2DAC` can carry RGB and nothing else.

## Why the HD route looked unable to carry it

Entering it on this source gives NO SIGNAL, and the sink says so rather than
naming a stale mode.

`applyForStandard()` dispatches on the standard byte with arms for `<= 2`,
`3 || 4` and `<= 7 || 13`. An RGBHV source holds 14 or 15, which matches no arm,
so **nothing source-specific is programmed.** The block keeps `enable()`'s
resting timing and the switch's hardcoded divider:

    PLLAD_MD      2345    the switch's literal
    HD_HSYNC_RST  1023    enable()'s resting value
    HD_HB_ST      3976
    HD_HB_SP       208

That is a raster for no source, so the sink refuses it. The route was never
tested; the missing arm was.

## What makes it work

`applySd()` already derives two of those from the divider, where the other arms
freeze them as literals. Applying the same derivation by hand, against the
divider the engine measured for this source, and latching:

    PLLAD_MD      1124    the measured divider
    HD_HSYNC_RST   570    PLLAD_MD / 2 + 8
    HD_HB_ST      1062    PLLAD_MD x 0.945
    HD_HB_SP       144

gives a full-screen, sharp picture -- the PM5544 card with its frequency
gratings resolved. `STATUS_SYNC_PROC_HTOTAL` reads 1124 against `PLLAD_MD` 1124,
which is the one witness that the divider latched, and
`STATUS_MISC_PLLAD_LOCK` is 1.

**So the HD bypass channel is not HD-only.** What made it look that way is a
raster frozen per standard instead of derived from the measurement.

## What follows

The reason for a second route was that an arbitrary raster could not be known,
so nothing could program the regenerator and the short path was the safe one.
The engine measures every source's line count, field rate and divider now, so
that reason is gone -- and the HD channel is strictly the more capable of the
two, being the only one with a matrix and a dynamic range converter in circuit.

One bypass output mode over one route is therefore reachable, and
`applyForStandard()`'s arms collapse into the derivation `applySd()` already
carries rather than being extended with a fourteenth and fifteenth case.

## What this does not show

- **The colour cast in the photograph is not this.** Greys read brown and the
  surround cyan, with `HD_MATRIX_BYPS`, `HD_DYN_BYPS` and `DEC_MATRIX_BYPS` all
  1 as an RGB source wants, ADC gains balanced at 123/123/123 and offsets at
  64/64/64, and all three DAC channels enabled. It is the border colour fault
  seen on the scaling path too, which is parked.
- **The derivation was applied by hand**, not by firmware, and only at one
  raster. `HD_HB_SP`, the vertical windows and the sync pulses were left at the
  values the switch installed.
- **Standards 5, 6 and 7 stay unexercised.** No source on this bench produces
  them, so what their arms freeze is not checkable here either way.

## Which route survives is decided by the colour path, and it is not ADC2DAC

This page says YPbPr rather than component throughout, because RGB is carried
on separate channels too and is component video by the same definition. The
distinction that matters here is luma plus colour difference against red, green
and blue, and only the first needs converting.

The chip works in YUV internally, so the conversion a source needs depends on
which side of the pipeline it is on. Measured on both bench sources, scaling:

| | RISC PC, RGB | Wii, YPbPr |
|---|---|---|
| `IF_MATRIX_BYPS` | 1 | 1 |
| `DEC_MATRIX_BYPS` | **0** | **1** |
| `VDS_CONVT_BYPS` | 0 | 0 |

`ColourSpace::applyRgb()` writes `DEC_MATRIX_BYPS` 0 and `applyYuv()` writes 1,
so **the decimator's converter is the RGB-to-YUV input stage**: in circuit for an
RGB source, bypassed for a YPbPr one. It converts in that direction only.

`VDS_CONVT_BYPS` 0 on both is the YUV-to-RGB stage on the way out, which the DAC
needs because the board feeds the encoder analog RGB.

So the converters this part offers are one RGB-to-YUV in the decimator, one
YUV-to-RGB in the VDS, and one YUV-to-RGB in the HD bypass channel
(`HD_MATRIX_BYPS`, with `HD_DYN_BYPS` beside it).

**On the ADC-to-DAC route the VDS is not in the path and the HD channel is not
in the path, so the only converter left runs the wrong way.** An RGB source
needs no conversion at all there -- RGB in, RGB out -- which is why that route
has always worked and why the fills-the-panel reference has every matrix
bypassed. A YPbPr source needs one YUV-to-RGB and there is nowhere on that
route to do it.

**YPbPr pass-through therefore requires `DAC_RGBS_BYPS2DAC`**, and is not a
configuration of the ADC-to-DAC route that has not been found yet.

That leaves the route as a real choice, but a one-line one taken from the input
selection rather than from a classification -- the same `rto->inputIsYpBpR` that
`HdBypass::applyColourPath()` already keys on.

## Testing YPbPr pass-through on this bench

The Wii at 576i cannot be bypassed here: 15 kHz, which the bench display refuses,
and `bypassCanBeDisplayed()` correctly declines it.

**A Wii set to 480p can.** Settings -> Screen -> TV Resolution -> EDTV/HDTV over
the same component cable gives 31.5 kHz progressive, which the display shows.
That reaches standard 3, whose `applyProgressive()` arm already carries real
480p values, and `/sc?K` routes it through `setOutModeHdBypass()` to the HD
channel with `applyColourPath(true)` writing `HD_MATRIX_BYPS` 0. So the
YPbPr bypass path becomes exercisable end to end, which no source on this
bench has previously allowed.
