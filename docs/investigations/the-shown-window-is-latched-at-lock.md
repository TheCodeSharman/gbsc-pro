# The encoder's active window does not follow our blanking edge

`OutputMode::solve()` carried the claim that the encoder "measures the pulse and
finds active video where our blanking ends", and used it to justify stating both
the sync pulse and the back porch as the standard's DURATIONS. Half of it is
measured true and half is refuted, and the refuted half is what a whole class of
framing faults rested on.

## What was measured

Bench unit on `vga`, separate sync, scaling path, automation FROZEN so nothing
re-solved underneath the experiment. Every comparison is between frames taken
inside ONE acquisition, which is what makes them comparable at all -- the
picture's position is re-rolled between acquisitions, and a pair taken either
side of one reports that displacement instead of the effect under test.
`the-picture-position-is-re-rolled-by-the-sync-pad.md`.

Frames are compared by a row-averaged luma profile over the middle half of the
panel, correlated for the best horizontal shift.

### The blanking edge does not move the picture -- REFUTED

At 640x480@75, `VDS_DIS_HB_SP` swept over 126 units:

| `VDS_DIS_HB_SP` | shift of the right-hand 55% | columns changed |
|---|---|---|
| 340 vs 300 | 0 px | **none** |
| 380 vs 300 | 0 px | **none** |
| 410 vs 300 | 0 px | 50..76 |
| 426 vs 300 | 0 px | 50..148 |

`r = 1.0000` at zero shift throughout. Had the encoder re-found active video
where our blanking ends, moving that edge 126 units would have moved or
rescaled the picture. It painted black inside a window that did not move.

**So the encoder holds a fixed active window relative to SYNC, and our display
blanking only paints black inside it.** Where our window is narrower than the
encoder's, we hide our own picture.

### The sync pulse is load-bearing -- SUPPORTED

Same unit at 800x600@60, a clean full-screen picture, `VDS_HS_SP` alone changed:

| `VDS_HS_SP` | panel mean | sd | |
|---|---|---|---|
| 112 | 149.6 | 60.6 | picture |
| 90 | 31.1 | 13.1 | no signal |
| 140 | 30.9 | 13.0 | no signal |
| 60 | 31.4 | 13.0 | no signal |
| 112 restored | 150.3 | 60.5 | identical, `r = 1.0000` |

A fifth either way drops the link outright, and restoring the standard's width
brings it straight back. The pulse is therefore the one horizontal quantity that
must arrive as the standard states it.

**What this does NOT establish is whether the encoder wants the pulse as a time
or as a fraction of the line.** One raster cannot separate them, because at a
single total the two differ only by a constant. Every value tried here was at
the same 1790-unit line.

## What follows for the solve

The two results point opposite ways, and taking both is what the code now does:

- the **sync pulse** stays a duration, because changing it costs the link;
- the **back porch** is what gives way when the line is too short to hold the
  porch and the active span together, because nothing downstream reads it.

The span is the picture. Taking the shortfall out of it throws picture away and
makes one source reach the panel differently at different field rates, which is
the fault this replaces: `../known-issues.md`, the entry on 70.9 Hz.

## The trap this closes

**"It is the encoder" is cheap to refute and was carried for a session.** Freeze,
move one output register, and ask whether the picture moved as a whole or a
strip of it was blanked. A row-averaged profile of two photographs answers it in
one command, and it does not need a second display, a probe or a bench trip.
