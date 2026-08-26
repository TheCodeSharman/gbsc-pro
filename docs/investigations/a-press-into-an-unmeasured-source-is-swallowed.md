# A pad press into an unmeasured source is accepted and moves nothing

A zoom or pan press made while the sync processor is not counting the source
returns 200, is consumed by `loop()`, is logged as applied -- and changes no
register and no framing. Nothing outside the firmware distinguishes it from a
press that worked: the registers read the same, `/geometry` reads the same, and
the source is locked again by the time anything can be checked.

## What the firmware says

The console is the only instrument that separates the two. The same press, taken
in each state:

    working   ADJ horizontalZoom +8px -> framing h 54+969/1044 v 38+582/622  IF_HB 134..1103  IF_VB 38..620  HSCALE 555 VSCALE 533
    swallowed ADJ horizontalZoom +8px -> framing h  0+0  /1044 v  0+0  /622  IF_HB 132..1105  IF_VB   2..0   HSCALE 557 VSCALE 533

The framing is `0+0` on both axes, which is what `PanAndZoom::reset()` leaves,
and the vertical capture window is empty at `2..0`. `IF_HB`, `HSCALE` and
`VSCALE` are the values that were already there.

## Why the press does nothing

An axis nobody has framed carries no proportion of its own, so
`ActiveImage::zoomBy()` seeds it before moving it:

    if (!framing_.tunedOn(axis))
        clampToLine(line, timing, axis, raster);
    framing_.zoomBy(axis, units, line.capturable());

`clampToLine()` returns without seeding when the line it is handed measures zero.
The zoom then applies to an extent of `0`, `PanAndZoom::clampOn()` holds it at
`0`, and the axis is still untuned -- `zoomBy()` does not set the tuned flag,
only `seedOn()` does. So `place()` keeps taking the default branch and returns
the window that was already programmed.

Every press made in that window is lost the same way, silently.

## How it reaches the suite

`recover_lock()` fires `/sc?~` and returns as soon as the source locks, and a
detection pass takes the source out for around five seconds on the way. A test
that starts in that window presses into an unmeasured source.

That is why the failure follows the order of a run rather than the test: it
passes alone and fails after a neighbour that re-detects. Measured on the bench,
`test_the_reset_control_returns_the_framing_to_default` run immediately after a
`/sc?~`: **4 of 8 runs failed**. Waiting for `locked_steadily()` first: **0 of
11**, one of which skipped because the source had not come back inside 40 s.

## Two things that do not work, both measured

**A longer press budget does not help.** The press is not delayed, it is
swallowed -- raising the budget from 6 s to 15 s left it failing at 15 s with the
same message.

**Register-read latency is not a witness that `loop()` is free.** Through a
detection pass, reads are served at an unchanged 34 ms median with occasional
multi-second spikes, so a read returning promptly says nothing.

**And reading the framing back does not distinguish the two states**, which is
why waiting for two agreeing framing reads before the press was tried and did not
fix it. `/geometry` reports the same numbers either way.

The only thing that separates them from outside is whether the sync processor is
counting, which is `locked_steadily()`.

## Open

Whether the firmware should refuse a press it cannot honour rather than logging
one it did not make. A press that reports success and does nothing is
indistinguishable from a working control at every layer above the console, and a
user walking a picture into place with a remote has no way to tell which presses
landed.
