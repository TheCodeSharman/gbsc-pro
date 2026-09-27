# The transmitted window opens later than our back porch, by a fixed count of samples

`OutputMode::solve()` used to place active video at `sync + back porch`, both
converted from the standard's durations, on the reading that the chain starts
carrying video where our blanking ends. It does not. It starts about **20
samples later**, on every output mode measured, and a picture placed on the back
porch therefore loses its leading samples off the emitted frame and leaves the
same width black at the far end.

`OutputMode::TransmittedWindowDelayPx` charges it. The window's WIDTH is
unaffected -- that is the mode's own fraction, and it is confirmed here -- so
the whole of the correction is a shift.

## What the picture does without it

Bench RiscPC at 320x256@50 on `vga`, output 1080p, zoomed so the picture
overruns the raster at both ends. The emitted frame carries **27 black columns
at the right and none at the left**: 1893 of 1920.

That is the whole of the fault. The left of the picture is not cropped by a
panel and the right is not short of picture -- the picture is 20 units before
the window at one end and 20 units short of it at the other.

## What the correction does

Flashed, RiscPC at 320x256@50 on `vga`, output 1080p, the same framing either
side -- capture 630 units, `VDS_HSCALE` 386:

| | `VDS_DIS_HB_SP` | `VDS_DIS_HB_ST` | left | right |
|---|---|---|---|---|
| before | 143 | 1808 | 0 | **27** |
| after | 163 | 1828 | 24 | **3** |

Every solved horizontal register moves by the 20 and nothing else changes. The
left gains black because the source's own border is no longer falling off the
frame -- the card's corner marker is on screen after and cut off before -- so it
is picture area the framing can now reach rather than loss.

Framed to fill the screen afterwards, at `VDS_HSCALE` 420 with the aperture at
163..1830, the emitted frame carries **4 px of black at the left and 1 at the
right**, and both are what the window predicts: (163 - 159.5) x 1.15 = 4.0 and
(1830.6 - 1830) x 1.15 = 0.7. The left one is the aperture's one-capture-unit
inset, which is ~3 px at that magnification, so there is no slack left to
recover at either end.

## The instrument

The USB HDMI capture, not a photograph of the panel: it is the emitted frame
itself, 1920 columns, no camera and no photo-column mapping to re-derive.
`docs/bench-output-capture.md`.

Each edge of the window is found by walking one of our own blanking registers
INTO it and extrapolating the emitted black margin back to zero:

| `VDS_DIS_HB_ST` | right margin |
|---|---|
| 1760 | 82 |
| 1780 | 59 |
| 1800 | 36 |
| 1812 | 22 |
| 1830 | 1 |
| 1850 | 0 |

1.15 emitted px per raster unit, zero at **1830.6**. The near edge the same way
with `VDS_DIS_HB_SP`, zero at **159.5**. Neither reading depends on where the
picture happens to sit, which is what makes them measurements of the WINDOW
rather than of the framing -- the margin tracks the register linearly as long as
the sweep stays inside the picture, and flattens once the register passes the
window's own edge.

## The measurement

Four output modes, automation frozen, the engine's own solve untouched:

| mode | raster | clock | `sync + porch` | window opens at | delta | window width | the mode's fraction |
|---|---|---|---|---|---|---|---|
| 1080p | 1916 | 108.0 MHz | 140 | 159.52 | **+19.52** | 1671.09 | 1672 |
| 1024p | 2024 | 108.1 MHz | 360 | 380.25 | **+20.25** | 1532.75 | 1534 |
| 960p | 2156 | 108.0 MHz | 424 | 443.80 | **+19.80** | 1530.38 | 1533 |
| 720p | 2156 | 81.0 MHz | 284 | 304.78 | **+20.78** | 1672.44 | 1672 |

The width column is why the correction is a shift and not a resize: the fraction
model holds to 3 px of 1920 on all four, so only the origin was wrong.

## It is a count of SAMPLES, not a time

720p runs a 25% slower display clock than the other three and carries the same
delta. A constant time would put it at 15 units rather than 20.7.

## What it is not

| refuted | how |
|---|---|
| the window latching from our blanking at lock | `VDS_DIS_HB_SP` set to 300 and the link re-acquired by toggling `PAD_SYNC_OUT_ENZ`: the window stayed at 159.1, and putting the blanking back gave 159.5. Three measurements either side of two re-locks, all the same window |
| our own hsync pulse | `VDS_HS_SP` walked 16, 32, 48, 64 with a re-lock at each: start 159.52, stop 1830.61 and width 1671.09 at every one of them, to the digit |
| the standard's blanking as a fraction | `(sync + porch) / totalStd` predicts 167 at 1080p, 432 at 1024p, 508 at 960p and 340 at 720p, against 159.5 / 380.3 / 443.8 / 304.8 -- wrong by up to 64 units and in no fixed direction |
| the back porch as a fraction with the sync kept a duration | fits 1080p to 1.4 units and misses 1024p by 29 |

## Where it lives is not established

Two mechanisms produce the same measurement and nothing here separates them:
the chain opening its window 20 samples after the back porch, or the part's
video coming out 20 samples early against the HSOUT it is timed to. The
correction is identical either way, and it is a constant of this board rather
than of a display -- the reading is off the emitted frame, so no television is
in it.

The overlay instrument in
[the-picture-position-is-latched-not-re-rolled.md](the-picture-position-is-latched-not-re-rolled.md)
is what could split them, since the STV9426 rides the sync timebase and the
picture rides the VDS's counter.

## What it supersedes

[the-transmitted-window-is-a-per-mode-fraction.md](the-transmitted-window-is-a-per-mode-fraction.md)
reads the left edge as landing on `activeStart` to within 2 px on all six modes.
That was measured against the panel, through a photo-column mapping fitted per
mode from window clips, and it is superseded for the near edge: the emitted
frame puts the window 20 units later on all four modes re-measured. Its finding
about the WIDTH -- `carriedPx`, and that it is not `activePx` on the two SD
modes -- is untouched and is confirmed above on the four it covers.

The SD modes are not re-measured. They are the two whose width already departs
from the standard, and the unit left the network before they were reached.

## A side effect worth knowing

`VideoPath::dividerCeilingForOutput()` sizes the sampling divider from
`maximumCapture(AxisHorizontal, total, 0, activeStop)` -- passing 0 for the
start, so the room it measures runs from the write floor to the far bound rather
than being the window. The far bound moves with the correction, so every
divider rises by 20: 1446 to 1466 on the bench source at 1080p. It is a
generous ceiling by construction and 20 samples is inside the slop it already
carried, but it means a constant about where the window OPENS reaches the
sampling clock, which is not a coupling anything wanted.

## Measuring this again

`tools/gbsc-pro-hwtest/hdmi_capture.py` for the frame and `setfield.py` for the
edges. Freeze automation first or the solver rewrites the windows underneath
the sweep, and put content hard against the edge under test -- at a default
framing the last thing on screen is captured input blanking, so the margin
measures the source's border.
