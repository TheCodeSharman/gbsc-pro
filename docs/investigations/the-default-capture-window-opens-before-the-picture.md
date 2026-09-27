# The default capture window opens before the picture, horizontally

The engine places an untuned source's capture window from the raster the
standards state: where the frame, the field rate and the hsync duty match a
published mode, `Tv5725::SourceTiming` takes that raster's own active window on
both axes. On the bench RISC PC running the VESA DMT set, the horizontal window
lands 20 to 54 counter units BEFORE the source's first active pixel, so a
default framing shows a band of the source's own back porch at one end and
loses the same amount of picture at the other.

The vertical axis does not do this. It carries `VerticalOriginLines = 7`, a
measured lead between the vsync pulse's leading edge and the counter's origin.
The horizontal axis carries no equivalent, and this page is the measurement that
says it needs one -- or that the source is not emitting the porch the standard
states. **The two have not been separated.**

## What was measured

`PATTERN CARD` draws a one-pixel green frame on the source's outermost pixels,
so it marks where active video begins and ends. With the capture panned to the
head of the line and zoomed out until the window is wider than the picture, both
green columns are in the emitted frame and each one's counter position follows
from the window the engine reports.

Anchored by a `PAD_SYNC_OUT_ENZ` toggle first, because where the picture sits in
the emitted frame is latched at link-up: the toggle re-locks the link against
the blanking in force, which puts the frame's first column on the aperture's
first pixel.

| mode | active starts | the standard's | difference | source px |
|---|---|---|---|---|
| 640x480@60 | 288.0 | 263.7 | +24.3 | +13.3 |
| 800x600@60 | 319.0 | 298.4 | +20.6 | +14.9 |
| 1024x768@60 | 346.4 | 321.8 | +24.6 | +22.7 |
| 800x600@72 | 270.5 | 216.4 | +54.1 | +46.2 |

Counter units, 1080p output, default framing reset before each.

Two things the same measurement settles, and they are what make the table
trustworthy:

- **the source's active WIDTH is the standard's**, to 0.13% on every mode, and
  one source pixel comes out at 1.3809 units against a line of 1056 pixels
  giving 1.3816. The horizontal total and the pixel size are the standard's.
- **the sync width is the standard's too**, 178 units against 128 pixels of
  1056 at 800x600@60. The pulse is not where the discrepancy is.

So the whole of it sits between the counter's origin and the first active pixel.

## What it is not

- **Not the capture margin.** `Axis::captureMargin` is one unit horizontally and
  opens the register window that much earlier still; the figures above are
  against the framing's own origin, which is the picture.
- **Not the encoder's latched displacement.** That moves the picture inside the
  emitted frame by tens of columns between acquisitions and is the reason the
  table is anchored. It cancels out of the active WIDTH, which is why the width
  agreeing with the standard is evidence the anchoring worked.
- **Not a constant this page can name.** Three of the four cluster at 20.6 to
  24.6 units, which is the shape a counter-origin lead would have -- the
  vertical one is a count of lines, not a fraction. 800x600@72 is 54.1, and DMT
  gives that mode an unusually short back porch of 64 pixels against a front
  porch of 56. A lead fitted to the three would be five source pixels out on
  640x480@60 and 46 out on 800x600@72.

## What it costs

`card_edges.py` resets the framing per mode and reports which of the card's
green edges reach the emitted frame. At 1080p across six DMT modes: vertically 0
or 1 output pixel of the source's own blanking on screen, horizontally one green
edge on five of the six -- one end showing blanking, the other end missing
picture. 640x480@60 lands inside the four-pixel allowance.

A user reaches the picture by panning, and the framing is stored per source, so
this is a default rather than a wall.

## What would separate it

The question is whether the counter's origin leads the hsync leading edge, or
whether VIDC20 is not emitting the back porch the mode file states. The RISC PC
is the only source that can be driven through the DMT set, so it cannot answer
the second on its own.

- **A second source running one of these modes** settles it directly: a lead
  belongs to the chip and would reproduce, a porch belongs to the machine and
  would not.
- **`SP_H_CST_SP`/`ST` and `SP_RT_HS_SP`** are the sync processor's own view of
  where the line starts, in the same ADC clocks the counter runs on. Nothing has
  compared them against the counter's origin.
- **The vertical lead was measured across four pulse widths** and came out
  between 7.3 and 7.9 lines whatever the pulse
  (`the-vertical-capture-window-is-placed-late.md`). The horizontal equivalent
  would be the same experiment across the sync widths the DMT set already
  offers -- 64, 72, 80, 96, 112, 120, 128 and 136 pixels.
