# A transition and a settled toggle place the sink's window alike

The sink's window is placed from the raster and from what the line carries
at our aperture's edge when the sync pad returns, and not from when it
returns. Measured on the two 1080p rasters where a transition had been read
as landing short: the pad toggled by hand on the settled state, with the same
picture at the aperture's edge, lands where the transition did.

| 1080p, aperture at 160 | transition | settled toggle, twice | black panned to the edge |
|---|---|---|---|
| 640x480@60, T 1600 | 151.8 | 151.9, 151.9 | 158.3 |
| 800x600@56, T 1706 | 153.0 | 153.5, 153.5 | 158.3 |

Per state: the default framing acquired, automation frozen, the window read
off the dongle by walking the aperture; `PAD_SYNC_OUT_ENZ` away for 1.5 s and
back, twice, read after each; then the capture panned 20 IF units earlier so
the source's own black is at the edge and toggled once more. The transition
readings repeat the six earlier arrivals of 640x480@60 by mode change, 152.3
to 153.7. The black-at-edge control repeats the sink's own position for the
raster, 158.5.

## What it refutes

`the-sinks-own-window-position-is-per-raster.md` read ten transitions as
landing 5 to 9 units before the sink's own position "which a settled toggle
never does". The toggles that supported it were taken with black panned to
the edge and the transitions with picture there: two states, not two
placements. Against the aperture rather than the own position, six of the
ten were on the aperture within 2.3 units, one was on its own content, and
four -- 640x480@60, 1280x480@60, 800x600@56, 1600x600@56 -- were 6.6 to 10
units short. Those four are short settled as well. What they share is
picture at the edge on a raster whose own position sits 1.5 units before the
aperture; on rasters whose own position is after the aperture, picture at
the edge lands the window within a unit of it. What the sink keys on in the
short case is not established.

So the placement is deterministic per state, and a source mode round trip
does not re-place it because the round trip never toggles the pad.

## What the pad's early return did do

The console showed the pad returning from the flip to the acquired state,
0.2 s after the solve, ahead of the sketch's rate match, the sampling phase
and a second sampling pass -- and 500 ms later the sketch's rate-change check
re-running the whole preset path. On 240x352@70 -> 640x480@60 the first pass
installed divider 1548 against the 1024p raster the previous source had
fallen back to, because the output for the arriving rate was chosen after
the divider and the re-install was suppressed as the same rate; the sketch's
second pass installed 1444. None of that moved the sink's window, but it was
two owners of one mode change and a divider chosen against the wrong raster.

The sequence is now: aperture and pad away on the arm; rate measured; output
chosen for it; divider installed once against that output's raster; duty;
raster and windows; the caller's rate match; then, a pass later and once the
pad has been away at least 300 ms, the divider has latched and the phase has
been searched, both back. Measured across five changes, one divider install
each -- 1444 straight away on the fallback leg -- one duty, one rate match,
the phase, then the pad, and no second pass.

## What it costs

Off the dongle, from the MODE command to the picture back:

| change | pad away | dark |
|---|---|---|
| 320x256@50 -> 640x480@60 | 0.69 s | 4.06 s |
| 640x480@60 -> 320x256@50 | 1.78 s | 8.06 s |
| 320x256@50 -> 240x352@70 | 0.72 s | 3.68 s |
| 240x352@70 -> 640x480@60 | 0.71 s | 3.93 s |
| 640x480@60 -> 320x256@50 | 1.09 s | 5.04 s |

The dark period is the dongle's re-acquisition after the pad returns, which
quantises the way the television's does; the pad's share is under a fifth of
it. The 640x480@60 -> 320x256@50 leg measured 8.0 s on the television before
the change (`the-transition-is-mostly-the-encoder.md`), and the source's own
transient through that change -- the count passing 235 lines -- is what
holds the pad away longest. `tools/gbsc-pro-hwtest/dark_time.py` is the
instrument.
