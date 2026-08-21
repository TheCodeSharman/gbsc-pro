# The tail green

A green band appears at the right of the picture. **`PB_CAP_OFFSET` positions it,
and 230 removes it entirely** — measured on the bench across the horizontal zoom
range, where the 260 the firmware used to write leaves it. `Tv5725::Memory`.

So it is not captured content. It is **unwritten frame buffer**, and the colour is
arithmetic rather than a clue: U and V are offset-binary, so zero is not neutral.
Decoded with the coefficients the firmware itself uses,

```
Y = U = V = 0
R = Y + 1.402  (V-128)  = -179  -> clamped to 0
G = Y - 0.344  (U-128) - 0.714 (V-128)  =  135
B = Y + 1.772  (U-128)  = -227  -> clamped to 0
```

`RGB(0,135,0)`. Any unwritten or unreached region of the buffer displays as
saturated green, which is why the artefact looked like a green *signal* problem
for as long as it did.

Measured on a RiscPC with a stock AKF50 mode file, 320x256@50, at
`PLLAD_MD` 2528 and `IF_HSYNC_RST` 1264 — a 1265-unit line.

## Why every source-side explanation failed

All four were tested on the bench and all four hold. They were refuted because
they are explanations of *captured content*, and the band is not captured content.

- **Not the source's blanking.** The band starts inside the right border, not at a
  border or porch edge, and the border either side of it renders black.
- **Not the ADC's black level.** The right border renders black immediately before
  the band, so the black reference is correct there. This also disposes of the
  standing suspicion of `SP_CS_CLP_ST`/`SP` — they are measurably inside the sync
  tip at IF 13..74, which is worth fixing on its own terms, but a wrong black
  reference would spoil the whole blanking interval rather than start part way
  through the border.
- **Not sync-on-green.** `ADC_SOGEN` is 1 with `SP_SOG_MODE` 0 — the ADC's
  green-channel slicer running while the sync processor is on separate sync.
  Setting `ADC_SOGEN` = 0 does not remove the band.
- **Not the game's border colour**, and **not `VDS_VSYN_SIZE1`/`SIZE2`**, which are
  the output vertical raster and cannot bound a horizontal input capture.

## Capturing the hsync pulse produces green too, and that is a different thing

Shown directly. With the capture window written by hand to `62..1062` — a start of
IF 62, inside the hsync pulse at IF 0..88.9 — a green band appears on the **left**.
Move the start clear of the pulse (`114..1114`) and the left edge is clean.

That one *is* captured content: the sync tip digitised as video. The head never
shows it in normal use because `InputLine::firstCapture()` returns `syncUnits`,
96 in this configuration, and the solver keeps the window clear of it. Writing
`IF_HB_SP2` directly bypasses that guard.

Both bands are green for the same reason — low code values in a 4:2:2 buffer — but
only the head one comes from the source.

## Unresolved: the capture-side dependence

The band's position was crept before the offset was known, with the window width
held at 1000 units:

| capture stop | source px | result |
|---|---|---|
| 1062 | 430.0 | clean |
| 1114 | 450.9 | clean |
| 1128 | 456.4 | green, ~1 source px wide |
| 1142 | 462.2 | green, thin |
| 1171 | 474.0 | green |
| 1263 | 511.2 | green, wide |

The width tracks `stop − X` with X at IF **1125–1126**, and `PB_FETCH_NUM` was
constant at 250 throughout, the width being held. A pure offset error does not
obviously predict a threshold in the *capture position*, so this table and the
`PB_CAP_OFFSET` result are in tension and neither is in doubt.

AKF50's own line for reference, `h_timings: 36, 30, 44, 320, 44, 38` over 512
source px, at 2.4707 IF units per source px:

```
sync          0 ..  36 px      IF    0.0 ..   88.9
back porch   36 ..  66         IF   88.9 ..  163.1
left border  66 .. 110         IF  163.1 ..  271.8
display     110 .. 430         IF  271.8 .. 1062.4
right border 430 .. 474        IF 1062.4 .. 1171.1
front porch 474 .. 512         IF 1171.1 .. 1265.0
```

X = 1125.5 is source px 455.6, 25.6 px into the 44 px right border — not a
boundary in the mode, which was itself the first sign that the source was not
where the answer lay.

**The experiment is to re-run that creep at `PB_CAP_OFFSET` 230** and see whether
the threshold has moved, gone, or stayed. It was never run against a varying
offset, so the two results have never been measured together.

## Retired: absolute, or relative to the line end

This page used to carry an open question about whether X is a fixed IF position or
a fixed offset from the line end, with a large `PLLAD_MD` change as the experiment.

**Do not run it.** It assumed the band is a capture position, and a memory-side
register removes the band outright, so the divider is the wrong knob and the
measurement would answer a question about the wrong quantity. If the creep above
still shows a threshold at offset 230, the question can be re-posed then — against
whatever the threshold turns out to depend on.

## A clamp is the wrong fix, and must not be reinstated

The obvious repair is to clamp `lastCapture()` at X so the window can never reach
the band. It is rejected, and the offset finding strengthens rather than weakens
the argument: the band is removed without touching the capture window at all, so a
clamp would crop picture to hide something that is not there.

X is a number measured from one source. Another mode's active picture may
legitimately extend past that IF position, and the clamp would crop it silently.
The head guard is legitimate precisely because `syncUnits` is *derived*,
`ceil(units × HLOW_LEN / PLLAD_MD)`, recomputed per solve, so it tracks any
source. An absolute constant does not.

## See also

- [`../scaler-geometry-model.md`](../scaler-geometry-model.md) — the two green
  regions in an IF line, and the arithmetic the capture window is solved from
- [`output-front-porch.md`](output-front-porch.md) — the far end of the output
  line, where a display window taken to the raster's edge produces its own
  green-and-wrapping artefacts
