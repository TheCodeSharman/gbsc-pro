# Bench log

What was on screen, what the registers were, and what it proved. Kept so a
finding does not have to be re-established next session.

**Photographs are the evidence. They are just not flat.** What is *in* a photograph
is real and is often the only record of it: the green bars, the two mouse pointers,
the uneven grille bars, the overshoot halo. Never argue a photographed artefact
away as a camera effect without a reason that predicts its specific shape.

What a hand-held photograph cannot carry is **metric** claims. Tilt and perspective
differ shot to shot, so a position read off one is worth ±100 output pixels at
best. Do not derive register values from a photograph, and do not quote a "match
within 1%" — that has already happened in this log and the claim was withdrawn.
Presence, colour, count, structure and "did it change": trustworthy. Position in
output pixels: not.

Design each test so its answer is **binary**: does the artefact appear or
disappear, is the text legible or not, is there one pointer or two. Those survive
a tilted camera. "Where is it" does not. When position genuinely is the question,
move a register by a large amount and ask whether the artefact tracked it, rather
than measuring where either ended up.

**Know what the source is supposed to look like before calling something a fault.**
The Acorn globe missing from the right of the icon bar is RISC OS scrolling it off
the desktop edge — it is *correct*, not scaler clipping. This has now been misread
as clipping twice, in two different sessions, and is already in the handover's
withdrawn list. `produced > display window` does **not** by itself mean lost
picture: the capture takes in border either side of the active region, so the
first several dozen output pixels of overhang are border.

**One change, then a photograph, then the log row — before the next change.**
Batching changes and asking for one photograph at the end destroys attribution and
wastes the frames: several states this session were overwritten before they could
be photographed, and their rows cannot be reconstructed.

**Adding an entry.** Capture the state *before* the photograph, never after —
photographs lag register writes, and getting that order wrong has inverted cause
and effect twice in this project:

```sh
nix develop -c python3 tools/gbsc-pro-hwtest/snapdiff.py --host <ip> \
    --save tools/gbsc-pro-hwtest/snapshots/<name>.json --note "<what this is>"
nix develop -c python3 tools/gbsc-pro-hwtest/geometry.py --host <ip> \
    --label "<what changed>" --log ~/geometry-photos.log
```

Then add a row below and say what it decided. One change per row.

The photograph goes in `photos/` as before, but **that path is gitignored and
the images live in
[gbsc-pro-bench-photos](https://github.com/TheCodeSharman/gbsc-pro-bench-photos)**
— this repo is a public fork and GitHub refuses LFS uploads to a fork. So drop
the file in `photos/` here to work with it, and commit it to that repo, which
mirrors this path. The row you write below stays here: it is what the photograph
decided, and it is the half that diffs.

---

## Snapshots

Full 6-segment dumps, diffable with
`snapdiff.py --diff <before>.json <after>.json`.

| file | date | state |
|---|---|---|
| `riscpc-working-2026-08-01.json` | 08-01 18:11 | 1280x1024-class source, locked |
| `riscpc-broken-2026-08-01.json` | 08-01 18:16 | no lock after cable fiddling |
| `riscpc-422-stock-2026-08-01b.json` | 08-01 21:19 | 422-line raster, stock registers |
| `zarch-akf50-fullscreen-2026-08-02.json` | 08-02 01:06 | Zarch, hand-tuned to full screen |
| `bypass-800x600-fills-2026-08-02.json` | 08-02 15:52 | HD bypass 800x600, **fills the panel** |
| `fills-corrupt-2026-08-02.json` | 08-02 15:59 | 320x256 full screen via tweak controls, **corrupt** |
| `clean-not-filling-2026-08-02.json` | 08-02 16:01 | 320x256, clean, does **not** fill |
| `before-capture-display-region-2026-08-02.json` | 08-02 16:25 | the above, re-captured immediately before the next test |
| `capture-display-region-2026-08-02.json` | 08-02 16:26 | capture narrowed to 732 units, x3.28 into a 2400 px window |
| `glitching-2026-08-14.json` | 08-14 00:46 | the shear glitch **happening**, csync path |
| `CLEAN-riscpc-320x256-50-2026-08-15.json` | 08-15 | **certified glitch-free**, 608 config + 48 status, restorable |
| `CLEAN-full-1536-riscpc-320x256-50-2026-08-15.json` | 08-15 | the same state, all 1536, for `snapdiff.py` |

Note `zarch-akf50-fullscreen` is labelled "hand-tuned to full screen", **not**
clean. It sits at the worst overflow in the set and very likely carries the same
edge corruption. It was mistaken for a clean counter-example once already.

**The last three are the first certified pair in this archive.** Every other
snapshot here was taken for a geometry reason and none is known glitch-free, so
until 08-15 a known-good comparison had never actually been runnable. Note the
two `CLEAN` files are the same moment in the two incompatible formats —
`dump_registers.py` writes 608+48 and can `--restore`, `snapdiff.py` writes 1536
and cannot. Diff like against like.

### What the pair says

The glitch was closed as **source-generated** — it went away when the RiscPC was
cold-booted along with the unit, and has not returned. So the diff between these
two is *not* a glitch diff, and must not be read as one. What it does record is
that the two states sit on different sync paths, and that four things moved
together:

| | `glitching-2026-08-14` | `CLEAN-…-2026-08-15` |
|---|---|---|
| `SP_SOG_MODE` / `SP_EXT_SYNC_SEL` | 1 / 1 | 0 / 0 |
| coast pre/post, `SP_H_PULSE_IGNOR` | 7 / 3, 107 | 0 / 0, 255 |
| `GBS_OPTION_SCALING_RGBHV` | 0 | 1 |
| `STATUS_SYNC_PROC_VSACT` | 0 in 2375/2375 | 1 in 150/150 |
| `SP_VTOTAL` | 308, 7.34% off-mode | 311, 0.00% off-mode |

`ADC_INPUT_SEL` is 1 and `PLLAD_MD` is 2553 in both, so the TV5725-side mux and
the horizontal timing are identical — the difference is upstream of the chip.
The `VSACT` row is why CLAUDE.md no longer says that bit is dead.

## Photographs

All 2026-08-02, RISC PC via GBSC Pro at `192.168.88.108`, in `photos/`.

| # | time | source / state | what it shows |
|---|---|---|---|
| 01 | ~15:20 | Nevryon title, window 1928 | Useless as a geometry test — the title screen is black at every edge, so blanking, border and content are indistinguishable. |
| 02 | ~15:25 | Nevryon in-game, window 1928 | First real edges. Margins 24.5% left / 71.7% picture / 3.8% right. |
| 03 | 15:36 | RISC OS desktop 320x256, window 1928 | Same geometry as 02 with a different source, so the margins are real, not content. |
| 04 | ~15:42 | desktop, window narrowed to **1444** | Picture cropped and *smaller*. Misread at the time as "the TV does not stretch" — see below. |
| 05 | ~15:46 | desktop, window restored 1928, pointer parked | Reference frame. Picture measured at output px **944..2403** (1459 px wide). |
| 06 | ~15:49 | desktop, window widened to **2350** | **No visible change at all.** Pointer, text size and margins identical. |
| 07 | 15:52 | desktop 800x600, **HD bypass** | Fills the panel edge to edge. The known-good reference. |
| 08–09 | 15:59 | desktop 320x256 full screen via tweak controls | Fills, but vertical comb bands ~50–70px at both edges, moving between frames, icons ghosted. |
| 10 | 16:31 | desktop 320x256, capture 732 units at **x3.28**, overflow eliminated | Fills horizontally. **The moving comb bands are gone** — image is static, no glitching, flat grey runs edge to edge. Residual: detail is blocky with colour fringing, and something overlaps at the right. |
| 11 | 16:38 | as 10 but `PLLAD_MD` 2048, capture 640 units at **x3.75** | Detail mangling *worse*. Flat grey still clean edge to edge. Six registers had moved since photo 10, so on its own it attributes nothing — see the control below. |
| 12 | 16:44 | as 11 but **x2.0** — `HSCALE` the only change | **The decisive frame.** Text reads "Discs"/"Apps" cleanly and icons are sharp — but the picture repeats partway across, **two mouse pointers**, then garbage. |
| 13 | 16:50 | `produced == memory == display == 1280 px` at x2.0, windows 660..1940 | **The model's prediction, confirmed.** "RiscPC :0 Discs Apps" all pixel-crisp, one pointer, no bands, no repeat, no garbage. Half panel width. One residual: a narrow strip of picture to the left, outside the display window, with a black gap before the picture. |
| 14 | 16:55 | both windows shifted right 300 px, width unchanged | Picture and strip **both moved right together**, gap between them unchanged. The strip is generated relative to the window position, not fixed in the output line. Memory and display moved together, so this does not say which of the two it tracks. |
| 15 | 17:00 | display window left edge pulled in 340 px, memory untouched | **Strip gone.** Picture correspondingly cropped — "cPC" instead of "RiscPC" — which is the 340 px, as asked for. So the strip is *upstream* of display blanking and is maskable by an inset. |
| 16 | 17:20 | **baseline restored** with `/uc?h`, restart of the photo loop | Pixel perfect. Crisp text, sharp icons, one pointer, no artefacts. Does not fill: 1928 px of a 2600 px line. |
| 17 | 17:35 | horizontal LPF chain bypassed **and** all TV enhancements turned off | Pixel edges hard instead of curved; the bright overshoot halo around dark strokes is gone with the TV processing. |
| 18 | 17:36 | close-up of 17, the RiscPC icon and text | Edges now square — but the icon's grille bars are **visibly uneven in width and spacing**, and "isc" is squashed. The LPF had been hiding this. |
| 19 | 17:42 | close-up after `PLLAD_MD` 2345 -> **3072** | **Bars uniform.** Even widths, even spacing. Sampling ratio confirmed as the cause of the unevenness. |
| 20 | 17:50 | same state, whole screen | **Green bars** over the right ~quarter, superimposed-looking, structured. OSD and PIP both verified *disabled*, so not an overlay. |
| 21 | 18:05 | `VDS_DIS_HB_ST` 2492 -> 1865, one field | **Green entirely gone**, picture clean. The window no longer extends past the valid picture. |
| 22 | 18:20 | capture 1200 units at **exactly x2.000**, produced 2400 == memory == display | **Green back**, though narrower than photo 20. Kills the "exact power-of-two `HSCALE`" theory outright. |
| 23 | 18:30 | `VDS_DIS_HB_ST` 2500 -> **2020**, one field | **Green gone — but the picture is clipped**: the source has content to the right of the new edge. So valid picture reaches at least 2020 *and* real content extends past it. Active video is **strictly between 960 and 1200 IF units**; the 62.5% estimate is a floor, not the value. |
| 25 | 20:05 | RISC OS test card with the **border colour set to purple**, at the baseline | Purple visible on **all four sides**: the capture takes in border well outside active video, horizontally and vertically. Vertically asymmetric — little above, a lot below — matching the wide-open vertical window. Stripe bands clean and evenly spaced. **This is the calibration signal**: shrink each edge until the purple just disappears. |
| 24 | 19:10 | capture cut to **984 units** (72..1056, the 0x420 clamp), x2.0, windows 100..2068 | **No green** — but clipped harder still, "Apps" cut in half. Also the grille bars went *uneven* again, at unchanged `PLLAD_MD` 3072. |
| 26 | 20:50 | capture trimmed to 205..1000 (795 units), `HSCALE` still 512 | Green frame intact on all four sides — **nothing clipped**, so the trim approached from the safe side. Magenta roughly halved left. A structured junk block appears right of the picture: product 1590 px against a 1928 px display window, so the tail of the window shows memory the scaler no longer writes. |
| 27 | 21:05 | `HSCALE` 407, product 2000.20 px against a 2000 px memory window | **Moving comb bands over the whole picture**, different every frame. A 0.2 px per line overflow. See the fractional-overflow entry below — `geometry.py` printed "+0" for this. |
| 28 | 21:12 | `HSCALE` back to 512, capture 240..1000 | Clean again, green frame on all four sides. Confirms 27 was the scale, not the trim. |
| 29–30 | 21:35 | horizontal tuned by eye to 277..1042 (765 units), animated card, **consecutive animation phases** | The pair is the evidence. Outer ring flips **white → yellow**, so the card is live. The magenta band, the thin cyan stripe and the junk to its right are **pixel-identical across both phases** — frozen, therefore scratch memory, not captured border. Registers in `horizontal-tuned-by-eye-2026-08-02.json`. |
| 31 | 2026-08-03 | test card at the solved geometry, `SOLVED-mode13-fullscreen-clean-2026-08-03.json` | **Edge to edge, clean.** No magenta, no scratch, no tearing, green frame present on all four sides so nothing is clipped. The one remaining defect is visible in the cyan square: it is oblong, ~17.5% too wide — see the aspect entry below. |
| 32 | 2026-08-03 | Nevryon title screen, same geometry | Full screen, crisp text, no comb bands, no green blocks, no repeat. |
| 33 | 2026-08-03 | Nevryon in game, TV set to 4:3 correction, `SOLVED-nevryon-fullscreen-2026-08-03.json` | **The goal.** Full screen, planets round, sprites and status bar pixel-sharp. |

## What each test decided

**Display window is a mask, not a size control.** Photos 04 and 06 together:
narrowing crops the picture, widening changes nothing. `VDS_DIS_HB_ST/SP` does
not rescale anything.

**The TV does fill; the encoder pads.** Photo 04 was read at the time as the TV
refusing to stretch. Wrong — the TV stretches whatever active region it gets, but
the MS9288A hands it black bars around our picture, so "filled" includes the
bars. Narrowing the window put *less* picture inside the same encoder frame, so
the picture shrank. gbs-control contains no MS9288A code at all.

**Bypass and scaling have identical VDS registers.** `bypass-800x600-fills` and
`clean-not-filling` agree exactly on `VDS_HSYNC_RST`, `VDS_VSYNC_RST`,
`VDS_DIS_HB_SP/ST` and `VDS_HSCALE`. One fills, one does not — because in bypass
the VDS block is not driving the output at all.

**Corruption is memory-window overflow, not a magnification limit.** Across all
seven snapshots the split is clean:

```
                            cap    mag   produced   VDS_HB   diff   outcome
bypass-800x600-fills       1000  2.000       2000     2000     +0   clean
clean-not-filling          1000  2.000       2000     2000     +0   clean
riscpc-422-stock           1010  1.000       1010     1069    -59   clean
riscpc-working             1020  1.000       1020     1034    -14   clean
riscpc-broken              1020  1.000       1020     1034    -14   clean
fills-corrupt              1045  2.554       2669     2401   +268   CORRUPT
zarch-akf50-fullscreen     1041  2.646       2754     2445   +309   full screen
```

`produced = capture_units x 1024 / VDS_HSCALE`. Every clean state has
`produced <= VDS_HB` width; both overflowing states are the full-screen ones. So
§3's "the upscaler degrades above a threshold that moves with the output
configuration" is better explained as overflow — the threshold moves because the
window does.

**The bind this exposes.** At the current capture width, filling needs 2669
output pixels inside a 2600-pixel line. It cannot fit, because 1045 captured
units are being magnified when only 732 of them are picture — the rest is border
and porch.

**Overflow causes the moving corruption; magnification does not.** The test above
was run (photo 10, snapshot `capture-display-region`):

```
IF_HB_SP2/ST2   120..1120  ->  252..984   (732 units, the display region per docs §2d)
VDS_HSCALE            512  ->  312        (x3.282)
VDS_HB          400..2400  ->  100..2500  (2400)
VDS_DIS_HB      564..2492  ->  100..2500  (2400)
produced = 732 x 3.282 = 2402 px, against a 2400 px window -- +2 px, i.e. rounding
```

Written with `setfield.py`, in that order, so that no intermediate step left the
scaler producing more than the window held.

The result splits the two things that had been called "corruption":

- **Gone: the moving comb bands.** Photos 08–09 had ~50–70 px vertical bands at
  both edges, changing frame to frame, at +268 px of overflow. At +2 px they are
  absent and the image is static — a still photograph captures it completely,
  which was not true of 08–09. Flat grey now runs edge to edge with no banding.
- **Remains: a static artefact.** Detail is blocky with colour fringing on
  one-pixel features, and something overlaps at the right of the icon bar.

So §3's model survives for the artefact it was built to explain, and the
"horizontal upscaler degrades above ~2.1" theory does not: **×3.28 is 56% more
magnification than any state that produced comb bands, and produced none.**
Magnification was never the variable; the window was.

This also means overflow and magnification were confounded in all seven earlier
snapshots — every high-magnification state was also an overflowing one. Only
setting them independently separated them.

## SHARPNESS: the LPF was hiding a sampling error

Three findings, photos 16-19, each on a single change:

**The horizontal path runs an 11-tap low-pass filter, and it was on.** `s1 0x02`
read `0x6a`: `IF_HS_TAP11_BYPS` 0 (data *through* the 11-tap LPF),
`IF_HS_INT_LPF_BYPS` 0 (INT/LPF path in use), `IF_HS_SEL_LPF` 1 (LPF path, not the
interpolator). Bypassing the first two turns curved pixel edges into square ones.
Noise reduction was already bypassed and was never a factor.

**The bright halo around dark strokes was the TV, not the GBSC.** Overshoot is an
edge-*sharpening* signature and nothing upstream was sharpening. It disappeared
when the TV's enhancements were switched off, not when any register changed.
Set the TV input to Game/PC mode with sharpness at 0 before judging any frame.

**`PLLAD_MD` must be a multiple of 1024 for this source.** With the LPF bypassed,
the RiscPC icon's grille bars — identical single-pixel lines in the source —
rendered at visibly different widths and spacings, because `PLLAD_MD` 2345 against
a 512-pixel source line is **4.58 ADC samples per source pixel**. At `PLLAD_MD`
3072 the bars are uniform. The condition is on IF units, not ADC samples:

```
IF units per line       = PLLAD_MD / 2
IF units per source px  = PLLAD_MD / 2 / 512  ->  integer requires PLLAD_MD % 1024 == 0

1024 -> 1 unit/px    2048 -> 2    3072 -> 3    4095 -> 4 (floor division gives 2047+1 = 2048)
```

**This is the tuning signal the project has been missing.** With the LPF bypassed,
a wrong sample clock is directly visible as uneven pixel columns — exactly what a
VGA monitor's "clock" control adjusts against. That makes §8.3's auto-adjust
button tractable: the precondition is *bypass the LPF first*, or the error being
tuned out is invisible.

`PLLAD_MD` 3072 locks at `PLLAD_KS` 2, and 4095 locks at `PLLAD_KS` 1 — both with
`HTOTAL == PLLAD_MD`. The docs' "3072 did not lock" was measured without moving
`PLLAD_KS` and should be qualified.

## THE MODEL: three independent failure modes, one shared cause

Photo 12 separated them. There is not one "corruption" — there are three, and all
three are about `produced` against the **display window**, plus magnification:

```
produced = capture_units x 1024 / VDS_HSCALE

produced >  window          moving comb bands at both edges, ~50-70 px, ghosting
produced <  window          the line REPEATS at produced, then wraps into garbage
produced == window          neither
magnification above ~x2     static detail mangling: text into barcode, colour fringing
```

### Correction: `produced` must count *active* units, not captured units

The formula above is wrong in one place, and photos 20-21 exposed it. **Blanking
captured is not picture produced.** The capture window can be wider than the
source's active video — the preset's own `120..1120` is — and the porch and
blanking inside it yield no picture. So the real quantity is:

```
valid picture = ACTIVE units within the capture x 1024 / VDS_HSCALE
```

and any part of the display window past that shows memory contents. That is the
green in photo 20: capture 1310 units of which only ~960 are active, so ~535 px at
the right of a 2000 px window had nothing valid behind it. It looked superimposed
and local because it *is* local — it is the tail of the window.

OSD and PIP were both checked and are **disabled** (`OSD_DISP_EN` 0, `OSD_MENU_EN`
0, `PIP_EN` 0, all PIP windows zero), so no overlay engine is involved. Ruling
those out is what forced the correction above.

So every artefact in this log is one failure in different clothes: **the output
window covering a region the scaler had no valid source data for.** Overflow, the
repeat, the leading strip and the green are all that.

### Dead end: `HSCALE` being an exact power-of-two ratio does not matter

Worth recording because the pattern was compelling and wrong. Every clean state in
this log sat on `HSCALE` 1024 (x1.000) or 512 (x2.000); every artefact sat on a
fractional ratio — 312 (x3.282), 273 (x3.751), 671 (x1.526). That is a perfect
split across seven states, and it suggested the interpolator misbehaves unless
1024/`HSCALE` is exact.

Tested directly (photo 22): capture 1200 units at `HSCALE` **512, exactly x2.000**,
produced 2400 == memory == display. **The green came back.** The correlation was an
artefact of sampling — every fractional-`HSCALE` state also happened to have a
window extending past the valid data.

This also retires "magnification above ~x2 mangles detail" as a *separate* rule.
x1.526 is less magnification than x2.0 and looked worse; what actually differed was
how much of the window had valid data behind it.

### Dead end: `IF_HB` has three sets, but set 1 is not the constraint

The Input Formatter has three horizontal blanking register sets, and the tools
only ever exposed set 2:

```
set 0   IF_HB_ST  s1 0x10   IF_HB_SP  s1 0x12      (start < stop: unused/disabled)
set 1   IF_HB_ST1 s1 0x14   IF_HB_SP1 s1 0x16      NOT declared in tv5725.h
set 2   IF_HB_ST2 s1 0x18   IF_HB_SP2 s1 0x1a      the one every tool writes
        IF_HBIN_ST s1 0x24  IF_HBIN_SP s1 0x26     "blank for scale down"
```

`IF_HB_ST1` is absent from `tv5725.h` and was absent from the register map; it was
read out of the address gap and has since been added to the map with
`status: inferred`.

The hypothesis was good: after `PLLAD_MD` 2345 -> 3072 stretched the IF line from
1173 to 1537 units, set 2 was rescaled and **set 1 was not**, so set 1 fell from
86.3% of the line to 65.8% and looked like a stale binding constraint. Its 1012
units x2.0 = 2024 px landed inside the measured 2020..2500 bracket, and it would
have explained why the baseline is clean (there, both sets agree).

Rescaled set 1 to 100..1426, restoring 86.3%. **No visible change at all.** Set 1
is not in the data path for this configuration. `pllad.py` still does not rescale
it, which remains untidy, but it is not the fault.

### What the datasheet actually says about the two sides

Both sides use "HB" for horizontal blanking with ST/SP pairs, which has cost this
project real time. They are different windows in different units:

| | seg | registers | units | role |
|---|---|---|---|---|
| Input Formatter | S1 | `IF_HB_ST/SP` (set 0), `_ST1/SP1` (set 1), `_ST2/SP2` (set 2) | IF units, `PLLAD_MD/2` per line | bounds what is **captured** |
| Video Display | S3 | `VDS_HB_ST/SP` | output px | "used to **get data from memory**" |
| Video Display | S3 | `VDS_DIS_HB_ST/SP` | output px | "used to **clean the output data in blanking**" |

Quotes are verbatim from RD-5725-1.1. `VDS_HB` is the **fetch**; `VDS_DIS_HB` is
only a **mask**. Pulling `VDS_DIS_HB` in hides an artefact without fixing it —
which is exactly what happened at photo 21.

**Three IF blanking sets exist**, RD-5725-1.1 pages 01-7..01-10, S1_10..S1_1B:
set 0 is unsuffixed (`IF_HB_ST`/`IF_HB_SP`), then set 1, then set 2. Hard to spot
because **`tv5725.h` omits `IF_HB_ST1` at S1_14 entirely**, leaving an `SP1` with no
matching `ST1` and an address jump 0x12 -> 0x16. Now added to
`tv5725_registers.json`; it should also be added to `tv5725.h`.

**The IF horizontal scaler can only decimate.** `IF_HS_RATE_SEG0..SEG7` (S1_03..0A)
are eight 12-bit DDA increments — 8 bits each plus a shared low nibble
`IF_HS_RATE_LOW` — giving arbitrary per-segment ratios, i.e. non-linear
"panoramic" scaling. But the datasheet says the DDA generates the **write enable**,
so it can only *skip* samples, never create them; `IF_HS_DEC_FACTOR`'s three bands
(>1/2, <1/2, <1/4) are all below unity. **No amount of register value makes it
scale up.** On this unit every increment reads 0 and `IF_SEL_WE` is 0, so it is
inert and affects nothing measured here.

**`IF_LINE_ST` explains the `+0x40`.** `IF_LINE_ST`/`IF_LINE_SP` (S1_20/S1_22) are
the progressive line window. `IF_LINE_ST` is 64 = 0x40, set by the preset and never
written by firmware, so `/sc?n`'s `IF_LINE_SP = pll_divider/2 + 1 + 0x40` makes the
window exactly one line long. `setIfHblankParameters()` instead writes
`IF_LINE_SP = IF_HSYNC_RST + 1`, which assumes `IF_LINE_ST` is 0 and yields a window
**64 units short**. Harmless only because that function is dead code (§4).

## THE FIFO LIMIT: capture width is capped at ~1056 units

This is the finding that ties the evening together and resolves the baseline
puzzle below. **The line-double FIFO holds about 1056 IF units, and that cap is a
fixed number of units — it does not scale with the sample clock.** Capture wider
than that and the surplus is read out of a buffer that was never filled, which
appears as the structured green block at the right of the picture.

Every state on record splits on it cleanly:

```
                    PLLAD_MD   IF line   capture width   result
baseline (photo 16)     2345      1173            1000   clean
photo 20                3072      1537            1310   GREEN
photo 22                3072      1537            1200   GREEN
photo 24                3072      1537             984   clean
```

Under ~1056 is clean, over it is green, with no exceptions. It also matches the
bisection: green vanished with the display window at 2020 and appeared at 2500,
bracketing the valid edge between 960 and 1200 units — and 1056 sits inside that.

The firmware knows about it. `setIfHblankParameters()` clamps the capture's right
edge to exactly `0x420` = 1056:

```c
if (GBS::IF_HB_ST2::read() >= 0x420) { GBS::IF_HB_ST2::write(0x420); }
```

That clamp lives in the branch taken when `IF_LD_RAM_BYPS` is 0, i.e. when the
line-double FIFO is in use — which is the case for this source, because the
firmware only bypasses the FIFO for `videoStandardInput` 3, 4, 8 and 9.

**Do not bypass the FIFO to lift the cap.** `VDS_VSCALE` reads 1023 (x1.001), so
the vertical doubling from 311 source lines to 626 output lines has to be the line
double. Bypassing it would very likely halve the picture height.

### The consequence: raising `PLLAD_MD` makes things WORSE

This inverts the direction the whole session was pushing in. The cap is a fixed
number of units, so a faster sample clock captures a *smaller fraction* of the
line:

```
PLLAD_MD 2345  ->  line 1173 units  ->  1056 is 90% of the line
PLLAD_MD 3072  ->  line 1537 units  ->  1056 is only 69% of the line
```

Active video is ~85% of the line. At the preset's own clock it fits with room to
spare; at 3072 it cannot be captured at all, which is why photos 23 and 24 clip
real content off the right. **The baseline clock is the correct clock for this
source**, and the preset's 1000-unit capture is a deliberate fit just under the
cap rather than a coincidence.

Chasing a higher clock for "more units" was backwards for the whole middle of the
session.

### Still unresolved after that: the invalid region is created upstream

The state that shows green has `produced == memory == display == 2400 px` exactly.
All three agree, so the green is **not** the output window outrunning the scaler —
that model is exhausted. The surplus must be created in the capture: the source's
active video is narrower than the 1200 units being captured, and the excess
digitises as green rather than black.

Bounded by two observations, one of which only a person watching the screen can
make:

```
active video > 960 units    real content was clipped at 960 (photo 23)
active video < 1200 units   green present when capturing 1200 (photo 22)
```

### RESOLVED: why is the baseline clean

Answered by the FIFO limit above — the baseline captures 1000 units, just under
the ~1056 cap, so it never trips it. The section below is kept as written because
the reasoning in it was the thing that eventually led to the answer.

#### (original entry)

The active-region model predicts the preset's own baseline should show the same
fault, and it does not. Baseline captures 1000 units of a 1173-unit line — the same
~85% fraction as the green state — so if only ~62.5% of the line is active, roughly
530 px at the right of its 2000 px memory window should have no valid data behind
it. Photo 16 is clean.

Either the invalid region happens to render black at that clock and hides against
the surrounding blanking, or the active fraction is not 62.5% and the preset's
capture window is genuinely matched to it. **Not resolved. Do not treat the
active-region model as settled until this is explained** — it is the same shape of
gap that let the overflow model stand unchallenged for an afternoon.

### The active region is bracketed, not measured

Photo 21 cut the display window to 1865 and the green vanished; at 2492 it was
present. That brackets rather than pins:

```
valid picture ends between output px 1865 and 2492
active video is between 960 and 1371 IF units at PLLAD_MD 3072
```

The 62.5% estimate (960 units) that every capture window in this project derives
from is the **floor** of that bracket, not a confirmed value. Narrowing it needs a
bisection on `VDS_DIS_HB_ST`, one frame per step, each answering only "is the green
back".

**This is also the auto-adjust primitive §8.3 wanted.** The handover argued no
detector can work because border and blanking are indistinguishable from content.
That is true of the *picture*, but irrelevant here: the green appears exactly when
the window passes the end of valid data, so a routine can walk an edge inward until
it disappears and never needs to identify the border at all. Same thing a VGA
monitor's auto-adjust does.

**`produced < window` is new, and it is the one nobody was looking for.** Photo 12
has produced 1280 px in a 2400 px window, so the valid picture should end a little
past halfway across the window (1280/2400). It does: the second copy of the icon
bar, and a **second mouse pointer**, begin around there. The two pointers are the
evidence, not the position — an earlier version of this entry claimed the
measurement agreed "within 1%", which a hand-held photograph cannot support. The
display window keeps clocking data out after the scaler has run out of line, and
what it reads is the line again followed by whatever is in memory.

This retro-explains every unattributed right-edge artefact in this log: the
"graduated grey bar", the "something overlapping on the right" in photo 10, and
`geometry.py`'s own line `N px of window with no picture in it` — which was
printing the diagnosis all along and reading as a note about wasted space.

**Magnification is confirmed as the cause of the detail mangling**, on a clean
single-variable test: photos 11 and 12 differ only in `VDS_HSCALE` (273 -> 512,
x3.751 -> x2.000), same capture window, same `PLLAD_MD`, same output windows.
x3.75 mangles, x2.0 is crisp. So §3's original "the horizontal upscaler degrades
above a threshold", withdrawn earlier today, is **reinstated for this artefact** —
it was withdrawn on the evidence of the comb bands, which really are overflow.
Two artefacts, two causes; the theory was half right and got thrown out whole.

The capture window and the integer sample ratio are both sound, and photo 12 is
the evidence: inside the valid region the text is pixel-crisp at
`PLLAD_MD` 2048 with capture 220..860.

### The bind this leaves

Filling the output line needs a wide `produced`. `produced` at x2.0 is capped by
the capture width, and the capture width is capped by the sample clock:

```
capture_units = 0.625 x PLLAD_MD / 2         (the active region is 62.5% of the line)
produced at x2.0 = 1.25 x PLLAD_MD / 2

to fill 2400 px at x2.0:  PLLAD_MD = 3840
```

`PLLAD_MD` is 12 bits, so 3840 fits — but 3840 x 15.65 kHz = 60.1 MHz needs
`PLLAD_KS` 2 -> 1 for the 40-80 MHz band, and
[gbs-control.ino:4694](../../../GBSC-Pro-Source%20code/gbs-control/gbs-control.ino#L4694)
shows **OSR 4 is unavailable at `ks==1`** — `setOverSampleRatio(4, …)` falls back
to OSR 1, which changes what an IF unit means and would push `IF_HSYNC_RST` past
its 11-bit range. Within the current band the ceiling is ~2556 (docs measured 2553
locking, 3072 not), giving ~1600 px at x2.0.

So full-screen at x2.0 is not reachable by sample rate alone. The other lever is
the **output line total** `VDS_HSYNC_RST` (2600 px): a narrower output line makes
the same produced width a larger fraction of it. That changes output timing and
needs the display clock moved with it, which is §8.2 territory.

## Instability: `PLLAD_MD` changes destabilise the unit

Twice today, and both times only a power cycle recovered it.

- **Spontaneous preset reload.** After `PLLAD_MD` was set to 4095 / `PLLAD_KS` 1
  and verified locked, the firmware reloaded the preset on its own a few minutes
  later, putting `PLLAD_MD` back to 2345 and restoring the preset's own VDS
  windows. Nothing was logged and no command asked for it.
- **ESP dropped off the network.** Setting `PLLAD_MD` 3072 -> 2048 through
  `pllad.py`: the write sequence reported success including the PLL latch, the
  next register read returned `None` on every field, and the unit stopped
  answering ping and HTTP entirely. The scaler kept running and kept a (wrong)
  picture on screen. `/sc?q` and `/uc?h` were both unreachable. Power cycle
  recovered it cleanly, and `HPERIOD_IF` came back correct (431) after having read
  a bogus 83 all evening.

§7 of the previous handover already recorded a third case, suspecting
`externalClockGenDetectAndInitialize()`. Three incidents around the same register
is a pattern, not luck. **Verify reachability before and after every `PLLAD_MD`
change and abort on a failed read rather than continuing the sequence.**

Also worth noting `set_by_registers()` does not run `updateClampPosition()` or
`updateCoastPosition()`. That turned out not to matter — the sync watcher re-runs
them on its own, verified by reading `SP_CS_CLP_ST/SP` as 31/180, exactly the
separate-sync formula for `HTOTAL` 3072 — but it is a real gap in the tool.

## Unexplained

- **Grille bars went uneven again at photo 24** with `PLLAD_MD` unchanged at 3072
  and `HSCALE` moved 671 -> 512. That should be *more* even, not less: 3 IF units
  per source pixel at x2.0 gives 6 output pixels per source pixel, a whole number,
  where x1.526 gave 4.58. No account of this. It is the one observation tonight
  that contradicts the sampling model.
- **The thin bright vertical line at the extreme left** of the picture whenever the
  display window starts at the same position as the memory window. Maskable with a
  small left inset — which is what the preset's own 164 px inset is for — but the
  source of it is not identified.
- **The leading strip** (photos 13-15): a detached band of picture to the left of
  the display window, tracking the window position, upstream of display blanking.
  Same family as the bright line, probably the same cause.

## THE MDF SETTLES THE GEOMETRY: the border is 44 px, not one or two

Three sessions of arguing about how much of the line is active video, and the
answer was on the RISC PC's own disc the whole time. `!Boot/Resources/Configure/
Monitors/Acorn/AKF50`, the monitor definition actually in use — **not**
`RetroScale`, which is a different file with a different 320x256 mode at 7.15 MHz:

```
h_timings: 36, 30, 44, 320, 44, 38    sync, back porch, LEFT BORDER, display, RIGHT BORDER, front porch
v_timings:  3, 16, 17, 256, 17,  3    sync, back porch, TOP BORDER,  display, BOTTOM BORDER, front porch
pixel_rate: 8000 kHz                  total 512 px x 312 lines
```

So §1's "512 px at 8 MHz, 320 active" was right, and it came from here. What was
missing is that **the visible region is not the display region**: border + display
+ border is 408 px, and the border alone is 44 px each side. At 1173/512 = 2.291
IF units per source pixel:

| region | source px | IF units |
|---|---|---|
| display only | 320 | 733 |
| one border | 44 | 101 |
| full visible | 408 | 935 |

`mdf_modes.py` computes totals but does not print the timing breakdown, which is
the part that mattered. The border fields are what you are trimming.

**Caveat, unresolved.** The MDF arithmetic and the tuning-by-eye disagree by about
4%: a capture of 765 units read as ~1 px of border remaining, where the arithmetic
says 765 units is 334 source px, i.e. ~7 px of border per side. Either the units
per source pixel is not 2.291, or a display-window offset is hiding border before
it reaches the eye. Not resolved. Do not treat 2.291 as established.

## LIVE vs SCRATCH: animate the card and read which pixels flip

`TestPat.bas` flips the screen border and the outermost ring twice a second.
Anything that flips is being written by the input formatter this frame; anything
frozen is memory the scaler has stopped writing. Film it rather than photograph
it.

This settled two things no static card could:

- **Junk beside a trimmed picture identifies itself.** Trim the capture without
  re-scaling and the product no longer fills the display window; the tail shows a
  photograph of an older state, including old *magenta border*, which is
  indistinguishable from live border in a still. Photos 29-30 are the proof.
- **A frozen whole display means capture has stopped entirely**, not that the
  geometry is wrong. Seen while pushing `IF_VB_ST`/`IF_VB_SP`: past a certain
  point the vertical window selects no lines at all and nothing is written. It is
  a cliff, not a gradient, which is why the vertical feels finicky. Suspected the
  wrapping blanking region; `IF_VB_SP` 55 with `IF_VB_ST` 0 avoids the wrap
  (blanking 0..55, active 55..310 = exactly 256 lines) and is the arrangement to
  try first.

## FRACTIONAL OVERFLOW: `HSCALE` 407 vs 408

The product must be **≤** the memory window, and the excess can be a fraction of
a pixel:

```
795 x 1024 / 407 = 2000.20 px   vs 2000 px window   -> moving comb bands (photo 27)
795 x 1024 / 408 = 1995.29 px                       -> clean (photo 28)
```

`geometry.py` formatted the product with `:.0f`, so it printed "2000" and "+0 px
vs produced" while the picture tore. Fixed: it now prints two decimals, flags the
overflow, and computes the smallest safe `VDS_HSCALE` for the current capture.

This is the likely explanation for a chunk of the "magnification degrades"
history. It is not that magnification degrades; it is that arbitrary `HSCALE`
values land fractionally over the window and the rounding hides it.

## The input formatter is in interlaced mode on a progressive source

`IF_PRGRSV_CNTRL` = 0 ("source is interlaced") and `IF_LD_SEL_PROV` = 0
("interlace read reset timing"), on a 320x256 progressive source with an odd
`VTOTAL` of 311. The firmware only sets them for `videoStandardInput` 3/4/8/9;
this source takes the 1/2 branch, which writes `IF_LD_SEL_PROV(0)` and never
touches `IF_PRGRSV_CNTRL` at all.

One classification decision produces two symptoms: the line-double FIFO stays
engaged, which is where the ~1056-unit cap comes from, **and** the chip splits a
progressive frame into two phantom fields — which is why moving `IF_LINE_ST` /
`IF_LINE_SP` was observed to affect only every second line. There is no second
register set for a second field; the RD labels both "Progressive line start/stop
position", and S1_24/26 are `IF_HBIN_ST`/`IF_HBIN_SP`, a different pair.

Untested: setting both to 1. It changes the line doubler's read reset timing, not
the FIFO bypass, so §3's warning about halving the picture height does not
strictly apply — but the vertical is the thing most likely to move.

### Correction: `IF_LINE_ST` is not where the `+0x40` comes from

The earlier entry claiming `IF_LINE_ST` = 0x40 explains `/sc?n`'s `+0x40` is
wrong. After a preset reload it reads **10**. The `IF_LINE_SP` half does hold —
1237 = `IF_HSYNC_RST` 1172 + 65 — so the `+0x40 + 1` formula is real, but its 64
comes from somewhere else.

## SOLVED: full screen, clean, 2026-08-03

Photos 31-33. `SOLVED-mode13-fullscreen-clean-2026-08-03.json` for the test card,
`SOLVED-nevryon-fullscreen-2026-08-03.json` for the game.

```
PLLAD_MD 2553    IF line 1277 units
capture          237..1119 = 882 units   (Nevryon: 296..1149 = 853)
HSCALE 665       x1.5398   product 1358.1 px
VSCALE 914       x1.1204
output raster    1445 x 1622
memory  window   9..1384
display window   146..1372
```

Three faults had to be separated before this was reachable, and conflating them
is what cost the earlier sessions:

| symptom | cause |
|---|---|
| moving comb bands, tearing | product **exceeds the memory window** — by as little as a fraction of a pixel |
| aliasing, occasional wrong pixels | **non-integer magnification**; `HSCALE` = 1024/n so only x2, x4, x8 are exact |
| green blocks | the **line-double FIFO cap** at ~1056 IF units |

Note the solved state runs `HSCALE` 665 = x1.5398, which is *not* an integer
ratio — so the aliasing fault is present in principle and is simply not visible
at this scale on this material. The integer-ratio route (`PLLAD_MD` 2048, 640
units, exactly x2) remains untried and would be the cleaner configuration.

### The aspect is a raster problem, not a tuning problem

The card's cyan square is drawn with equal sides, so it reads out the aspect
directly. At the solved geometry:

```
picture fills 0.848 of the line and 0.962 of the frame
on a 16:9 panel that presents as   1.568 : 1
a 320x256 source wants             1.250 (square pixels) or 1.333 (4:3)
```

~17.5% too wide. The cause is that the output raster is 1445 x 1622, which the
panel stretches to fill 16:9 — the scaler was being asked to compensate for a
raster that does not match the display. **Full width and correct aspect are
mutually exclusive** for a 5:4 source on a 16:9 panel: it either stretches or it
pillarboxes.

Resolved in the end by **turning on the TV's own 4:3 correction** (photo 33)
rather than by any register. Worth remembering before tuning aspect at the
scaler: check what the display is doing to the raster first.

## Where this stands, and the next test

The unit is at the **preset baseline** after a power cycle: `PLLAD_MD` 2345,
capture 120..1120 (1000 units), memory 392..2392, display 556..2484, `HSCALE` 512.
Clean and pixel-perfect, filling 74% of the output line.

**The input side is now understood and should be left alone.** The preset's clock
and capture window are correct: 1000 units is just under the FIFO cap and covers
the full active region. The input formatter cannot upscale — its DDA scaler
generates a *write enable*, so it can only drop samples — so the entire
enlargement job belongs to `VDS_HSCALE` on the output side. Input and output are
decoupled; full screen is purely an output-side problem.

**The one question left is whether magnification alone degrades the picture.** It
has never been tested in isolation: every previous attempt moved the capture, the
clock or the windows at the same time. With the baseline capture inside the FIFO
cap and both windows sized to the product, `HSCALE` is the only variable:

```
VDS_HSCALE   512 -> 427          x2.398,  produced = 1000 x 2.398 = 2398 px
VDS_HB       392..2392 -> 100..2498       memory = produced, +0
VDS_DIS_HB   556..2484 -> 120..2498       20 px left inset for the edge line
capture, PLLAD_MD                          unchanged
```

Everything should simply get ~20% bigger, filling 92% of the line instead of 74%.

- **clean** -> magnification never was the problem, every "the upscaler degrades"
  observation across two sessions was a window or FIFO fault in disguise, and full
  screen is solved by `HSCALE` plus matched windows.
- **corrupt** -> magnification genuinely degrades, isolated for the first time with
  everything else known-good, and the fault is in the interpolator rather than the
  geometry.

Watch the icon bar text and the grille bars — degradation showed there first every
time tonight.

---

# 2026-08-03 — SOLVED: full screen, clean, MODE 13 and Nevryon

Photos 31-33. Final register set: `SOLVED-final-2026-08-03.json`.
Preferences: `SOLVED-nevryon-frametimelock-on-2026-08-03.prefs.txt`.

```
PLLAD_MD 2553   line 1277 IF units
capture  296 .. 1122 = 826 units
HSCALE   665 = x1.5398   ->   product 1271.92 px
memory     6 .. 1387 = 1381 px      headroom +109.08 px
display  146 .. 1372 = 1226 px
frame    1126 lines   VSCALE 505   IF_VB 57..53   VDS_VS 11..95
```

Restore with `--segments 1,3,4,5 --repeat 2`. The default `--segments 5` restores
none of the geometry.

## THE FINDING: the memory window needs *headroom*, not just fit

Every previous entry modelled this as "product must be <= the memory window".
That is wrong, and it is why three sessions of tuning felt like guesswork.

| capture | memory | HSCALE | product | slack | result |
|---|---|---|---|---|---|
| 877 | 1363 | 660 | 1360.68 | 2.3 px | artefacts |
| 877 | 1363 | 665 | 1350.40 | 12.6 px | artefacts |
| 882 | 1375 | 665 | 1358.15 | 16.9 px | **clean** |
| 877 | 1363 | 670 | 1340.37 | 22.6 px | **clean** |

**Rows 2 and 3 are decisive: the same `HSCALE` 665.** Same ratio, same
interpolation, same phase, same clocks — only the slack differs, and only that
changed the outcome. So the governing quantity is `memory window - produced`,
and the threshold is between **12.6 and 16.9 px**.

    DESIGN RULE:  memory window >= product + ~20 px

Physical reading: the scaler must finish reading the line out of memory before
the line period ends, so it needs slack. Nominal fit is not enough.

This retro-explains the 407/408 result above (2000.20 px into a 2000 px window
tore, 1995.29 was clean) and the apparently non-monotonic HSCALE sweep, which was
headroom moving under the knob as the product changed.

`geometry.py` now prints headroom on every run and warns below 13 px, with the
smallest HSCALE that restores 20 px. Verified against all four rows.

### What the decisive pair killed

- **Resampling phase / "exact ratios are clean".** HSCALE was *identical*.
- **Clock beat frequencies, PLL_MS.** Nothing in the clock domain moved.
- **PCB parasitic coupling.** Same frequencies, same edge activity.
- **Plain overflow.** Both products fit inside their windows.

Not disproven as phenomena — just not the cause of this.

### Unverified

Measured at one clock and one output preset. A timing budget should scale with
the line period, so on the 2600 px preset the threshold may be a similar
*fraction* of the line rather than the same pixel count. Re-measure before
trusting 13/20 px there — and that is also the cleanest test of whether it is a
budget at all.

## The periodic vertical tear was the frame-rate beat

A tear sweeping through every ~2 s = input and output differing by ~0.5 Hz
(input 15625/311 = 50.24 Hz against the Si5351-driven output).

Fixed by the **FrameTime Lock** toggle in the gbscontrol UI (`gbs-message="5"`
-> `/uc?5` -> `uopt->enableFrameTimeLock`), with `frameTimeLockMethod` 1 rather
than the default 0. The webui help documents it exactly: *"keeps the input and
output timings aligned, fixing the horizontal tear line that can appear
sometimes"*.

**It defaults to OFF** in `loadDefaultUserOptions()`, and it is a SPIFFS
preference, not a register — so a register restore will not bring it back.

## Saving a preset captures hand tweaks

`capturePresetRegisters()` does a live `readFromRegister()` off the chip, so
`/uc?4` bakes in whatever you have tuned. It covers 432 registers:
`0:40-5F, 0:90-9F, 1:00-2F, 3:00-7F, 4:00-5F, 5:00-6F` — narrower than
`dump_registers.py` in segments 1 and 5, but every geometry register we touch is
inside it.

It disables scanlines first, and would reset FrameSync to avoid baking in a
correction — but that is guarded by `if (!rto->extClockGenDetected)`, and this
board has the Si5351, so that branch is skipped and the save never enters the
wedge path. It switches `presetPreference` to `OutputCustomized`, so the unit
then boots into the tweaks. `/uc?p` reverts.

## PLLAD: range selection, and registers that must move together

- **`PLLAD_KS` and `PLLAD_CKOS` are coupled.** `doPostPresetLoadSteps()` writes
  `KS`, then `setOverSampleRatio()` — which sets `CKOS` as a function of `KS` —
  then does **one** `latchPLLAD()`. Changing `KS` alone by hand will not lock.
  Clicking a preset in the UI re-runs the coordinated set; that is why it fixed
  a stuck PLL.

  | OSR | KS=1 | KS=2 | KS=3 |
  |---|---|---|---|
  | 4 | falls back to 1 | CKOS=0 | CKOS=1 |
  | 2 | CKOS=0 | CKOS=1 | CKOS=2 |

- **The PLL ceiling is a range, not a wall.** `KS` picks the band: `01` =
  80-40 MHz, `10` = 40-20 MHz (current), `11` = 20-min. At `PLLAD_MD` 2553 the
  ADC clock is 39.89 MHz, right at the top of the current band — which is why
  2573 hunted and 2583 wedged. Measured ladder on this source: 2553 solid, 2562
  locked, 2573 hunting, 2583 wedged.
- **`PLLAD_MD` per preset**: PAL tops out at 2553 (`pal_downscale`,
  `pal_1920x1080`). `pal_768x576` is 2345 with HSCALE x2 and the 2600 px output
  line.
- **The 1056-unit FIFO cap is doubtful** — the solved state captures to 1122,
  well past it.

## Two operational traps that cost hours

1. **Unplug the USB serial cable before power cycling.** It back-powers the
   board, so a power cycle with USB attached does not reset the ESP or the AV
   board. Every recovery cycle became a partial reset, the scaler stayed
   unprogrammed and the LCD stayed dark — indistinguishable from a dead board. It
   also made a cool-down test appear to disprove thermal when the ESP had never
   lost power.
2. **`stty -F /dev/ttyUSB0 115200 -hupcl raw -echo` before touching serial**, or
   opening the port toggles DTR/RTS and resets the board. Three HDMI drops came
   from exactly that. Boot ROM prints at 74880 baud.

Also: one client at a time. A stale `regpanel.py` had been up 23 hours and
`soak_watch.py` 22 hours; with the browser that was four clients. Several phantom
readings — `PLLAD_MD` reading 0, a capture window that moved on its own — trace
to that. `soak_watch.py` is read-only and its log was the best instrument of the
night: it timestamps every sync drop and PLL unlock without touching the unit.

## The firmware hang — diagnosed, still unfixed

`framesync.h`, `vsyncInputSample()` and `vsyncOutputSample()`: `ESP.wdtDisable()`
then a 3,000,000-iteration spin with no `yield()`, whose only exit is a vsync
pulse. A `PLLAD_MD` write big enough to break sync means no pulse ever arrives.
Serial goes silent, ping and HTTP die, the association stays up in hardware, and
the picture keeps running because the TV5725 is a separate chip. The caller
`continue`s on failure with its print commented out, so the retry is silent and
the hang permanent.

**A bare timeout is not the fix.** 500 ms plus `yield()` was tried and flashed: it
converts the hang into a self-recovering reset, but the reset leaves the scaler
unprogrammed and the caller's `continue` turns it into a silent infinite retry.
Reverted. A correct fix must fail *upward* — give up, report, and leave the
system able to re-acquire.

Distinguishing a wedge from a reboot without touching the unit:

```sh
ssh router "iw dev phy1-ap0 station dump | grep -A6 fc:f5:c4:b1:f2:38"
```

Station present with low inactive time but no ping -> wedged. Absent -> rebooted.

## Method

- **The eye beat the derivation, again.** The solve came from patient manual
  sweeping of `VDS_HB_SP` and `HSCALE`. Every model argued for during the session
  — thermal, a firmware patch, the input mux, resampling phase, clock beats, PCB
  coupling — was wrong, and one controlled A/B settled it.
- **Change one thing.** The decisive result was a pair differing in exactly one
  register.
- **Snapshot the moment it looks right.** A stable `PLLAD_MD` 2048 picture
  earlier in the session was never captured and was lost.
- **Do not flash on a theory.** A patch went onto the only unit unverified, and a
  symptom that survived the rollback proved the theory wrong anyway.

## Still open

- **`geometry.py` ignores the offset between the memory and display windows**, so
  its "N px cropped" line is wrong whenever the two do not share a start. It is
  also horizontal-only — it says nothing about vsync, `VDS_VSCALE` or either
  vertical window.
- **The write origin is assumed to be `VDS_HB_SP`** and has never been measured.
  Moving it and watching which way the picture slides would settle it.
- **Vertical is not solved the way horizontal now is.** At VSCALE 505 the scaler
  produces ~622 lines into a 1083-line display window, so the TV is doing some of
  the scaling.
