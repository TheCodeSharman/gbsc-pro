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
Vetoed, the source acquired first time.

## The channel does carry it, and the entry is what is unstable

Measured with the raster fixed: a Wii at 480p passes through sharp and full
screen, `PLLAD_MD` 1124 against `STATUS_SYNC_PROC_HTOTAL` 1124, PLL locked. So
the route is not the problem and the veto is not the answer.

What is left is the ENTRY. One input switch produced two `bypass-switch` events
1.5 s apart, under two different standards, each followed by
`source UNLOCKED: 524 lines, 2039 samples against divider 1124` -- the channel
raster sized from the divider the arm computed, against a held divider that is
still the scaled path's.

**The colour path is wrong DURING that churn and right afterwards.** Caught
mid-entry the picture renders greys as saturated green with a magenta band, the
signature of chroma carried at the wrong offset, while `HD_MATRIX_BYPS` 0,
`HD_DYN_BYPS` 0 and `DEC_MATRIX_BYPS` 1 all read correct for a component source.
A frame taken of the same menu once the entry settled is grey. So it is a
transient of the entry rather than a steady misconfiguration, and a single
photograph of it is evidence about WHEN it was taken.

**`HD_BLK_GY_DATA` is a latent second green.** `updateClampPosition()` writes
`5 / 0 / 0` to the channel's programmed blank for a YPbPr source on this
channel, where neutral chroma is 128 rather than 0 -- so the blanked region
decodes green rather than black. `enable()`'s own `0 / 0 / 0` has the same
problem. It reaches only the blanking, so it is not what the menu showed.

## Comparing two routes needs a source event between them

`/uc?x` moves the preference, and the engine re-decides only on a solve -- so
three frames taken across two toggles were all pass-through and the comparison
between them said nothing. Bounce the input after the toggle, and read
`DAC_RGBS_BYPS2DAC` in the same window as each frame rather than assuming the
toggle took.
