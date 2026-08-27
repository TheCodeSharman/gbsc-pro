# An output change is not a source change

Picking a different output resolution and the source changing mode are two
different events. The engine has an entry point for each: `modeChanged()`, which
freezes capture and measures the new source, and `outputChanged()`, which
re-solves from state already held. What follows is what separates them on this
board, and what does not.

## The whole-load path was not slow, and did not disturb the source

The reason to split the two is usually stated as cost: an output change went
through `applyPresets()`, which sweeps `getVideoMode()`, cycles
`SFTRST_SYNC/DEC/MODE/IF_RSTZ` through `resetDigital()`, restarts the ADC PLL's
VCO and re-runs `ResetSDRAM()`. **On a settled source none of that is visible
from outside.**

Measured by polling `STATUS_SYNC_PROC_VTOTAL` as fast as HTTP allows across a
resolution change, on the build before the split and on the build after it,
bench RiscPC at 320x256@50:

| build | change | raster lands | reads | longest gap | count off 311 |
|---|---|---|---|---|---|
| before | 1080p → 720p | 1.7 s | 549 | 1.31 s | 0 |
| before | 1080p → 480p | 1.8 s | 695 | 1.36 s | 0 |
| after | 1080p → 720p | 1.9 s | 524 | 1.33 s | 0 |
| after | 1080p → 480p | 1.5 s | 515 | 1.30 s | 0 |

Every read arrives — a register read defers to `loop()`, so a refused read is
how a stalled loop shows — and the sync processor never loses the count. **So a
hardware test cannot tell the two paths apart on a settled source**, and one
that claims to is measuring its own fixtures. What the split buys is that the
resets and the re-detection are no longer on a path that has no use for them,
which is a reliability argument and not a measurable one.

## The line doubler is a property of the output

`SourceMeasurement::lineDoublingFor()` asks whether the doubled frame fits the
raster, so the answer moves when the raster does. 311 source lines doubled is
624 half-lines: that fits a 1125-line frame and does not fit a 525-line one.

Two things follow, and both are why an output change cannot simply re-solve the
raster and stop.

- **`solveScanMode()` belongs on the output path.** Skipping it leaves the
  doubler on into a frame with no room for it, the vertical solve fails, and the
  whole change falls back to a load.
- **The divider follows the doubling**, because the capture write limit doubles
  with it. Measured across 1080p → 480p: `PLLAD_MD` 2250 → 1124,
  `IF_LD_RAM_BYPS` 0 → 1, `IF_HS_DEC_FACTOR` 1 → 0, `IF_PRGRSV_CNTRL` 0 → 1, and
  the vertical capturable region 622 → 310.

  It is re-derived from the line rate already held, never re-measured, and only
  where the doubling actually moved: writing `PLLAD_MD` re-latches the ADC PLL.

## A framing survives as a proportion, and only as a proportion

Because the vertical capturable region halves, so do the framing's vertical
units — while the proportion, and the picture, are unchanged. `/geometry`
reports both: `ov`/`ev` in input units and `pov`/`pev` in ten-thousandths.

Measured across 1080p → 480p at one tuned framing: `ev` 558 → 278. **A test
comparing units across an output change reports the denominator moving and calls
a framing lost that the picture still shows.** Compare `poh`/`peh`/`pov`/`pev`.

## What the two paths leave differently: 20 fields

Same firmware, same source, 1080p reached from 480p twice — once by `/uc?s`, the
re-solve, and once by `/sc?)`, a whole load — and the 1536-register states
diffed.

**Most of it is `Tv5725::SourceStandard::apply()`, which the re-solve does not
run:** `IF_SEL_WEN`, `IF_HS_SEL_LPF`, `IF_INI_ST`, `IF_HB_SP`, `VDS_V_DELAY`,
`VDS_Y_DELAY`, `SP_SDCS_VSST_REG_L`, `SP_SDCS_VSSP_REG_L`, `IF_HBIN_ST`,
`IF_HBIN_SP`, `IF_HB_ST`. It is keyed on the source standard and the colour
space, neither of which an output change moves, so not running it is the intent
rather than an omission.

**The oversampling group differs, and the re-solve's values are the baseline.**
`PLLAD_CKOS` 0 against 1, `ADC_CLK_ICLK2X` 1 against 0, `DEC1_BYPS` 0 against 1.
`Adc::applySampleRate()` writes all five of that group together on both paths,
so each state is internally consistent and neither is the mismatch that greens
the screen — see CLAUDE.md, "a solid green screen has a SECOND cause". They
differ because the load recomputes the oversample from `PLLAD_KS` as the
previous resolution left it, while the re-solve keeps the one the source's last
detection chose.

**The picture is clean on both, photographed at the same framing, and that
settles it**: the oversample describes how densely the SOURCE is sampled, so the
value derived from the source's own measurement is the right one and the load's
is an artefact of which raster happened to be up before. The re-solve state is
archived whole as `snapshots/output-change-resolve-1080p-2026-08-27`. Diff a
re-solve against that, not against a load, or the group reads as three fields
changed by a commit that did not touch them.

The remainder are run-variable between any two solves: `VDS_HB_ST` and
`VDS_DIS_HB_ST` by two units on the field-rate wobble, `IF_HB_SP2` by one,
`PA_SP_S` and `MADPT_Y_DELAY`.
