# The capture margin is clamped away at full framing, and the window is charged for it anyway

`CaptureWindow::captureOn()` puts a spare unit under each end of the picture. The
first and last units the write touches are only partly filled, so the margin is
what lets the aperture show the whole picture without being inset to hide one --
`Axis::captureMargin()` is 1 horizontally and 2 vertically.

It is clamped to the counter's own bounds. A framing across the whole capturable
region has nothing to spare at either end, so **both margins are clamped to
zero** and the capture window comes out equal to the picture. `OutputWindow` was
not told, and went on charging `captureMargin()` in `pictureOffset()`.

Everything then sits a margin late: the picture is placed that far after the
write starts, and the aperture closes that far after the write ends, which is
memory nothing wrote.

## What it looks like on the emitted frame

`320x256@50` on `vga`, 100% framing, read off the USB capture with the registers
taken in the same pass:

| | before | after |
|---|---|---|
| `IF_VB_SP`..`ST` | 1..622 | 1..622 |
| picture | 1..622 | 1..622 |
| `VDS_VSCALE` | 589 | 589 |
| `VDS_VB_SP` | 36 | **39** |
| write | 37.6 .. 1117.2 | **40.6 .. 1120.2** |
| `VDS_DIS_VB_ST` | 1120 | 1120 |
| rows 1077, 1078 | **128.2, 71.1** | **1.49, 1.49** |

The aperture does not move -- it is already on the far bound -- and the write
moves to cover it. Every other edge reads 0.00 before and after.

**The default framing is the control and shows the inverse.** There the capture
is 586 units against the 621 a full framing takes, so the margin fits: the write
runs 37.7 .. 1124.8 against an aperture closing at 1121, over-reaching it by 3.8
lines, and the same rows read 0.00. That is why the defect never showed in
ordinary use, and why it needed a 100% framing to find.

## The margin is handed over, rather than reserved

`VideoPath::marginTaken()` reports what the capture window actually opened on,
and the offsets, the placement and the raster fit all charge that.

**The alternative was to reserve the margin inside `CaptureWindow::place()`**, so
that `captureOn()` never clamps and the invariant `capture = picture + margin`
holds everywhere. It is the tidier statement and it was tried. It also insets the
picture at full framing, which changes what a framing PROPORTION means: a stored
framing at the edge lands a margin further in, and
`the framing realised at a zoom stop is the same whichever bound binds` fails --
a guard whose own comment says a change to where either clamp runs has to
reproduce it exactly. Handing the margin over changes no framing anywhere and
trips nothing.

## What this does not reach

**Only a framing that reaches the counter's bounds.** Anywhere the margin fits,
`marginTaken()` returns the nominal one and every number is what it was.

**It is not the origin error.** Where the source's active video lands in the
counter is `SP_RT_HS_SP` on the undoubled path and `IF_HBIN_SP` on the doubled
one, and both are measured wrong by their own amounts --
`the-line-doubler-resets-the-fifo-late.md` and
`the-retime-stop-is-the-counters-origin.md`. This is the other end: what the
window does with a picture wherever it sits.

**The near edge is now marginally early rather than late.** At the bench state
the display opens at 40 with the write starting at 40.6, and horizontally at 142
against 142.9 -- under a unit at each, where before it opened 3.4 lines after the
write began and hid that much. Both edges of the emitted frame read 0.00.
