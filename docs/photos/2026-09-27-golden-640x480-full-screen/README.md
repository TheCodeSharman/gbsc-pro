# Golden: 640x480@60 framed to the edges of the emitted frame

The framing the bench settled on for the goal in
`docs/investigations/full-screen-framing-on-the-vesa-modes.md`: the card's green
border reaches the left, the top and the bottom of the emitted frame, with two
columns of slack at the right.

**Reached by hand, not by the engine.** Two registers differ from what the solve
writes, and both were found on the unit with automation frozen:

| field | solved | golden | what it does |
|---|---|---|---|
| `VDS_HS_ST` | 0 | **2** | the output hsync pulse's start. At 0 the left-most two columns lose 34 rows at each of two bands; at 2 they do not |
| `VDS_DIS_HB_SP` | 159 | **152** | the aperture's near edge, walked until the black at the left closed |

`VDS_HS_SP` stays at 32, so the pulse is NARROWER as well as later — position
and width are still tangled and it is not established which of them matters.

## The state

640x480@60 on `vga`, separate sync, scaling path, output 1080p.

```
VDS_HS_ST     2      VDS_HS_SP      32     VDS_HSYNC_RST  1599
VDS_HB_SP    66      VDS_HB_ST    1547     VDS_HSCALE      855
VDS_DIS_HB_SP 152    VDS_DIS_HB_ST 1547
VDS_VB_SP    34      VDS_VB_ST    1120     VDS_VSCALE      455
VDS_DIS_VB_SP 40     VDS_DIS_VB_ST 1120    VDS_VSYNC_RST  1124
PLLAD_MD   1456      IF_HSYNC_RST 1456     IF_HB_SP2 259 .. IF_HB_ST2 1439
PB_FETCH_NUM 292     PB_CAP_OFFSET 365
```

Emitted frame: **0 black columns at the left, 2 at the right, 0 rows at the top
and bottom.**

## Restoring it

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore tools/gbsc-pro-hwtest/snapshots/golden-640x480-full-screen-2026-09-27.dump.json \
  --segments 0,1,2,3,4,5 --repeat 2
```

**Freeze automation first** (`/freeze?on=1`), or the solver rewrites the windows
back. The source must be on `X640 Y480 C256 F60` with `PATTERN CARD`, since the
capture window belongs to that raster.

`…dump.json` is the 608-register config capture `--restore` reads. The `.json`
beside it is a full 1536-register capture for `snapdiff.py`, which covers the
928 addresses the config dump leaves out.

## What it is evidence for

**The engine's own solve does not reach this**, and the gap is two numbers. The
aperture is seven samples late, which is what
`OutputMode::TransmittedWindowDelayPx` decides, and the hsync pulse starts on
the line origin, which nothing models.

## What it is NOT evidence for

**The left-edge bands are hidden here, not cured.** Putting the picture back
where `VDS_HS_ST` 0 had it, by moving `VDS_HB_SP` and `VDS_DIS_HB_SP` three
samples right, brings them back identically at rows 35..68 and 1014..1047. What
the pulse buys is that they fall off the left edge where nothing shows them.

At `VDS_HS_ST` 2 with the picture unmoved they do not vanish entirely either:
they shrink to single rows every sixth row over the same spans, and reach
column 2 as well as columns 0 and 1.

## The images

`emitted-frame.png` is the whole emitted frame, off the USB capture rather than
a camera, so a column index is a measurement. `left-edge-40x-before.png` is the
left 24 columns at 40x, taken BEFORE either register was moved: two black
columns, then the green border smeared across four columns and desaturated
against the white ring beside it, which is why every hue test reported it
missing.
