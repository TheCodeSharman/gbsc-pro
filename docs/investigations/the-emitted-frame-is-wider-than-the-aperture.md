# The emitted frame is wider than the aperture, and the aperture does not move

At a default framing on a VESA DMT source the card's one-pixel green frame
reaches the top and bottom of the emitted frame and does not reach the left.
The cause is not where the capture window is placed. It is that **the emitted
frame covers more raster than the display window does**, so the picture — which
fills the display window exactly — falls short of the frame at one end or the
other, and the shortfall shows as a black band.

Measured at 800x600@60 into 1080p, capture 1105 units at VDS_HSCALE 815:

| | |
|---|---|
| the picture, from the scale alone | 1388.4 px |
| its span in the emitted frame | ~1904 of 1920 columns |
| so the emitted frame covers | ~1401 raster px |
| the display window is | 1388 px |

The picture is therefore about 13 raster pixels narrower than what the chain
carries, whatever the framing.

## The capture window is NOT where the fault is

Measured in ONE acquisition, with the window zoomed out past the source's active
video at both ends so both green columns lie inside the frame with black either
side:

```
active video   298.9 .. 1403.0 units      one source pixel 1.3800 units
the engine's default window   298 .. 1403   (line 1459, so 1.3816 per pixel)
```

The window is the source's active window to within a unit, and the pixel size
agrees with a 1056-pixel line to 0.1%.

**A cross-acquisition measurement says otherwise and is wrong.** Reading the
active start in one framing and comparing it against a window read in another
gave 20 to 54 units of error across four modes, which is the emitted frame's own
offset being charged to the capture. Take both ends inside one frame.

## Two facts about the instrument, each of which cost a wrong conclusion

**`VDS_DIS_HB_SP` does not move the emitted frame.** Walked 159 → 179 with
automation frozen, the card's green columns did not move at all: 34 and 1910 at
every value. The transmitted window is latched at link-up and keeps what it
latched; the display blanking reaches it only through a re-acquisition. Any
method that walks a blanking register and reads the result off the emitted frame
therefore measures nothing unless it re-locks at every step.

**What does move the picture is `VDS_HB_SP`**, the write origin — so the picture
moves inside a fixed window, trading one edge for the other.

**The placement is otherwise stable.** Six `PAD_SYNC_OUT_ENZ` re-locks at one
framing put the picture within two columns each time, so on the scaling path the
encoder is repeatable and the displacement documented for pass-through does not
apply here.

## A refuted fix

Leading the aperture by a measured constant — the band is flat in output pixels
across magnifications 1.20 to 2.85, 10.2 px with a spread of 1.8, so it looked
like a fixed lead between the write starting and the picture appearing. It is
not: because `placePicture` pins the picture's corner on the raster's active
start, adding the lead moved `VDS_HB_SP` earlier rather than moving the
aperture, and the picture slid left inside an unchanged window — the band at the
head closed and the same width opened at the tail. Reverted.

The flatness is still the evidence that matters: a capture window opening N
units early would have tripled that band between 1.20x and 2.85x.

## What would close it

The span the aperture is given is `horizontalTotal x carriedPx / totalPx`, the
standard's own active fraction, and it opens `TransmittedWindowDelayPx` after
the back porch. Against this bench reading the window the chain actually carries
is both wider than that fraction and offset from that start.

`OutputMode::TransmittedWindowDelayPx` was fitted on four output modes by
walking each blanking register into the window and extrapolating the black
margin to zero. **That method needs a re-lock at every step**, per the finding
above; whether it had one is not recorded. Re-measuring the transmitted window
with a `PAD_SYNC_OUT_ENZ` toggle between steps, on several output modes, is what
this needs — and it must measure the window's WIDTH as well as its start,
because a single delay cannot express a window that is wider than the fraction.
