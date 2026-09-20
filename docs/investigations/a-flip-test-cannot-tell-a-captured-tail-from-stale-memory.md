# A flip test cannot tell a captured tail from stale memory, and the landing decides whether either shows

The bright vertical strip at the right-hand end of the picture on the bench
RiscPC at 320x256@50 is captured content -- the tail of the source line past
where its picture stops, which
[the-bar-at-the-right-edge-is-captured-line-tail.md](the-bar-at-the-right-edge-is-captured-line-tail.md)
establishes.

Two things make it read as an unwritten memory window instead, and both are
answered here: a detector that cannot separate the two, and the byte-identical
register dumps of
[the-hbin-start-blanks-the-captured-tail.md](the-hbin-start-blanks-the-captured-tail.md),
which leave the carrying state outside the registers and so invite the memory as
the only candidate. **The painted extent is also outside the registers**, and it
accounts for the same observation.

Measured on `vga`, RiscPC at 320x256@50, `PATTERN PM5544`, forced 100% framing,
output raster 1920 x 1126, capture 101..1099 of a 1101-unit line.

## Putting it on the screen

    curl 'http://<ip>/sc?~'                    # re-detect
    sleep 45
    setfield.py --set PAD_SYNC_OUT_ENZ=0       # the pad latches down; raise it

Without raising the pad the screen is dark with every other register healthy,
which reads as the re-detect having failed -- `docs/known-issues.md`,
"`PAD_SYNC_OUT_ENZ` is found at 1 with everything else healthy". A source mode
round trip is the other reproduction, and lands it on 4 trips of 6.

## The lit extent is not the painted extent, and using it as one is circular

The rightmost lit column looks like a cheap measure of how much of the raster is
painted, and across an appearance and a disappearance it moves:

| | rightmost lit column | strip |
|---|---|---|
| landing A | 1442 | absent |
| landing B | **1566** | **present** |

**It moves because the strip is lit.** The strip occupies 1504..1566 with a dark
gap from 1442, so the metric is reading the artefact and cannot then be offered
as a reason the artefact showed. A measure of the painted area has to come from
something painted in both states -- the blanking level either side of the
picture, or a reference taken in pass-through, where the source fills the panel.

That the strip is present or absent at byte-identical registers is
[the-hbin-start-blanks-the-captured-tail.md](the-hbin-start-blanks-the-captured-tail.md)'s
finding, and the state carrying it is latched in the capture and scaling path.
Nothing here adds a second cause.

## The flip test is blind here

The card's border alternates cyan and magenta twice a second, which makes a
tempting detector: photograph several frames and call any lit column whose
colour does not follow the flip unwritten. **It does not separate the two
candidates**, because the captured tail is the source's front porch -- static
content, which does not follow the flip either.

Per-column phase difference over a four-second clip, landing B:

| columns | what is there | follows the flip |
|---|---|---|
| 63..1424 | the border | yes, difference 130..160 |
| 216..1271 | the card's own pattern | **no** |
| 1504..1599 | the strip, lit 112..185 | **no**, difference 2.6..4.7 |

The card is static by construction and scores exactly as the strip does, so a
detector reporting "lit but frozen" reports the card, the strip and a genuinely
unwritten region alike. **Frozen across N frames says nothing**, however large N
is.

## The strip is not the border, either end of the line

The colour is a close match for the border's cyan phase, and the capture stop at
1099 is two units from the line's wrap at 1101, which makes the next line's
leading border a plausible reading. It is wrong twice over: the strip does not
follow the flip, above, and `BORDER OFF` leaves it in place.

| col | `BORDER ON` | `BORDER OFF` |
|---|---|---|
| 1528 | (51, 154, 182) | (46, 142, 169) |
| 1544 | (52, 153, 183) | (48, 142, 169) |

The 7% fall is the whole frame dimming -- column 1440, outside the strip, goes
(50, 32, 57) to (39, 28, 43) across the same pair. Hue and position are
unchanged.

## What separates a captured tail from an exposed memory tail

**A zoom clips the strip away; an exposed tail would survive it.** Zoom moves the
capture stop and re-solves the scale so `produced` still fills the same display
window -- `VDS_DIS_HB_ST` 1854 throughout, `produced` 1714.5 at `VDS_HSCALE` 509
against 1714.7 at 596 -- so anything anchored to the end of that window cannot
move:

| capture | strip |
|---|---|
| 101..1099 | present |
| 174..1026 | **gone** |
| 112..1088 | present |

**A pan marches it; an exposed tail cannot move.** An unwritten region runs from
where the write ends to where the display window does, and both are functions of
the fetch, the scale and that window. **Record all three**: a pan that moved the
display window would march an exposed tail too, and a pan table without them
settles nothing.

Measured with `VDS_HSCALE` 585, `VDS_DIS_HB_SP` 140, `VDS_DIS_HB_ST` 1855 and
`PB_FETCH_NUM` 245 identical at every step:

| capture | picture's right edge | strip onset | strip right end |
|---|---|---|---|
| 101..1081 | ~1450 | 1536 | 1566 |
| 107..1087 | ~1440 | 1530 | 1566 |
| 113..1093 | ~1430 | 1524 | 1566 |
| 119..1099 | ~1420 | **1512** | 1566 |

The picture's edge and the strip's onset march together, 30 and 24 photo columns
as the window advances 18 units into the source. The strip's far end stays
pinned at the edge of the painted area, so it widens as its onset walks away
from it. An exposed tail would sit at fixed output pixels throughout.

A separate pan of 24 units at another framing confirms the output side holds
still under a pan at all: `VDS_HSCALE` 554, memory window 39..1855, display
window 140..1855 and `PB_FETCH_NUM` 232 constant across four steps.

## Bypassing both scalers shows what an exposed window actually looks like

`VDS_HSCALE_BYPS` and `VDS_VSCALE_BYPS` (s3_00 bits 4 and 5) drop the scale to
1:1, so `produced` collapses to the capture's 998 px inside a 1715 px window and
the exposed region becomes most of the screen instead of a sliver. Frozen, at
the bench framing, the picture occupies the top left and beyond it are two
different things -- and they behave oppositely:

| region | frames 1-3 | frames 4-5 | |
|---|---|---|---|
| picture border | (181, 63, 172) | (59, 175, 196) | live |
| first bar right of the picture | (141, 180, 238) | (78, 227, 244) | **live** |
| third bar | (122, 193, 228) | (96, 213, 232) | **live** |
| sixth bar | (86, 115, 160) | (50, 139, 165) | **live** |
| the field below the picture | (175, 164, 184) | (173, 164, 184) | **frozen** |

**Horizontally, running past `produced` repeats LIVE line data.** It arrives as a
row of alternating bars that follow the source, which is the repeat
`Memory::fetchFor()` predicts -- "a line one pixel short of its source still
fails to finish, and it repeats". Why the repeat is periodic bars rather than
one re-run of the line is not established.

**Vertically it shows frozen junk**, which is uninitialised memory and the only
place on this part where an unwritten region is actually visible.

That gives a third discriminator, independent of the pan and the zoom: **a
horizontal over-read follows the source, because it re-reads live line data.**
The strip does not follow the source, so it is not one.

## The contamination is anchored to the next line's pulse

Converting the strip's onset to source units, and then to a distance from the
next line's hsync pulse at 1101 -- the sync processor reads
`STATUS_SYNC_PROC_HSPOL` 1 and `HLOW_LEN` 156 ADC samples, so the pulse is at the
HEAD, 78 IF units wide, and `firstCapture()` already excludes it:

| capture | strip onset, source unit | units before the pulse |
|---|---|---|
| 101..1081 | 1058.3 | 42.7 |
| 107..1087 | 1059.8 | 41.2 |
| 113..1093 | 1061.3 | 39.7 |
| 119..1099 | 1058.3 | 42.7 |

So the tail is contaminated for about **41 to 43 units before the pulse**, and
the capture runs to `units - 2` -- two units short of it. Measured active extent
is **0.844** of the line against `AxisHorizontal`'s assumed 0.864.

**The head has a guard for the same thing and the tail has none.**
`DoubledHeadBlankingUnits` is 22 because a window opened 17.9 units past the
pulse takes the saturated colour blanking decodes to and 19.9 is clean;
`lastCapture()` returns `units - 2` and takes the whole approach.

A blanket subtraction of the pulse from the tail is already refuted --
`lastCapture()` carries the measurement, at 640x480@60, where the right-hand
border goes with it. That case is a line counted from the pulse's TRAILING edge,
where the pulse genuinely occupies the tail; this one is counted from the
leading edge, where what occupies the tail is the approach to the next pulse. A
tail guard has to distinguish the two.

## What it is not

**Not the playback fetch.** `PB_FETCH_NUM` is 250, which is
`ceil(998 / RequestsPerLine)` at `RequestsPerLine` 4, and it is correct:
photographed either side, 244 gives a black gap plus a detached strip and 250 is
clean. Raised to 275 it changes nothing.

**Not the output geometry falling short.** The picture covers 1715 px of a 1920
raster, and the 205 px it does not cover is `activeStart` 140 with `activeStop`
1855 -- the CEA-861 1080p sync and back porch converted to this raster's clock in
`OutputMode::solve()`, whose porches are what stop the landing being re-chosen on
every acquisition. `998 x 1024 / 596` = 1714.7 against a window of 1714.

**`IF_HB_ST2` written on its own does not reach it.** With automation frozen,
1099 -> 1079 -> 1059 -> 1039 -> 1019 leaves the strip at 52 photo columns
throughout, while an engine-solved change of the same stop removes it. A lone
write to it is not a test of anything.

## Two instruments that mislead

**A large `VDS_DIS_HB_ST` change re-lands the output.** Blanking to 1500 moves
the whole picture, so a photo-column mapping calibrated across such a step is
invalid. Steps to 1600 and above hold the landing still.

**"Rightmost flipping column" scores the broken state as good.** At
`PB_FETCH_NUM` 244 the detached strip flips, so a motion metric reads it as live
picture and ranks 244 above the clean 250. Only the photograph separates them.
