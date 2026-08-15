# 2026-08-05 — horizontal geometry, photographed

Bench photographs from the session that measured the write origin. Source is the
RISC PC at VTOTAL 311, `PLLAD_MD` 2553 (line = 1277 IF units), output raster
1445 px, `VDS_HSCALE` 1023 (1:1) throughout. Written up in
[../../riscpc-game-modes.md](../../investigations/riscpc-game-modes.md), section
*2026-08-05 afternoon*.

Register states are from a `geometry.py` read taken adjacent to each photo.
Where a photo falls between reads the state is marked approximate.

**The images themselves live in
[gbsc-pro-bench-photos](https://github.com/TheCodeSharman/gbsc-pro-bench-photos),
not here.** This repo is a public fork, GitHub refuses LFS uploads to a fork,
and untracking the 146 MB cut the nix evaluation copy from 211 MB to 65 MB. That
repo mirrors these paths, so copying its tree into a checkout lands the files
beside this table, where `.gitignore` keeps them untracked:

```sh
git clone https://github.com/TheCodeSharman/gbsc-pro-bench-photos.git
cp -a gbsc-pro-bench-photos/docs gbsc-pro-bench-photos/tools <gbsc-pro checkout>/
```

The table below is the part worth reading anyway — it is what each photograph
decided, which is not recoverable by looking at the photograph.

| # | photo | capture `IF_HB_SP2..ST2` | memory `VDS_HB_SP..ST` | display `VDS_DIS_HB_SP..ST` | what it shows |
|---|---|---|---|---|---|
| 01 | `01-1503-baseline-custom-preset` | `104..1124` (1020) | `170..1204` (1034) | `256..1262` (1006) | The custom preset as loaded. Desktop inset, black borders both sides, clean. Headroom 14 px. |
| 02 | `02-1507-pan-plus100-green-bar-right` | `204..1224` (1020) | `170..1204` | `256..1262` | Capture panned +100. Content moved left; **green garbage bar** on the right where the capture ran past the source's active video. |
| 03 | `03-1525-corruption-six-registers-at-once` | `354..974` (620) | `170..810` (640) | `190..790` (600) | **Six registers changed in one step.** Whole desktop present but shredded. Attributable to nothing; discarded. |
| 04 | `04-1538-baseline-restored-clean` | `104..1124` | `170..1204` | `256..1262` | Baseline restored from snapshot, clean — matches 01. |
| 05 | `05-1539-ifhbsp2-124-content-panned-left` | `124..1124` | `170..1204` | `256..1262` | Single edge moved +10. Content panned **left**, frame stayed — first direct evidence the origin does not track the capture. |
| 06 | `06-1543-ifhbsp2-284-repeat-strip-right` | `284..1124` (840) | `170..1204` | `256..1262` | Display window now wider than the picture. Main image, black band, then a **repeat of the line's tail** where the read runs past valid memory. |
| 07 | `07-1546-green-corruption-firmware-unfrozen` | `104..1124` (reverted) | `170..1204` | `332..1262` | **Firmware was unfrozen** after an unnoticed reboot. Hand-set capture values reverted, `HPERIOD_IF` latched at 177, full green corruption. |
| 08 | `08-1551-dis-249-1023-clean` | `284..1124` (840) | `170..1204` | `249..1023` (774) | Display window pulled inside the picture. Clean — no repeat, no garbage. |
| 09 | `09-1603-testpat-loaded` | `260..1061` (801) approx | `170..1204` | `248..1048` (800) | `TestPat` loaded as an instrument. All four edge markers present (3 blue top, 2 green left, 4 yellow right, 1 red bottom), `BAND 8`. |
| 10 | `10-1620-testpat-left-aligned-garbage-right` | `264..1061` (797) | `49..1204` (1155) | `127..1048` (921) | Origin moved to 127 = the panel's left edge. Card clean and left-aligned; the remaining junk on the right is the display window running past the picture, which ends at 923 (inclusive). |

## 13 / 14 — the vertical axis

| # | photo | state | what it shows |
|---|---|---|---|
| 13 | `13-1712-vertical-top-aligned-scratch-below` | `IF_VB` 22..568, `VDS_VB` 37..978, `VDS_DIS_VB` 19..1065, `VSCALE` 660 | Picture's top on the panel's top edge (origin 63). The cyan and magenta stripes below it are the 156 lines of display window running past the picture end at 909 — scratch, not picture. The 44 lines of scratch *above* are invisible because they fall above the panel's top edge. |
| 14 | `14-1725-both-axes-clean-measured` | `IF_HB` 264..1061 / `IF_VB` 56..569, `VDS_HB` 49..1074 / `VDS_VB` 37..831, `VDS_DIS_HB` 127..923 / `VDS_DIS_VB` 19..833 | **Both axes clean and fully measured.** Every register traceable to a measured constant or the AKF50 mode file. Registers saved as `snapshots/measured-both-axes-2026-08-05.json` (608 registers, segment 2 included). |

Note 14 sits at **-1.9 lines** of vertical headroom and is still clean — the same
margin horizontally would shred the picture. Tentative evidence that the
headroom rule is a horizontal line-time constraint with no vertical equivalent.

## 15 — the horizontal headroom threshold, bracketed at HSCALE 1023

`15-source-video-IMG_1253.mov` (4.3 s, 30 fps, 1920x1080), still extracted at
`15-1818-vds-hb-st-847-shredded.jpg`. State throughout:

```
IF_HB 264..1062 (798 units)   PLLAD_MD 2553   HSCALE 1023, BYPS 0
VDS_HB_SP 49                  VDS_DIS_HB 129..927        output line 1445 px
IF_HS_INT_LPF_BYPS / IF_HS_TAP11_BYPS both 0 — the low-pass filters were ON
```

`VDS_HB_ST` was moved with everything else held fixed, so only the memory window
— and therefore the headroom — changed:

| `VDS_HB_ST` | memory window | headroom vs produced 798.78 | verdict |
|---|---|---|---|
| 847 | 798 px | **-0.78 px** | **shredded** — the video |
| 881 | 832 px | **+33.2 px** | stable |
| 883 | 834 px | **+35.2 px** | stable, and steadier still |

**What the video shows.** The card is structurally intact — `BAND 8`, the circle,
the bars and all four edge markers are in place, so this is not a lost capture.
Every *vertical* edge is shredded into horizontal ribbons, and the damage is
worst toward the right of the line, which is the signature of the scaler failing
to finish reading the line before the line period ends rather than of a window
addressing invalid memory. It is steady across frames 2 s apart, not transient.

The filters being on makes this a *stronger* positive, not a weaker one: the
corruption is gross enough to survive the low-pass that would have hidden a
subtle one-pixel tear.

**Why this matters more than the bracket itself.** `geometry_math.py` carries
`HEADROOM_MIN_PX = 13`, bracketed on 2026-08-03 at the *same* clock and the
*same* 1445 px output line — but at HSCALE 660-670, where 12.6 px artefacted and
16.9 px was clean. At HSCALE 1023, 16.9 px is not remotely enough.

### The threshold is a function of HSCALE

Isolated the same evening by holding capture at 798, `VDS_HB_SP` at 49, the
clock at `PLLAD_MD` 2553 and the output line at 1445 px, and moving **only**
HSCALE — then creeping `VDS_HB_ST` down to the tearing edge at each:

| HSCALE | magnification | produced | boundary `VDS_HB_ST` | memory window | headroom needed | status |
|---|---|---|---|---|---|---|
| 665 | x1.540 | 1358.15 | — (2026-08-03) | 1375 | **~13-17** | separate session |
| 850 | x1.205 | 961.36 | 971 claimed | 922 | -39.4 | **INVALID** |
| 993 | x1.031 | 822.91 | 896 | 847 | 24.1 | **suspect** |
| 1023 | x1.001 | 798.78 | 881 | 832 | **33.2** | **valid** |

**Only the 1023 row is trustworthy, and the reason is a trap worth its own
note.** `VDS_DIS_HB_ST` was left at 927 — sized for the HSCALE-1023 picture —
while HSCALE was lowered. Lowering HSCALE *grows* the picture, so the unmoved
display window progressively blanked its right-hand end:

| HSCALE | picture ends at | display window ends | hidden |
|---|---|---|---|
| 1023 | 927.8 | 927 | 0.8 px |
| 993 | 951.9 | 927 | 24.9 px |
| 850 | 1090.4 | 927 | 163 px |

The tearing appears **worst toward the right of the line**, because that is where
the scaler runs out of line time. So the display window was blanking exactly the
region the measurement depends on. At 850 the "stable" state is a 39 px overflow
with its evidence hidden — `geometry.py` reports `!! OVERFLOW` for it. At 993 the
amount hidden (24.9 px) is indistinguishable from the headroom measured (24.1),
so that point cannot be told apart from the edge of the blanking.

**When sweeping HSCALE, move `VDS_DIS_HB_ST` to `129 + produced` at each step**
before creeping `VDS_HB_ST`. `VDS_DIS_HB_SP` stays at 129 — that is the origin,
and it does not move with HSCALE.

What survives: at HSCALE 1023 the requirement is 33.2 px against a rule that
permits 13, and the 2026-08-03 bracket that produced the 13 is entirely at
HSCALE 665. So the threshold is not one constant. The *shape* of its dependence
on HSCALE is not yet measured.

All four `SOLVED-*` snapshots sit at HSCALE 665, which is why the original
bracket looked tidy and why nothing caught this: the rule was fitted in one
regime and never exercised outside it.

Leading explanation: HSCALE sets the line-memory reads per output pixel
(`HSCALE/1024`). At 665 the read engine idles about a third of the time and can
absorb a stall; at 1023 it runs flat out, so nothing is recoverable and all the
slack must be pre-paid. The collapsing idle fraction fits the steepness near
unity. Three points, one clock and one output line — the mechanism is a
hypothesis, the measurements are not.

## 11 / 12 — the flash, and why it needed two frames

Extracted from `11-12-source-video-IMG_1252.mov` (3.4 s, 30 fps) at 4 fps; the
border flips twice a second, so consecutive sampled frames land in opposite
phases. State at the time:

```
IF_HB  264..1061   HSCALE 1013   VDS_HB  49..1074    VDS_DIS_HB  127..923
IF_VB   20..595    VSCALE  605   VDS_VB_SP 114 / ST 70   VDS_DIS_VB   42..1106
```

| | 11 — phase A | 12 — phase B |
|---|---|---|
| screen border | **magenta** | **cyan** |
| outermost ring | **white** | **yellow** |

**What the pair proves.** The wide top and bottom bands change colour between
the two frames, so they are being written this frame — **live captured source
border**, and the input capture window is taking in more than active video.
The thin outermost strips at the extreme top and bottom are *the same pale cyan
in both frames*: static, therefore **frame-buffer scratch outside the active
capture** that the scaler is no longer writing.

Those two things look identical in any single photograph and mean opposite
things — one says trim the capture, the other says pull the display window in.
This is the case the card's animation exists for, and it is why the write-up
says to film it.

## 16 — the frozen band that found the moving origin

`16-2236-x3.2-frozen-magenta-band-left-of-picture.jpeg`, capture 264..464 at
HSCALE 320 (x3.2), display window 129..809.

The top border runs **cyan**, flipping — live captured source. The block at its
left end is **magenta and frozen**, and it is ~41 px wide. Same technique as the
11/12 pair above, and the same reading: static means frame buffer the scaler is
no longer writing.

What made it worth chasing is that the display window was *correct* by the model
then in use — 680 px, exactly what `produced` asked for, every register reading
what it had been set to. The scratch was at the near edge, which nothing had ever
measured.

It is there because **the corner is not a constant**. The scaler starts writing
`55 + 25 x magnification` px after `VDS_HB_SP`, so a window pinned to a corner
read once at x1.58 begins before the first written pixel at every higher
magnification: 11 px of scratch at x2, 41 at x3.2, 61 at x4. Measuring from that
moving origin is also what made `produced` look like it had a loss term for two
years. See `docs/scaler-geometry-model.md` and `measure_origin.py`.

## Reading these

**Magenta and cyan are the same screen border**, alternating twice a second. The
green 1-pixel frame does not flip and marks the true edge of active video. A
*static* coloured line is stale frame-buffer scratch, not live captured video.
See the `TestPat` section of the write-up before drawing conclusions from any of
these.

**Do not measure position from these photographs.** They are hand-held and the
camera moved between shots; comparing absolute edge positions across two of them
produced a confident wrong conclusion during the session. Only discrete events —
garbage appearing or vanishing, a colour changing — are reliable evidence here.
