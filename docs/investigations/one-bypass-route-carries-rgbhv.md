# The HD bypass channel carries an RGBHV source, so two routes are one too many

The firmware has two bypass switches reaching two DAC routes, and the standard
byte decides which. `docs/video-source-acquisition.md`, *Bypass is ONE output mode*, wants one
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

## The firmware does it now

`applyForStandard()` has an arm for a source with no standard, and it is the
derivation `applySd()` already carried, shared between them.

**The divider is handed in rather than read back**, and that is the whole
difference between working and not. `setOutModeHdBypass()` writes a literal 2345
into `PLLAD_MD` on its way to the ladder, so a derivation that reads the register
solves for that literal: measured, an 800x600 source the engine measured at 1124
got `HD_HSYNC_RST` 1180 and `HD_HB_ST` 2216, and the sink reported no signal
exactly as it did with no arm at all. Given the engine's divider the arm writes
and latches it, so the ADC samples at the density the raster is drawn for --
which on a bypass route is the picture rather than an internal detail.

Measured through the firmware, RiscPC on `vga` at 800x600@60 via `/sc?K`:

    PLLAD_MD                  1124
    HD_HSYNC_RST               570
    HD_HB_ST                  1062
    HD_HB_SP                   144
    DAC_RGBS_BYPS2DAC            1
    STATUS_SYNC_PROC_HTOTAL   1124   the divider latched
    STATUS_MISC_PLLAD_LOCK       1

full-screen and sharp, the PM5544 gratings resolved finer than the scaled path
renders them. A source with nothing measured keeps the resting timing rather
than deriving a raster from a zero.

### The residual left strip, and what it is not

The HD route still opens with a dark band the ADC-to-DAC route does not have.
Measured off the photographs rather than by eye -- a 200-column strip at
mid-height, averaged to one row, both shot minutes apart at the same camera
position:

| | left edge |
|---|---|
| ADC-to-DAC | content from column ~10 |
| HD bypass | flat dark band columns 6..38, content from column ~40 |

about 30 columns, 2.4% of the panel width. **Absolute brightness across the two
is not comparable** -- the camera meters on its crop and the band itself moves
the exposure -- but the column at which content starts is geometric and does
survive.

**Every raster register in the block has been ruled out against it**, each by a
single change and a fresh profile:

| register | tried | effect on the band |
|---|---|---|
| `HD_HB_SP` | 0, 144, 240, 380 | none |
| `HD_HB_ST` | derived, and 3976 to leave the generator inert | none |
| `HD_HSYNC_RST` | 562, 570, 1810, 1864 | none |
| `HD_INI_ST` | 34, 256, 1776 | none at 34 or 1776; no signal at 256 |
| `HD_HS_SP` | 124, 170 | none |

**The video processor is not in the path either, and that is measured rather
than inferred from the mux bits.** `VDS_DIS_HB_SP` was moved from 108 to 400 --
nearly three hundred units -- with the profile unchanged, so the scaler's
display window does not reach the DAC on this route. The VDS keeps a raster from
the last scaled load (`VDS_HSYNC_RST` 1667 here) and none of it is emitted.

`HD_SEL_BLK_IN` is ruled out with them: taking the blank from the input rather
than from the one sync generates leaves the profile identical.

So the band is not programmed blanking, not the played-out line's length, not
where the output sync pulse sits, not the scaler's display window, and not which
blank the channel selects. What differs between the routes and is
not yet accounted for is that the video passes THROUGH the channel on this one
and around it on the other, while the sync is generated by the block either way.

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
- **The vertical windows and the sync pulses** are left at the values the
  switch installed. Only the horizontal derivation is programmed.
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

**THE ADC IS NOT WHAT STOPS IT.** It digitises three analog channels and has no
opinion about what they carry; YPbPr arrives on the same R, G and B lines and is
sampled exactly as RGB is. The constraint is at the far end -- the DAC drives
analog RGB into the MS9288A, which cannot be told to expect anything else, being
on no I2C bus with its firmware in mask ROM and `MCUSEL` strapped low.

### Measured: YUV into an RGB input, photographed

The failure does not need a component source to reproduce, because the chip
works in YUV internally and an RGB source is already converted both ways.
Measured on the bench RiscPC at 320x256@50 on `vga`, scaling: `DEC_MATRIX_BYPS`
0 converts RGB to YUV going in, `VDS_CONVT_BYPS` 0 converts back on the way out.

Setting `VDS_CONVT_BYPS` to 1 leaves the output stage emitting the internal YUV
onto the DAC's RGB pins, which is the same thing ADC-to-DAC would do with a
component source. **The picture goes overwhelmingly green** -- luma on the green
channel, the two colour differences sitting near mid-scale on red and blue --
while the geometry, the sharpness and the frequency gratings stay perfect. The
signal is intact and only the colour mapping is wrong. Writing 0 back restores
it exactly.

So the route's limit is the missing converter and nothing else, and it is
reproducible from either bench source rather than only from a component one.

**YPbPr pass-through therefore requires `DAC_RGBS_BYPS2DAC`**, and is not a
configuration of the ADC-to-DAC route that has not been found yet.

That leaves the route as a real choice, but a one-line one taken from the input
selection rather than from a classification -- the same `rto->inputIsYpBpR` that
`HdBypass::applyColourPath()` already keys on.

## Testing YPbPr pass-through on this bench

The mechanism is settled by the `VDS_CONVT_BYPS` measurement above, which needs
no component source. What is still unphotographed is a real YPbPr signal through
`ADC2DAC`.

The Wii at 576i cannot be bypassed here: 15 kHz, which the bench display refuses,
and `bypassCanBeDisplayed()` correctly declines it.

**A Wii set to 480p can.** Settings -> Screen -> TV Resolution -> EDTV/HDTV over
the same component cable gives 31.5 kHz progressive, which the display shows.
That reaches standard 3, whose `applyProgressive()` arm already carries real
480p values, and `/sc?K` routes it through `setOutModeHdBypass()` to the HD
channel with `applyColourPath(true)` writing `HD_MATRIX_BYPS` 0. So the
YPbPr bypass path becomes exercisable end to end, which no source on this
bench has previously allowed.
