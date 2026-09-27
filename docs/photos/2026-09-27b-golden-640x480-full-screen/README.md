# Golden: 640x480@60 with the green border reaching the frame

**Supersedes `2026-09-27-golden-640x480-full-screen`**, which was found by hand
before `HsyncStartPx` and `TransmittedWindowDelayPx` were changed. This one sits
on top of both, so the gap between it and the engine's own solve is two numbers
rather than four.

## What the engine now solves by itself, and what is still hand-set

The whole output side comes out of the solve. Two capture-side registers do not:

| field | solved | golden | what it does |
|---|---|---|---|
| `IF_HB_SP2` | 261 | **259** | the capture's near edge, opened two units earlier |
| `VDS_HSCALE` | 855 | **856** | the scale nudged so the produced width is unchanged |

**`VDS_HB_SP` stays at 66, and that is the point.** Raising
`Axis::captureMargin` reaches the same capture start but drags the write origin
with it, which cancels the gain -- measured, the emitted frame went from two
black columns to three and the green washed out. What works is opening the
capture earlier while the write stays where it is, and paying for the two extra
units in the scale.

## The state

640x480@60 on `vga`, separate sync, scaling path, output 1080p, automation
frozen for the capture.

```
VDS_HS_ST     2      VDS_HS_SP     34     VDS_HSYNC_RST 1599
VDS_HB_SP    66      VDS_HB_ST   1547     VDS_HSCALE     856
VDS_DIS_HB_SP 152    VDS_DIS_HB_ST 1547
VDS_VB_SP    34      VDS_VB_ST   1120     VDS_VSCALE     455
VDS_DIS_VB_SP 40     VDS_DIS_VB_ST 1120
PLLAD_MD   1456      IF_HB_SP2 259 .. IF_HB_ST2 1429    IF_HB_SP 72
```

The framing is the engine's own state and no register holds it:
`oh 262, eh 1166, ch 1457` -- `poh 1798, peh 8003, pov 533, pev 9143` in
ten-thousandths, which is what `/geometry` reports and what a framing table
stores.

## What the emitted frame carries

Two black columns at the left, then the green border, strongly coloured at
both ends:

| | col 2 | col 3 | col 4 | col 5 | | col 1916 | col 1917 | col 1918 | col 1919 |
|---|---|---|---|---|---|---|---|---|---|
| R | 32 | 96 | 86 | 133 | | 178 | 160 | 111 | 96 |
| G | **85** | **149** | **199** | **246** | | **246** | **228** | **228** | **212** |
| B | 22 | 86 | 73 | 120 | | 173 | 155 | 104 | 89 |

Vertically flush, top and bottom. **The two black columns at the left are what
is left of the goal**, and the border being saturated rather than washed out is
the change: before these registers it read near-neutral, which is why every hue
detector reported it missing.

## Restoring it

```sh
curl 'http://<ip>/freeze?on=1'
printf 'MODE X640 Y480 C256 F60\n' | nc 192.168.88.10 6502
printf 'PATTERN CARD\n'            | nc 192.168.88.10 6502
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore tools/gbsc-pro-hwtest/snapshots/golden-640x480-full-screen-2026-09-27b.dump.json \
  --segments 0,1,2,3,4,5 --repeat 2
```

`…dump.json` is the 608-register config capture `--restore` reads; the `.json`
beside it is a full 1536-register capture for `snapdiff.py`.

## The images

`emitted-frame.png` is the whole emitted frame off the USB capture, so a column
index is a measurement rather than a judgement. `left-edge-40x.png` is the left
24 columns at 40x: two black columns, then the green border.
