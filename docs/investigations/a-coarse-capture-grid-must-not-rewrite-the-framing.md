# A coarse capture grid must not rewrite the framing

An output too short for a doubled frame turns the line doubler off, and the
capture grid changes with it. On the bench source, 311 lines at 50 Hz:

| | vertical units | horizontal units |
|---|---|---|
| doubled (1080p, 1024p, 960p, 720p) | 621 half-lines | 998 |
| undoubled (576p, 480p) | 309 whole lines | 1673 |

The framing is a proportion, so it carries across a grid change correctly. What
it must not do is come back changed.

## What it cost

`ActiveImage::clampToLine()` wrote the placement back into the framing on every
solve, for a tuned axis as well as an untuned one. For a tuned axis that is a
read-back: it re-quantises the stored proportion onto whatever grid the current
line offers, and the autosave then persists the result as the user's framing.

Measured on the bench, one trip through a short output:

    1080p  ev 513        the framing as set
     480p  ev 255        621 -> 309 units, the best that grid can express
    1080p  ev 512        the fine grid can no longer say 513

One unit per excursion, and it accumulated: the framing walked away from what
the user set, by a step nothing in the picture explained.

## What holds now

A framing the line CAN realise is left exactly as set. `clampToLine()` seeds an
axis nobody has framed yet, and rewrites a framed one only where a bound moved
the placement off what the proportion asked for.

In practice that bound is `MinimumCapture`, the 16-unit floor: if the grid
coarsens until the extent lands below it the capture is raised, and the framing
has to follow or the zoom control is stuck below its own floor with no press
back. The width and start clamps cannot fire for a tuned axis, because
`PanAndZoom` already holds the origin and extent inside [0, 1].

Measured after, eight excursions across 576p, 480p, 720p and 1080p:
`poh 1232 peh 6884 pov 998 pev 8261` at every one, and every return to a
doubled output gives back `ev 513` exactly.

## What this does NOT fix

The framing is a proportion of `capturable()`, and that region is not the same
part of the source line in the two scan modes -- the head exclusion is
`DoubledHeadBlankingUnits` on a doubled line and `CaptureLagUnits` on an
undoubled one, and the sync pulse is counted in units of different size:

    doubled     22 + 79  = 101 of 1100 units    9.2% .. 99.8% of the line
    undoubled   72 + 134 = 206 of 1881 units   11.0% .. 99.9% of the line

So one proportion names a slightly different part of the picture in each mode.
Photographed at one framing and fitted for scale and shift against the 1080p
frame, 480p comes out at 0.980 horizontally and 576p at 0.962 -- enough, at a
framing set tight to the panel, to bring the source's own border on screen at
one edge and lose a column at the other.

Anchoring the framing to `units()`, the whole source line, would remove it:
1100 against 1881 horizontally and 624 against 312 vertically are the same span
of source in both scan modes. It changes what every stored record means, so it
needs the file versioned and old records converted on load.
