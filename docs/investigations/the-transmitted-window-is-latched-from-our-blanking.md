# The transmitted window's start is latched from our blanking, so it is not a constant to fit

What the chain carries out of our raster is two quantities and they are not the
same kind. Its **width** is the output mode's own active fraction, and that
holds everywhere it has been measured. Its **start** is not computed at all: the
encoder latches it at link-up, near wherever `VDS_DIS_HB_SP` was at that moment,
and then holds it until the link goes again.

It lands a little EARLIER than the aperture, so a picture placed exactly in the
aperture shows a black band at the left and loses picture off the right. That
offset is not noise -- ten re-locks at one state give the same margins to the
pixel -- but it is not ours to choose either, because the latch follows whatever
we do to the blanking.

**`OutputMode::TransmittedWindowDelayPx` therefore corrects nothing.** It was
fitted from +19.5 / +20.3 / +19.8 / +20.8 on four output modes at one source
rate, which reads as a constant of the chain; the same measurement on one output
mode across six rasters runs 0.4 to 8.6.

## The latch, measured

800x600@60 into 1080p, raster 1592, aperture 159..1547, automation frozen. Each
row sets `VDS_DIS_HB_SP`, toggles `PAD_SYNC_OUT_ENZ` to re-acquire the link,
then puts the register back at the aperture and reads the emitted black margin —
so the margin is a reading of the WINDOW rather than of the blanking.

| `VDS_DIS_HB_SP` at lock | margin back at the aperture |
|---|---|
| 100 | **83** |
| 300 | 3 |
| 160 | 3 |

It follows the register DOWN and not up. Blanking earlier than the latch moves
the video's leading edge earlier, which the encoder takes, and the margin the
window then shows at the aperture grows by the whole of it. Blanking later
leaves black inside the window it already holds, which is no cue: 300 is not a
window that opens at 300, it is the window refusing to move.

## The width is the mode's fraction, and that is confirmed

One output mode across six rasters, driving the source's field rate from 50 to
85 Hz so the raster is the only thing moving. Each edge found by walking one of
our blanking registers into the picture and extrapolating where the blanking
lands back to the frame's edge.

| raster | aperture | window | start | width |
|---|---|---|---|---|
| 1914 | 159 .. 1829 (1670) | 158.58 .. 1830.38 (1671.79) | −0.42 | +1.79 |
| 1706 | 159 .. 1647 (1488) | 157.30 .. 1647.25 (1489.96) | −1.70 | +1.96 |
| 1592 | 160 .. 1548 (1388) | 151.38 .. 1542.37 (1390.99) | **−8.62** | +2.99 |
| 1370 | 159 .. 1354 (1195) | 156.83 .. 1354.82 (1197.99) | −2.17 | +2.99 |
| 1330 | 154 .. 1313 (1159) | 150.48 .. 1313.50 (1163.01) | −3.52 | +4.01 |
| 1134 | 129 .. 1117 (988) | 127.77 .. 1119.03 (991.26) | −1.23 | +3.26 |

The width column is `horizontalTotal x carriedPx / totalPx` to within 4 units of
1200 to 1700, and a couple of those units are the instrument: the changed strip
is bounded where the difference clears a threshold, so the interpolated edge is
read a pixel wide at each end and the span comes back that much long.

The start column is the latch, and it is what a delay constant was fitted to: it
sits within about two units of the aperture five times and nine units out once,
on one output mode with nothing changed but the source's field rate.

## What follows for the picture

The near end and the far end are ONE quantity, because the width is right: a
window latched N units before the aperture puts N units of black at the left and
loses N units of picture off the right. That is what the card shows at a default
framing — every margin measured across the DMT set is `N | 0`, all the slack at
the left and none at the right.

**A constant cannot express it**, and that is measured rather than argued.
800x600@60 into 1080p, default framing, ten re-locks at one state first to
establish that the latch has no scatter at all -- `10 | 0` on every one of them.

Then the whole solve shifted, write origin and aperture together, which is
exactly what a different `TransmittedWindowDelayPx` does. Each row re-acquires
the link and reads the card's green frame against the emitted frame's edges:

| solve shifted by | margins after the re-lock |
|---|---|
| +0 | 10 \| 0 |
| +8 | 10 \| 0 |
| +16 | 10 \| 0 |
| −8 | 10 \| 0 |

And the aperture alone, the picture untouched, in case the window's start were a
natural position our blanking was merely hiding:

| `VDS_DIS_HB_SP` | margins after the re-lock |
|---|---|
| 159 (solved) | 10 \| 0 |
| 151 | 8 \| 0 |
| 143 | 10 \| 0 |
| 135 | 10 \| 0 |

The window follows `VDS_DIS_HB_SP` down with a lead of about 7.6 units, holds
the mode's fraction for its width, and nothing the horizontal solve can do
changes the result. **Do not reinstate a delay constant to correct this**: the
four readings of ~20 that justified the present one are four samples of the
latch, and moving it is the +8 row above.

## What is left that a solve could reach

The two ends are not equally hopeless.

The black at the LEFT is window the chain carries before our aperture opens, and
our aperture is what blanks it. Opening the aperture earlier moves the latch by
the same amount, so the band is **irreducible from the board** -- 10 output
pixels of 1920 at this raster.

The loss at the RIGHT is ours. The window runs `apertureStart - 7.6 + 1391`
while the picture runs `apertureStart + 1388`, so the last ~4.6 units of picture
are painted past the end of what the chain carries. Charging the lead to the
SPAN rather than to the start -- a picture ~5 units narrower, scaled to fit --
would land the picture's far edge on the window's, losing nothing. Whether the
lead holds across output modes is measured at 1080p only.

## What it supersedes

[the-transmitted-window-opens-late.md](the-transmitted-window-opens-late.md)
lists "the window latching from our blanking at lock" as refuted, on
`VDS_DIS_HB_SP` set to 300 across a re-lock leaving the window at 159.1. That is
the one direction the latch does not move in, and the reading is reproduced
above. Its measurement of the WIDTH stands and is confirmed here.

`the-emitted-frame-is-wider-than-the-aperture.md` said `VDS_DIS_HB_SP` does not
move the emitted frame at all, from a walk of 159 to 179 that left the card's
green columns where they were. The walk blanks 1.38 output pixels per raster
unit, so 20 units reaches column 28 and the green column it was judged on sat at
34: the register was working and the walk was too short to touch the thing being
watched. That page is gone.

## Measuring it again

`tools/gbsc-pro-hwtest/transmitted_window.py` does the whole sweep, including
driving the source's field rate to walk the raster.

Two instrument traps, each of which produced a wrong answer that looked like a
finding:

**Do not find the edge by the first lit column.** The card's concentric bands
put whole black columns inside the picture, so a brightness test runs past the
blanking edge to the next bright band and reads tens of pixels late — which
arrives as a jump in an otherwise straight line rather than as an artefact.
Difference against the previous step instead and take the strip whose width is
the one the step size predicts. The card animates, so the frame carries changed
columns that have nothing to do with the register, and neither "the outermost
run" nor "the run nearest the edge" tells them apart: a flashing block can lie
further out than the strip. Its width does.

**Put picture at the edge being walked.** A source with its own border leaves
black there, and blanking black changes nothing — which reads as a register that
does nothing rather than as a framing that cannot be measured. The tool zooms
until picture reaches both ends; the aperture is the raster's active window
whatever the framing, so this moves what is measured through the window without
moving the window.
