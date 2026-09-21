# 2026-09-21 — the golden 800x600 pass-through reference

What a correct RGBHV pass-through looks like on this bench: the RISC PC at
**800x600@60 on `vga`**, `preferScalingRgbhv` off, divider 2038, source running
`PATTERN PM5544` with `BORDER ON`.

**The images themselves live in
[gbsc-pro-bench-photos](https://github.com/TheCodeSharman/gbsc-pro-bench-photos),
not here** — this repo is a public fork and GitHub refuses LFS uploads to a
fork. That repo mirrors these paths, so copying its tree into a checkout lands
the files beside this table.

The registers are `tools/gbsc-pro-hwtest/snapshots/golden-800x600-bypass-2026-09-21.json`
(all 1536, for `snapdiff.py`) and `…-2026-09-21.dump.json` (608 config, for
`dump_registers.py --restore`).

| | |
|---|---|
| `HD_HS_ST` / `HD_HS_SP` | 33 / 164 |
| `HD_HB_SP` / `HD_HB_ST` | 340 / 2037 |
| `HD_VB_ST` / `HD_VB_SP` | 0 / 27 |
| `HD_HSYNC_RST` | 2046 |
| `PLLAD_MD` | 2038 |
| `STATUS_SYNC_PROC_HTOTAL` / `VTOTAL` | 2038 / 627 |
| `HPERIOD_IF` | 176 |

| # | file | what it shows |
|---|---|---|
| 01 | `01-golden-800x600-bypass.jpg` | the card filling the panel, castellation complete at both ends, the mouse pointer parked hard against the left edge of the source's picture |
| 02 | `02-golden-800x600-bypass-flash.mp4` | four seconds, 120 frames. The card's liveness animation is what makes both boundaries measurable — see below |

## What makes this state correct, and how it is checked

**The corner blocks are the acceptance test.** `PatLib`'s `PROCanimcorners`
draws four blocks of `CW% DIV 3` x `CH% DIV 3`, identical by construction, so a
picture running off either end of the panel returns that corner narrower. Here
they measure 27 and 28 photo columns — equal, so nothing is clipped at either
end. It needs no blanking change, no column-to-sample mapping, and nothing on
the scaler moves while it is measured.

**The two animations separate on orthogonal channels.** The screen border flips
cyan/magenta (`VDU 19,0,24`) and the corner blocks flip yellow/white
(`PROCcol &00FFFF00` / `&FFFFFF00`), both off one phase. `R−G` swings ±255 on
cyan/magenta and is zero on yellow/white; `B` does the reverse. So one clip
carries the border and the picture's own edge, separately. Chroma subsampling
leaves some crosstalk where the two sit side by side, so the clean reading is
the corner that has no border beside it.

**Phase-locked averaging is what makes it visible at all.** The flip is three
grey levels through this camera, which max-minus-min cannot find. Splitting the
frames by phase and averaging each half takes the noise floor to 0.05 levels and
the border reads at 57 sigma. A single photograph cannot do this, and a
threshold on one frame finds the bezel instead.

In this state the left border shows ~10 columns and the right border is off the
panel, so the panel's window is a few samples wider than the picture and the
slack sits at the near end.

**The frequency wedge carries no beat.** Per-column temporal standard deviation
across the 120 frames is 0.37 against a flat-grey camera floor of 0.15 — the
residual is sub-pixel camera shake on a high-contrast grating, and a drifting
beat would band the wedge and swing far wider.

## What the blanking window is, and what it is not

`HD_HB_SP` 340 / `HD_HB_ST` 2037 is the source's **active video** extent:

```
RetroScaler-Acorn.mdf  h_timings: 128, 48, 40, 800, 40, 0
                       sync 128 | back porch 48 | border 40 | display 800 | border 40 | front porch 0

active video  176/1056 x 2038 = 339.7      ->  HD_HB_SP 340
              1056/1056 x 2038 = 2038.0    ->  HD_HB_ST 2037, one below the line
```

`HdBypass` computes 417 / 1961 instead, which is VESA DMT's **display area**
(216/1056 and 1016/1056). Both descriptions agree on the total and the sync
width; they disagree on the 40 pixels at each end that the RISC PC emits as
border and DMT spends on porch. A border is active video, so blanking to the
display area crops it.

On this path the blanking window is not what frames the picture — the sink's
window is, and that follows `HD_HS_ST` one sample for one. Blanking only has to
keep sync and back porch off the screen, so it biases wide.
