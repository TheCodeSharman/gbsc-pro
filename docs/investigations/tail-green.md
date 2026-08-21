# The tail green

A green band appears toward the right of the line, and **active picture that
reaches it is destroyed** — not overlaid, not blanked, lost. It is a bound on
usable capture width, and the engine now respects it two ways: see what the
engine does about it, below.

Measured on a RiscPC with a stock AKF50 mode file, 320x256@50, at `PLLAD_MD`
2548 and `IF_HSYNC_RST` 1274 — a 1275-unit line.

## What it is not

Every one of these was proposed with a mechanism and killed on the bench. They
are listed because each is a plausible re-derivation, and rediscovering them
costs a session each.

| refuted | how |
|---|---|
| **the playback offset positions it** | `PB_CAP_OFFSET` 230 does not remove it. 230 is *below* the fetch, so lines overlap and each overwrites its predecessor's tail — it corrupts the gap rather than closing it |
| **unwritten frame buffer** | genuinely unwritten memory reads as **random colour noise**, seen directly by bypassing the vertical scaler so the picture occupies only its captured lines. Flat green and random noise are different states |
| **the source's blanking or front porch** | moving the border/porch boundary by 20 source px at constant line total does not move the band by one pixel |
| **the source's border colour** | the band sits at the same place and width with the test card replaced by the desktop, and with the border black instead of purple |
| **a capture-request shortfall** | `CAP_REQ_OVER` (`s4_22` bit 0) on and off is indistinguishable, tested twice — the second time with the stride bug already fixed, so the right band was in view. It was the only write-side candidate the datasheet offers |
| **the playback fetch** | `PB_FETCH_NUM` swept from 192 upward at a fixed framing has no effect on the band. Past about 224 it introduces TEARING instead, which is the ratio going the other way |
| **sync-on-green** | this source drives separate H and V sync. `SP_SOG_MODE` is 0 and `VSACT` reads 1, the separate-sync path |
| **the ADC's black level** | `ADC_ROFCTRL`, `ADC_GOFCTRL` and `ADC_BOFCTRL` are all 64. A per-channel offset error would tint the whole blanking interval |
| **memory write bandwidth** | `PLL_MS` swept 162 -> 144 -> 108 -> 81 MHz, halving what the write path can absorb per unit time. X does not move by one unit. A fill-versus-drain race cannot survive that |
| **the blank-insert registers** | `HD_BLK_GY_DATA`/`BU`/`RV` are all 0, which decodes to exactly this green — but writing 128 into the chroma pair changes nothing, so that path is not in circuit for RGB input |

## The colour is arithmetic, and it says the zeros are written

```
Y = U = V = 0
R = Y + 1.402  (V-128)  = -179  -> clamped to 0
G = Y - 0.344  (U-128) - 0.714 (V-128)  =  135
B = Y + 1.772  (U-128)  = -227  -> clamped to 0
```

`RGB(0,135,0)`. U and V are offset-binary, so neutral chroma is 128 and zero is
saturated green.

**The zeros are inferred from the colour, not observed.** `PB_ENABLE` = 0 gives
a **white** screen, and an all-ones idle through this same decode would be pink,
so the colour space of that stage is not settled.
[`playback-fetch-and-stride.md`](playback-fetch-and-stride.md).

**This is a digital signature, not an analogue one.** A source at blanking level
puts 0 V on all three channels; digitised and converted that gives `Y=0` with
**neutral chroma**, and comes out black. Only a stage writing zeros into the
chroma bytes produces this colour. So something substitutes blank data — the
question is what, and where in the line it decides to.

## Capturing the hsync pulse produces green too, and that is a different thing

With the capture window written by hand to `62..1062` — a start of IF 62, inside
the hsync pulse at IF 0..88.9 — a green band appears on the **left**. Move the
start clear of the pulse (`114..1114`) and the left edge is clean.

That one is captured content: the sync tip digitised as video. The head never
shows it in normal use because `InputLine::firstCapture()` returns `syncUnits`,
96 in this configuration, and the solver keeps the window clear of it.

## Where X is

X is the position where video stops being written and the green starts.
**X = 1125 IF units = 2250 ADC samples**, counted from the line start, and it is
absolute: `IF_HB_ST2` was crept down one unit at a time to the value where the
band exactly vanishes, and the register at that threshold is the reading. The
capture start was 91 here and 62..263 across an earlier sweep, and X did not
move.

It falls at no boundary in the source's mode — an older reading put it 25.6 px
into the 44 px right border — and that is now a confirmation rather than a
suspicion, because the mode's boundaries were moved by 20 source px at constant
line total and X ignored them.

Bandwidth is refuted (see the table above), which takes "fixed in time" with it.
What survives is a **count**: something tallies samples per line and stops,
indifferent to how fast memory can take them. **What counts to 2250 is not
known.** No register holds it, it is not a power of two, and it is not
`IF_LINE_SP - IF_LINE_ST`.

**Neither datasheet states a line buffer depth**, searched 2026-08-18: DS-5725-3.2
gives no capture width or buffer size at all, and RD-5725-1.1 mentions line
buffers only for the deinterlacer's scaling-down and IIR paths, without depths.
**The safe guards are not it either.** `CAP_SAFE_GUARD_EN` is on, but both
capture guards are written to the top of the address space and are addresses in
the frame buffer rather than positions in a line, so nothing there can stop a
write 1125 units in. The one suggestive number in RD-5725-1.1 is that the chip's
own largest input modes are 2200x1125 and 2640x1125 — a buffer sized for 1080i
would be sized in exactly these units — but that is a coincidence of numbers
until something on the bench moves X.

That makes the divider a lever rather than a wall, since the line is `PLLAD_MD`
samples long whatever the source does:

| `PLLAD_MD` | line, IF units | lost |
|---|---|---|
| 2548 | 1274 | 149 |
| 2400 | 1200 | 75 |
| 2250 | 1125 | none |

**Confirmed at `PLLAD_MD` 2048.** The line becomes 1025 IF units, the capture is
opened to 73..1020, and no band appears anywhere — the limit is out of reach. A
fraction of the line would have put one at 904, and a deficit measured relative
to the line end would have put a sliver at 1633. Neither appears. **X is a
position, not a proportion and not a distance back from the capture stop**, and
that is what makes reducing the divider the fix rather than a workaround.

**The sharper confirmation has not been run.** At `PLLAD_MD` 2400 a count
predicts a band of exactly 75 units in a 1200-unit line. "No band" is also what a
broken capture path gives; a thin band appearing where predicted cannot be an
accident.

**A band seen at 2048 was cyan rather than green**, after the clamp was scaled
for that divider. Either it is a different band — the capture reaching into the
porch, coloured by the black reference — or the substituted value is not constant
and "writes zeros" is too strong. Unresolved, and the arithmetic above is the
only evidence for the zeros.

Changing the divider by hand needs four fields the tooling does not scale.
`set_pllad_scaled()` moves `IF_HSYNC_RST`, `IF_LINE_ST`/`SP`, the capture window
and `SP_RT_HS_SP`. It leaves behind `SP_H_CST_ST`/`SP`, which costs sync, and
`SP_CS_CLP_ST`/`SP`, which costs colour: the line's duration is fixed by the
source, so a shorter divider makes each sample longer and slides the clamp window
later in real time, out of the sync tip and into the back porch.

## What it costs, in modes

X bounds the capture at IF 1125, so the usable fraction of any line is
`2250 / PLLAD_MD`. Across the 28 stock AKF50 modes, four are bypassed on line
count and five more are scaled but cannot be captured whole:

| mode | htotal | VTOTAL | usable |
|---|---|---|---|
| 1056x250 / 1056x256 | 1536 | 312 | 73% |
| 1280x480 | 1600 | 525 | 70% |
| 1280x480 | 1664 | 520 | 67% |
| 1280x480 | 1680 | 500 | 66% |

Every one of those is an outlier geometry, and the ordinary modes clear the bound
with about 9% to spare — the tightest is htotal 1024. So X is not currently costing
a mode anyone wants.
[`../capture-limits.md`](../capture-limits.md) has both bounds, the trade against
`PLLAD_MD`, and the full mode audit.

## The limit is settled — do not reopen it

There is a write limit. It is a fixed count of ADC samples, so `PLLAD_MD`
decides whether a line reaches it, and the capture path writes dark green past
it. **Reducing the divider is the fix**, and it is in the firmware.

Two readings have been proposed against this and neither survives the bench:
that the band is a proportion of the line, and that there is no limit at all and
the write origin is wrong by ~50 px. The `PLLAD_MD` 2048 test refutes both — the
whole line fits inside the limit and no band appears anywhere, where either
model predicts one. Re-deriving a doubt from window creeps taken at a single
capture stop is not new evidence; a creep at a different stop would be.

## What the engine does about it

Two changes, and they compose: the divider does the work and the clamp is the
backstop.

`SourceMeasurement::recommendedDivider()` caps `PLLAD_MD` so `ifLineFor()` stays at or
below `InputLine::WriteLimitUnits` — a divider of 2250, since the IF halves it.
That is a **second** ceiling alongside the ADC's 162 MSPS rating, with its own
justification, and the tighter of the two binds. `InputLine::lastCapture()` then clamps the far end of
the window at the limit, for the lines the divider did not choose — `adopt()`
takes whatever a custom preset or a bypass switch left behind.

**The constant is what to be careful of.** X is one board's number, measured
once, and a source whose active picture legitimately extends further would be
cropped by it silently. The head guard is derived —
`ceil(units × HLOW_LEN / PLLAD_MD)`, recomputed per solve — and the far end
cannot be, until what counts to 2250 is known. Capping the divider is what keeps
the clamp off real picture: with the line inside the limit it never fires.

The cost is sampling density. At the bench source the divider goes 2548 -> 2250,
2.49 -> 2.20 IF units per source pixel, against a Nyquist floor of 2.00 for its
512 px line. [`../capture-limits.md`](../capture-limits.md) has the trade.

## Two green bands, and only one of them is X

A green band down the right has two causes that look alike on screen and are
told apart by whether it moves with the ZOOM.

| | the write limit | the stride |
|---|---|---|
| cause | the capture path stops writing video past IF 1125 | `PB_CAP_OFFSET` below `PB_FETCH_NUM`, so lines overlap and each overwrites its predecessor's tail |
| when | the capture window reaches past X | the capture is wide enough that the fetch passes the stride |
| with the zoom | fixed in the line, so zooming out walks the window INTO it | appears as the picture zooms OUT, because the fetch follows the capture |
| fix | cap the divider, and clamp `lastCapture()` | size the stride from the whole line |

The stride one is the trap, because the *fetch* is derived and the stride was a
constant: at rest the fetch sits well under it and the picture is clean, and the
band only arrives once the zoom has widened the capture far enough. On the bench
that was a capture of about 920 units. `Memory::offsetFor()` now takes the line,
so the stride covers a capture of the whole of it and no framing can outgrow it.

**Neither is `PB_CAP_OFFSET` 230 removing the green**, which is the refuted
claim in the table above: 230 is below the fetch at every framing this board
runs, so it *causes* the second band rather than curing the first.

## The garbage past the picture is a different artefact

Right of the picture there can also be structured junk and a repeat of the line.
That is the display window extending past what the scaler produced: playback
keeps fetching, so the region shows stale buffer from earlier framings, and when
the fetch is exhausted the output stage re-emits. It is separated from the green
by cause and by fix — `Axis::solve()` sets the display window to
`origin + produced`, and the memory window with it.

Extending the capture window into the source's blanking is a way to *mask* it:
the IF writes blanking data into the buffer, so playback reads written green
rather than stale memory. That works and costs capture width, a dependency on the
source having porch to reach, and a green field instead of a black one.

## See also

- [`../scaler-geometry-model.md`](../scaler-geometry-model.md) — the arithmetic
  the capture window is solved from
- [`output-front-porch.md`](output-front-porch.md) — the far end of the output
  line
- [`playback-fetch-and-stride.md`](playback-fetch-and-stride.md) — the buffer
  stage the stride band lives in
