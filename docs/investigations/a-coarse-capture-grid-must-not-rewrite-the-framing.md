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

## Anchored to the line instead

The framing is a proportion of `units()` now -- the whole source line, 1100
against 1881 horizontally and 624 against 312 vertically, which is the same
span of source in either scan mode. `defaultWidth()` was already anchored
there, so the tuned path and the untuned one agree.

The reachable window is a subset of what a framing can name, so the bound
reaches the CONTROLS: each gives back at it in its own quantity, a pan stopping
where it is and a zoom stopping widening. Nothing in the placement can decide
that, because it does not know which control ran.

Measured with one framing, 1080p -> 480p -> 576p -> 1080p:

| | capture window, as a fraction of the line |
|---|---|
| 1080p | 0.2016..0.8265 horizontally, 0.1010..0.9231 vertically |
| 480p, 576p | 0.2015..0.8262, 0.1026..0.9231 |

The horizontal agrees to 0.0003 where it was 0.015 adrift. The vertical start
differs by 0.0016, half a source line, which is what a whole-line grid can
express.

The stored numbers changed meaning with it -- `framing.txt` and the slots are
ten-thousandths of the LINE -- and no migration was written, because nothing
has shipped.

## What is still open, and it is not the capture

With the capture matching, the PICTURE at 480p and 576p is still about 1.3x
the magnification of 1080p's: photographed at one framing and fitted for scale,
480p lands at 0.760 of the 1080p frame horizontally and 576p at 0.715. The
capture is the same part of the source in all three, so this is output-side --
the same class as the active window being a fraction of the line rather than a
porch time, and unresolved for these two modes.

What decides it is what the television reports it is receiving at each, which
is the datum that settled the 1024p case.
