# 2026-08-08 — the geometry engine on the bench

Photographs from the session that flashed `src/tv5725/driver.h` / `geometry_regs.h` to the
unit for the first time. Source is the RISC PC at **320x256@50, VTOTAL 311**
(the 800x600 desktop mode is over 535 lines and trapped in RGBHV bypass, where
the VDS is out of the video path and there is no geometry to solve).

Written up in `~/riscpc-gbsc-firmware-session-handover-2026-08-08-night.md`.
Register states are from a `geometry.py` or `/getregs` read taken adjacent to
each photo.

**The images themselves live in
[gbsc-pro-bench-photos](https://github.com/TheCodeSharman/gbsc-pro-bench-photos),
not here** — this repo is a public fork and GitHub refuses LFS uploads to a
fork. That repo mirrors these paths, so copying its tree into a checkout lands
the files beside this table. See
[../2026-08-05-horizontal-geometry/README.md](../2026-08-05-horizontal-geometry/README.md)
for the commands and the reasoning.

**Two different faults, and the second post-dates the first.** They are not the
same bug and should not be merged.

| # | photo | state | what it shows |
|---|---|---|---|
| 01 | `01-2140-picture-recovered-after-power-cycle` | preset 0x15, `SCALING_RGBHV` 1, `SP_VTOTAL` 311, raster 1445 x 1126 | Healthy RISC OS desktop, pillarboxed, after a **true power cycle** (mains *and* USB — USB backfeeds the rails). The MS9288A had wedged; the serial console proved the output clock was fine while the screen was dead. Baseline for what follows. |
| 02 | `02-2210-green-bar-display-window-too-wide` | capture `104..1124` (1020), `VDS_HSCALE` 832 (x1.2308), memory `9..1443`, display `95..1348` | **Fault 1.** A green comb band ~67–100 px wide down the **right** of the picture. The display window is open past the last written pixel, so unwritten frame buffer scans out. `applyBestHTotal` moves `VDS_HSYNC_RST` and slides the blanking windows by `diffHTotal/2`, but `produced` depends on capture and scale — the picture does not follow. |
| 03 | `03-2240-phase-comb-and-wrong-colours` | `VDS_HS_ST` 8, `VDS_HS_SP` 56 (**both even**), `PLLAD_MD` **1509**, `IF_HSYNC_RST` 1276 | **Fault 2, later and different.** Zoomed crop of the icon bar. Every vertical stroke is broken line-to-line by ~1 px, **and the colours are wrong** — the disc icon shows magenta/blue/red fringing that is not in the source. Zoomed in deliberately to make both visible. |

## Fault 2 — what is ruled out and what is not

**Ruled out:** the odd-sync trap. `VDS_HS_ST` 8 and `VDS_HS_SP` 56 are both even,
so the documented "`VDS_HS_ST`/`SP` must be even" hazard is not firing.

**Leading candidate.** A register diff against `snapshots/before-geometry-flash-2026-08-08.json`
shows the ADC sample clock has moved and the IF's line length has not:

```
PLLAD_MD       2048 -> 1509
IF_HSYNC_RST   1276, unchanged      invariant wants IF_HSYNC_RST ~ PLLAD_MD / 2 = 754
```

The ADC is sampling a different number of samples per line than the IF's own
line-length register claims. That combs every vertical edge by construction, and
**colour fringing is what distinguishes it from simple aliasing** — a wrong
sample clock lands R, G and B at different points on each transition, which
resampling arithmetic alone would not do.

**Nothing in the geometry engine writes `PLLAD_MD`.** It is the legacy preset
machinery, which changes it seemingly at random. Same root cause
as fault 1: the old preset code writing registers the engine has computed.

**A second reading, not yet excluded.** At x1.178 a 1-px source column must
become either 1 or 2 output pixels, and RISC OS icons are full of 1-px strokes,
so *some* combing is inherent to a non-integer ratio. It does not explain the
colours. The discriminator was set up and not answered: the unit was left frozen
at **exactly x2.000** (capture `264..914`, `VDS_HSCALE` 512, display `113..1411`),
where integer scaling cannot alias. If the comb survives at x2 the ratio is
innocent.

## The measurement neither fault has had

Creep `VDS_DIS_HB_ST` down until the green just vanishes; `edge - origin` is the
real `produced`, measured rather than derived. That is `measure_produced.py`,
repaired in `0f0324b` this session and still never run.
