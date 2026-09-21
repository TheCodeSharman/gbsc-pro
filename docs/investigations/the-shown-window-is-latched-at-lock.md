# The window the picture is shown through is latched at lock, not re-rolled

**The window is held DOWNSTREAM, and the overlay measurement says so.** The
STV9426 menu is generated on the board from `HS_OUT`/`VS_OUT` and keyed into the
video at U13, so it rides the analog frame with the picture. Across a lock taken
with `VDS_DIS_HB_SP` moved 300 -> 425, with the board frozen and no sync register
touched, **the overlay itself moved +124 photo columns on the panel and its
visible box was clipped from 934 columns to 666**. Nothing on the board can move
the overlay, so the analog-frame-to-panel mapping changed.

**It is a RE-ACQUISITION LOCK, not a re-roll.** The sink establishes the window
when it acquires, and holds it until it acquires again.

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
`the-picture-position-is-latched-not-re-rolled.md`.

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

## The shown window has an origin of its own, and it is latched

Found by creeping `VDS_DIS_HB_SP` and watching the panel's left edge: below some
value nothing changes, above it the edge moves linearly at a fixed number of
photo columns per output unit. The knee is where the shown window opens.

| raster | knee | slope | our window |
|---|---|---|---|
| 1790 | **420.8** | 1.200 cols/unit | 425..1694 |
| 1440 | **376** | 1.5 cols/unit | 402..1423 |

Sweeping `VDS_DIS_HB_ST` the same way at 1790 puts the far end at **1572.5**, so
the window is `300.3..1572.5` after the latch below -- 1272 units of a 1790 line,
**0.7107**, against `Mode960p`'s `carriedPx / totalPx` of `1280/1800 = 0.7111`.
**`carriedPx` is therefore right**, and a reading of 0.682 taken from a guessed
panel width is not.

### It follows our blanking across a lock

Frozen, at 1790, with `VDS_DIS_HB_SP` set to **300** and `PAD_SYNC_OUT_ENZ`
toggled 1 then 0: the knee moves from 420.8 to **300.3**, following the register
to a third of a unit. So two landings that differ are two different blanking
values in force at the lock, **not two draws** -- which is what refutes the
re-roll. `the-picture-position-is-latched-not-re-rolled.md`.

### It can be pulled earlier but not pushed later

The same toggle at 1440 with `VDS_DIS_HB_SP` at 402 -- LATER than the 376
already latched -- left the knee at 376 and the picture byte-identical, left
edge and right edge both unmoved. Black inside the window already held is no
cue; content earlier than it is.

**So a window that opens LATER than the latched one leaves a bar nothing
downstream will correct, and one that opens earlier costs nothing.** Bias the
solved origin early.

### The board-side reading is refuted twice over

Opening the window to 350 at 1790 revealed **nothing**, so there is no
playback-fetch content sitting below the knee to be uncovered; yet after a lock
the same register at 320 puts content on screen with the board frozen and every
geometry register unchanged. And the overlay moves with it, which no board-side
mechanism can do.

**What the sink reports confirms the mapping.** Its own menu names the incoming
mode as `1280 x 960/60Hz` off a 1790 x 1000 raster -- `Mode960p`'s
`activePx` exactly -- so our line is resampled into the standard's active pixel
count, which is why `carriedPx / totalPx` is the right shape for the width.
