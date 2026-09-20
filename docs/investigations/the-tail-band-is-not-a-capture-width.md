# The tail band is not a capture width, and it is not written blanking

**What it is instead is settled: the VDS's one-line delay, `VDS_D_RAM_BYPS`.**
[`the-tail-green-is-the-vds-line-filter.md`](the-tail-green-is-the-vds-line-filter.md)
That is why nothing here fits a capture-domain constant -- the boundary is
imposed by a stage downstream of the capture, which is also why it survives the
picture being destroyed.

The green band at the end of the line was carried as two constants: a capture
WIDTH of 1024 IF units measured from wherever the window opens
(`VideoSourceLine::CaptureWidthLimitUnits`), and a write POSITION of 1125 IF
units (`WriteLimitUnits`). The width is what drags the measured hsync duty into
the divider choice, because a window that must open after the sync pulse has
less of the line left to span.

Measured on the bench RiscPC at 320x256@50 line-doubled, and at 800x600@60
scaled undoubled with `preferScalingRgbhv` on. **Both constants are wrong, and
so is the colour arithmetic.**

## Measuring it at all needs the window FILLED

An underfilled display window shows whatever the playback stage fetches past the
end of the produced picture, and on this bench that is green-tinted repeated
card. It is not the band. A first attempt measured it for two hours and
concluded the bound moved with the window start.

So every frame below has `VDS_HSCALE` re-fitted at each step to
`(stop - start) x 1024 / 1789`, which fills the display window exactly, and the
artefact is read as the count of columns where `G - max(R, B)` clears 25.

## It is a position, not a width

At divider 2600 with the window opening at 129, walking the window's stop down:

| `IF_HB_ST2` | 1298 | 1260 | 1230 | 1210 | 1200 | 1190 | 1180 |
|---|---|---|---|---|---|---|---|
| band, columns | 132 | 90 | 58 | 34 | 18 | **0** | 0 |

A linear fit puts the onset at **IF 1187**, not 1125. Then the window is
narrowed from the LEFT instead: start 329, stop 1298, a span of **969 units** --
comfortably inside any width bound of 1024 or the ~1034 the width was measured
at -- and the band is still there, at 188 columns. A width bound forbids it
entirely.

**So `capture-limits.md`'s "X is not a position" is refuted**, and the premise
`framableIfLine()` inverts does not hold.

## The position is not the same number in the two scan modes

Doubled, the whole line arrives up to divider 2400 and the band appears at 2450:

| divider | 2200 | 2250 | 2300 | 2350 | 2400 | 2450 | 2500 | 2548 | 2600 |
|---|---|---|---|---|---|---|---|---|---|
| IF line | 1100 | 1125 | 1150 | 1175 | 1200 | 1225 | 1250 | 1274 | 1300 |
| band, columns | 0 | 0 | 0 | 0 | 0 | 54 | 82 | 108 | 132 |

Undoubled at 800x600, where one IF unit is one ADC sample rather than two:

| divider | 1150 | 1200 | 1250 | 1300 | 1400 | 1500 | 1700 | 1900 | 2000 |
|---|---|---|---|---|---|---|---|---|---|
| band, columns | 0 | 0 | 0 | 0 | 0 | 0 | 56 | 214 | 270 |

**Neither unit is constant across the two.** Doubled the onset is about 1190 IF
units, which is 2380 ADC samples; undoubled it is between 1500 and 1700 units,
which is the same again in ADC samples. A bound fixed in IF units predicts the
undoubled band from 1190; a bound fixed in ADC samples predicts it from 2380,
which `IF_HSYNC_RST`'s eleven bits cannot reach. Both are refuted by the same
table.

Within EITHER source the band grows linearly with the line, so it is a fixed
position in that source -- a fixed FRACTION of the line would hold the band at a
constant width and it does not.

## It is not written blanking, and the picture inside it survives

`tail-green.md` decodes the colour from `Y=U=V=0`, which gives a flat
RGB(0,135,0). Measured inside the band against the picture beside it:

| | mean R | mean G | mean B | max G |
|---|---|---|---|---|
| inside the band | 5.5 | 66.6 | 22.3 | 181 |
| the picture beside it | 169.6 | 169.9 | 195.0 | 255 |

**The band is dark where the picture is dark.** Under `Y=U=V=0` nothing inside
it could fall below G 135, and under `U=V=0` with luma preserved the same floor
applies. What is on screen is the picture with **red and blue lost and green
kept** -- the card's structure is still legible in green, blacks are still
black. That page flags its own weakness: *the zeros are inferred from the
colour, not observed*. They are not there.

## The boundary survives the picture being destroyed

At divider 2000 undoubled the ADC PLL does not lock and the picture shreds into
torn stripes. The band's left edge stays a **straight vertical line at a fixed
column, full height**, while everything to its left tears. Content read out of
memory carries the corruption; a boundary that does not is imposed after
whatever holds that content.

## What does not move it

| | band |
|---|---|
| `SP_RT_HS_SP` 2000, 2200, 2418, 2560 -- 1000..1280 IF units, straddling the onset | 178 columns, unchanged |
| `IF_HBIN_SP` 2, 200, 600, 1000 | 270, 268, 270, 268 columns |
| every wide field in `tv5725_registers.json` read and searched for a value within 40 of 1190 IF or 2380 ADC | one hit, `STATUS_SYNC_PROC_HLOW_LEN`, a status register reading the pulse complement |

So no register the firmware writes holds the boundary's position, and the two
fields most likely to collide with the window's end do not own it.

## What this costs, and what the divider should be made of

The bench source runs **clean at divider 2400** where the engine picks 2206 --
nine percent of horizontal sampling given away by a constant that is too small
by about 65 units and has the wrong shape besides.

The duty enters the divider only through the width model, and that model is
refuted, so `framableIfLine()` has no measured basis. What a replacement needs
is the onset measured per scan mode, which this page does not supply: the
doubled figure is bracketed at 1200..1225 IF units and the undoubled at
1500..1700, both on one source each.

**The original firmware computes none of this.** The twelve deleted preset
tables carry a hand-picked divider apiece -- 2269, 2341, 2345, 2553, 2558,
2559 -- and every one is above 2250, so every stock preset ran past the bound
that was believed to exist. Pass-through does not use the duty either:
`HdBypass::dividerFor()` is the line rate against the channel counter.
