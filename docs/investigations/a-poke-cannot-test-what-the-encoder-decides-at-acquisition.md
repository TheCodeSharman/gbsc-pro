# A poke cannot test what the encoder decides at acquisition

**The column is fixed, and the cause was on the INPUT side**: the blanking the
capture path writes past the hsync pulse on a doubled line, taken into the
window because `VideoSourceLine::DoubledHeadBlankingUnits` was 17 where the
bench needs 20. Crept with the display window held still and confirmed after a
flash, so the encoder acquired the raster from cold — 0.0 against 27.0 before,
and no picture given up, because the guard takes the source's own blanking.

This page is kept for the route that did not work, which cost most of the
session: **every output-side reading below was taken through a sampling window
the encoder had fixed at an earlier acquisition**, and they were monotonic,
repeatable and mutually consistent all the way to a firmware change that did
nothing.

## What the panel test did establish

`VDS_DIS_HB_SP` 140 -> 220 blanks the column: photo column 52 goes from
R67 G70 B71 to R55 G42 B55, while the panel's own edge does not move. So the
column is inside the display window, at its near edge, and the panel is ruled
out. That still stands, because it does not depend on what the encoder acquired.

## The refuted model

By poking registers on a frozen engine, the column appeared to follow the
distance from the output hsync pulse's start to the first written pixel — the
sync width and the back porch together. It looked clean at 136 output pixels and
present at 138, with the baseline repeating six times at 23.1..25.6 on a metric
where under 8 is clean. Neither the pulse's width nor its position mattered on
its own; only the sum.

It was convincing. It was also wrong.

## What killed it

The rule was built into `OutputMode::solve()` — the sync pulse giving up four
pixels so the sum came to 136, the back porch untouched at CEA's 108 — and
flashed, so the encoder acquired that raster from cold. **The column came back
unchanged**: 27.0 and 26.8 against 26.0 and 26.3 before, where the same
registers reached by poking had measured 3.7.

Poking from there settles it:

| encoder acquired | poked to | leading blanking | green | |
|---|---|---|---|---|
| 136 | 136, its own | 136 | 27.0 | artefact |
| 136 | 140 | 140 | 30.2 | artefact |
| 136 | 132 | 132 | 3.5 | **clean** |
| 140 *(earlier run)* | 136 | 136 | 3.7 | **clean** |

136 is clean under one acquisition and dirty under another, and 140 is dirty
under both. What is clean is **whatever sits about four pixels below the value
the encoder acquired**, never a particular value. The poke moves the scaler's
content relative to a sampling window the encoder fixed at acquisition, so the
column leaves the sampled region. It is displaced, not cured.

## The rule this is an instance of

**A register poke cannot test anything the encoder decides at acquisition, and
the scaling path is full of those.** The porch work states it from the other
direction — a landing measured after a poke is measured on a timing that arrived
mid-flight.
[`the-picture-position-is-re-rolled-by-the-sync-pad.md`](the-picture-position-is-re-rolled-by-the-sync-pad.md)

What makes it expensive here is that the poke produced a *monotonic, repeatable,
well-separated* result — a clean fade from 24.9 to 0.3 with the baseline
repeating to +-1 — across four independent experiments that agreed with each
other. Internal consistency is no defence: every one of them was taken through
the same stale sampling window, so they agreed because they shared the fault.

**The test that distinguishes them is a build.** Where a claim is about
something the encoder settles at acquisition, the only instrument is a flash and
a cold re-acquire; a sweep of pokes measures the displacement and reports it as
a threshold.

## Why the output side looked like the cause at all

The column is a few CAPTURE units of green magnified onto the output, so how
many output pixels it covers scales with the magnification — and the divider cap
for the tail band had just raised it from 1.497 to 1.710 at this mode. That is
also how the guard came to be 3 units short without anyone noticing: the creep
that set 17 counted green columns at a framing where the residue was sub-pixel.

**Two attempts to link it to magnification directly failed** and are recorded so they are
not repeated:

- a frame from before the tail-band divider cap against one after gives 1.9
  against 26.0, but the two landed 28 photo columns apart with different amounts
  of captured blanking, so the measurement band does not cover the same region
  of each;
- sweeping the zoom moves the capture, so different source content arrives under
  the window's near edge at every step. The picture's edge jumped 100, 85, 69,
  232, 219 across one sweep and the readings went with it.

Holding the content still while only the magnification moves needs the divider
changed, which needs a build — as, now, does any other candidate.
