# The encoder places its window when the sync pad returns, from what is on the wire

The MS9288A fixes the start of the window it transmits at the moment
`PAD_SYNC_OUT_ENZ` is driven again after being taken away, and holds it until
the pad is taken away again. Where it puts it depends on what our line carries
at that moment: the first non-black content at the aperture's edge if that is
earlier than the sink's own position for the raster, else that position. The
width is the mode's fraction of the line whatever the start. A source `MODE`
round trip does not move it, because the engine never toggles the pad for one.

Measured on the bench RISC PC on `vga` into 1080p, automation frozen, the
window read by walking `VDS_DIS_HB_SP` and `VDS_DIS_HB_ST` into the picture
with `framing_sweep.py`'s own walks, the emitted frame read by the USB capture.

## A pad toggle re-places the window, and a source round trip does not

320x256@50, T 1916, aperture 160..1831, the window found at 172.2 after the
mode change. In raster units:

| lock | content at the aperture's edge | window start |
|---|---|---|
| source `MODE` round trip, default framing | captured black, card border at 181 | 172.2 |
| source `MODE` round trip, framing zoomed so picture reaches 159 | picture | 170.9 |
| source `MODE` round trip, default framing again | captured black | 172.2 |
| pad away 0.3 s, default framing | captured black | 171.6 |
| pad away 1.5 s | captured black | 171.8 |
| pad away 5 s | captured black | 172.2 |
| pad away 15 s | captured black | 172.0 |
| pad away 1.5 s, framing zoomed | picture | **158.9** |
| pad away 1.5 s, default framing | captured black | **171.0** |

The round trips leave it where it was, picture at the edge or not. The toggle
places it at the picture's edge when the picture is there and back at 171 when
it is not, so it is chosen afresh at every return of the pad and remembers
nothing.

The console during the sweep says the same from the other side: `sync pad:
away` and `sync pad: driven` are printed only on a source mode change, never
on a round trip, so every judged frame in a sweep carries the window the mode
change placed.

## The sink's own position is per raster

With black at the aperture's edge, the same raster lands in the same place on
different sources:

| raster | source | locks | window start |
|---|---|---|---|
| 1916 x 1125 @ 50.08 Hz | 320x256@50 | 3 round trips, 5 toggles | 171.0 .. 172.2 |
| 1600 x 1125 @ 60.00 Hz | 640x480@60, capture panned 20 units into the source's blanking | 2 toggles | 158.8, 158.2 |
| 1600 x 1125 @ 60.00 Hz | 320x480@60 | transition and 2 toggles | 158.0, 158.0, 158.0 |
| 1600 x 1125 @ 60.00 Hz | 1280x480@60, panned as above | transition and 2 toggles | 158.3, 158.6, 158.3 |

After the scaled sync and back porch, 140 units at both rasters, that is +18.3
at 1600 and +31.8 at 1916. `OutputMode::TransmittedWindowDelayPx` at 20 is the
first to within two units and twelve short of the second. Two rasters cannot
say what the sink derives it from; the measurement is one toggle with black at
the edge, so the rest of the set can be read the same way.

## The same rule read across the Tier C families

Seven AKF50 modes at 312 lines and 50.08 Hz share one output raster, T 1916.
In the sweep each arrives through a mode change, so each carries the window
that transition placed:

| source | at the aperture's edge when the pad returned | window start |
|---|---|---|
| 320x250, 320x256, 640x250, 640x256 | captured black, 17 to 22 units of it | 170.9 .. 171.7 |
| 768x288, 1056x250, 1056x256 | picture | 159.1 .. 160.0 |

The four with black show the sink's own position and fifteen black columns at
the right, where the window runs past the aperture; the three with picture are
pulled to the aperture and show none. The 158.9 recorded for 320x256@50 in
Tier B, on the same raster and aperture, was placed while a stored, cropped
framing put picture at the aperture's edge; today the default framing puts
captured black there and the same state reads 172.

## What the transition lock sees

`VideoSourceAcquisition` takes the pad away when the raster moves and
`serviceEncoderRelook()` returns it 300 ms later, but the acquired transition
drives it back first: the console shows `sync pad: driven` 0.18 to 0.42 s
after `sync pad: away` on every mode change of the sweep, before the frame
time lock's first rate match moves the display clock and before the sampling
phase is chosen. So the sink locks on a line the engine is still solving, and
the per-source placements in Tier A, B and C are what that line carried at
that instant. 640x480@60 read 153.0 at its transition and 158.2 to 158.8 at
two later toggles with black at the edge; 1280x480@60 read 150.0 in Tier C
and 158.3 in a later run, through different transitions.

One transition held the pad away for 15.3 s rather than 0.3: a rate-only arm
on 360x480@60.15, after which the release, which runs only on a detection
pass, found none for that long. The window then read 142.3, sixteen units
before the raster's own position, and a 15 s pad drop by hand on the settled
1916 raster did not reproduce a move of that kind. Open.

## What follows

- The pad must return once the output is settled, not on a delay, so the sink
  locks on the picture rather than on a solve in progress.
- With the aperture placed AT the sink's own position for the raster, every
  source frames flush: content cannot be earlier than the aperture, so the
  sink's rule lands the window on the aperture whatever the source carries at
  its edge. That position is a property of the output raster, measurable by
  one pad toggle with black at the edge, and is not the constant the model
  carries.
- A source `MODE` round trip is still the re-acquisition of the CAPTURE, which
  is what a hand-set capture register needs judging after; it is not a re-lock
  of the sink, and a measurement of where the sink's window sits needs a pad
  toggle instead.

`tools/gbsc-pro-hwtest/sweeps/framing-20260930T074949Z.jsonl.gz` is the Tier C
record; `framing_report.py --acquisitions` prints it in run order with the
raster each state came from.
