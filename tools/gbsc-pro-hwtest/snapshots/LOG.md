# Bench log

What was on screen, what the registers were, and what it proved. Kept so a
finding does not have to be re-established next session.

**Adding an entry.** Capture the state *before* the photograph, never after —
photographs lag register writes, and getting that order wrong has inverted cause
and effect twice in this project:

```sh
nix develop -c python3 tools/gbsc-pro-hwtest/snapdiff.py --host <ip> \
    --save tools/gbsc-pro-hwtest/snapshots/<name>.json --note "<what this is>"
nix develop -c python3 tools/gbsc-pro-hwtest/geometry.py --host <ip> \
    --label "<what changed>" --log ~/geometry-photos.log
```

Then add a row below, drop the photo in `photos/`, and say what it decided.
One change per row.

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

Note `zarch-akf50-fullscreen` is labelled "hand-tuned to full screen", **not**
clean. It sits at the worst overflow in the set and very likely carries the same
edge corruption. It was mistaken for a clean counter-example once already.

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

## Open, and the next test

Capture the display region only and magnify that, so the produced width equals
the window instead of overflowing it:

```
IF_HB_SP2/ST2   ->  252..984   (732 units, the display region per docs §2d)
VDS_HSCALE      ->  312        (x3.28)
VDS_HB          ->  100..2500  (2400)
VDS_DIS_HB      ->  100..2500  (2400)
produced = 732 x 3.28 = 2400 = memory window = display window,  inside a 2600 line
```

This is a real test of the overflow model, because ×3.28 is far above the ~2.1
where §3 recorded tearing. If magnification never mattered it will be clean.
