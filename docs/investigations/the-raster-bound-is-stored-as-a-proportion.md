# The raster's bound was stored as a proportion, so it bit again on every shorter line

640x480@75 reached by a mode change came up capturing 436..1031 of a 1223-unit
line -- 49% where DMT states 76.2% -- with the test card cropped to its middle
and its green border off all four edges. A cold boot into the same source
captured 268..1200 and was whole.

## What it is not

**Not the default placement.** `CaptureWindow::place()` takes the mode's
published active region when `SourceTiming` has one, and it is right to four
decimals on every mode measured: 800x600@60 lands on 0.2048/0.7576 against DMT's
20.5%/75.8%, 800x600@56 on 0.1951/0.7810 against 19.5%/78.1%, and 640x480@75 on
0.2191/0.7621 against 21.9%/76.2%. So this is not
`vesa-modes-are-clipped-by-default.md`, whose centred default is superseded.

**Not a framing loaded from flash.** `/framing.txt` carries no entry for the
500-line 75 Hz source, but the file is not the table: the table lives in RAM, the
flash write is debounced, and `/sc?B` calls `forget()` before anything is read.
The decisive test is a restart, which is the one moment RAM and the file are
identical -- and the first acquisition after one is correct.

**Not the encoder-ceiling fallback.** The same fallback to 1024p runs on the boot
path, which is the leg that works.

## The mechanism

`VideoPath::narrowToRaster()` caps the capture at what the output raster can
show, and expresses the cap as a fraction of the line:

    framing.narrowTo(axis, (float)most / (float)whole);

`most` is `OutputWindow::widestCapture()`, a count of capture units. `whole` is
the line in force. But `PanAndZoom` is **a proportion of the capturable region
and nothing else** -- that is what makes it both the live state and the stored
state -- so it outlives the divider that set `whole`.

`calculateInputFormatterRegisters()` then adopted the capped framing back into
the engine's own state, and `PanAndZoom::narrowTo()` only ever shrinks. So the
cap became the user's framing, and the same cap returned as a tighter crop every
time the line got shorter.

Measured, on the mode change that reproduces it:

| | line | framing | capture | the bound allows |
|---|---|---|---|---|
| solve while the divider was 2046 | 2047 | 998/2047 = **0.4866** | 998 | 998 |
| after the divider settled at 1222 | 1223 | 0.4866 still | **595** | 998 |

A cold boot never sees the 2047 line, so the framing is seeded once against the
line it will keep.

## The fix

The seeding is adopted and the bound is not. An axis nobody has framed still
takes the mode's default and becomes the framing, so a press starts from where
the picture is; the bound is applied to the solve in hand and re-derived on the
next one, so dropping it loses nothing.

Bench, after: `800x600@60 -> 640x480@75` lands on 0.2189/0.7621, capture
268..1200 of 1223, byte-identical to the cold-boot result, card whole with its
green border on all four edges.

## The reporters answer the ASK, not the applied capture

`VideoPath::originUnitsOn()` and `extentUnitsOn()` compute from the framing, not
from the window written to the chip, and `test_video_path.cpp` asserts exactly
that. Before the fix the two agreed, because the applied bound had been written
into the framing; after it they legitimately differ wherever the raster binds --
`/geometry`'s `oh`/`eh` are what the framing asked for, and `geometry.py` reads
`IF_HB_ST2`/`IF_HB_SP2`, which are what the chip got. **Reading one as the other
is how the clipping looked self-consistent.**

`IF_HB_*` are BLANKING, so the captured span is `IF_HB_ST2 - IF_HB_SP2`.

## Left behind, and it is what made this fire

A mode change into a resolution the encoder cannot transmit installs one extra
divider before it settles: `VideoPath::dividerCeilingForOutput()` solves the
raster for the mode currently held, and since
`the-encoder-ceiling-is-the-raster-floor.md` that solve is refused for a mode
above the encoder's ceiling -- so the ceiling comes back 0, which
`SamplingClock::recommendedDivider()` reads as no bound at all, and the pass
lands on 2046 before the fallback moves the mode and the next pass lands on
1222.

It is now harmless, and it is what put a solve on the 2047-unit line. The mode
that will run is `OutputMode::transmittableFor(mode_, rate)`, which the ceiling
could ask for; whether it should is a question about who owns that choice, the
acquisition layer holding it today. Not changed here, and not measured as
costing anything beyond a second ADC PLL latch per such mode change.
