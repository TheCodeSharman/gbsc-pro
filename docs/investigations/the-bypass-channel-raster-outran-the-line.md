# The bypass channel's raster outran its own line

A Wii at 480p on `ypbpr` came out pink with a black bar down the left, and then
as no signal at all. Two defects in `Tv5725::HdBypass`, reaching the same end by
different routes, and one open question about whether the route should have been
chosen at all.

## The rule

`applyHorizontalFromChannelLine()` states it: `HD_HB_ST` has to sit below
`HD_HSYNC_RST` or the generator never opens. Above the line total that edge
never fires, so the only blanking left in the line is `HD_HB_SP`'s -- which
paints from the start of every line to wherever `HD_HB_SP` lands, and reads as a
black bar down the left of the picture.

## An unrecognised source was taken for SD

`applyForStandard()` dispatched `standard <= 2` into `applySd()`, and **0 means
nothing recognised** rather than a standard. Read off the unit in that state:

| register | read | written by |
|---|---|---|
| `HD_HSYNC_RST` | 570 | `applySd()`: `PLLAD_MD / 2 + 8`, `PLLAD_MD` being 1124 |
| `HD_HB_ST` | 1062 | `applySd()`: `PLLAD_MD x 0.945` |
| `HD_HS_ST` / `HD_HS_SP` | 128 / 0 | `applySd()`'s `0x80` / `0x00` |
| `OUT_SYNC_SEL` | 2 | `applySd()` |
| `HD_VB_ST` / `HD_VS_ST` / `HD_VS_SP` | 0 / 2 / 7 | **`enable()`'s resting values** |

Four writes match the SD arm and the untouched fifth is what names the standard:
`applySd()` writes the vertical registers only under `standard == 1` or `== 2`,
so neither ran. Blanking then started 492 units past the end of the line.

The rest of that arm is as wrong for the source as the raster is. It inverts
`SP_HS2PLL_INV_REG`, `SP_CS_P_SWAP` and `SP_HS_PROC_INV_REG`, flips Mode
Detect's `MD_HS_FLIP`/`MD_VS_FLIP` and narrows `ADC_FLTR` to the 40 MHz corner,
which on a 480p component source is why `STATUS_MISC_PLLAD_LOCK` stayed 0 and
the sync processor counted 97 where 524 was due.

## Progressive SD never wrote the line total at all

`applyProgressive()` writes `HD_HB_ST` `0x864` and no `HD_HSYNC_RST`, so the
channel keeps `enable()`'s resting 1023 and blanking starts at 2148. The
constant was sized for the 2345 `setOutModeHdBypass()` used to write into
`PLLAD_MD` on its way in; that literal is gone, so the frozen raster is orphaned
from the divider it assumed.

Which arm a run took was not stable. Across four identical switches the
classification read 0, then 3, then nothing -- the same source, the same cable.

## Both arms now size the channel from the divider

Standard 0 takes the measured arm with every other source no table names.
Progressive SD calls `applyPassThroughSampling()` for its horizontal block and
its sampling, and keeps only the vertical window that is genuinely its own. That
hands `ADC_FLTR`, `PLLAD_KS` and `PLLAD_CKOS` back to `Adc::applySampleRate()`,
which is the one owner of that group.

`test_hd_bypass.cpp` asserts the rule across every arm that sets a raster.

**Standard 13 is outside it, and for a different reason: it writes neither
register.** The channel raster it runs on is whatever entered bypass before it,
which is the inherit-from-the-chip fault under another name. No bench source
reaches it.

**`applySd()` keeps its own inconsistency.** `PLLAD_MD / 2 + 8` says the channel
line is the divider halved, and `PLLAD_MD x 0.945` applies the active fraction to
the unhalved divider, so the two disagree for any divider at all. A 15 kHz SD
source cannot be bypassed to a display that refuses the rate, so no reading here
could confirm a change.

## The route was chosen for a source that does not need it

`SourceMeasurement::bypassSuitsCount()` passes through any source at or above
400 lines whose line rate reaches 26 kHz. A Wii at 480p is 524 lines at
31.4 kHz, so it qualifies -- and with pass-through vetoed the same source scales
to a clean full-screen picture at the numbers `../bench-sources.md` records as
this input's known good: `PLLAD_MD` 1096 against `STATUS_SYNC_PROC_HTOTAL` 1096,
`HPERIOD_IF` 214, `STATUS_SYNC_PROC_VTOTAL` 524, PLL locked, acquired in 16 s.

**Every failure to lock on this input happened after a bypass switch or in its
wake**, and entering bypass rewrites the ADC PLL group and the sync processor.
Vetoed, the source acquired first time. Whether the channel carries this source
once its raster is right is not yet measured.
