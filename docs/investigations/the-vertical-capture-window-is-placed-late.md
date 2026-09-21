# The vertical capture window is placed late

At 800x600@60 on `vga` the solve starts the vertical capture about **eight
counter lines after the source's active image begins**, so the top of the
picture is clipped. Nothing in a register dump shows it: every value is
self-consistent and `/geometry` reports a 600-line capture of a 600-line mode.

## What is measured

Both vertical capture edges moved together -- `IF_VB_SP` and `IF_VB_ST` -- so the
captured height stays at 600 lines and only the window's POSITION changes.
Photographed rectified, with the card's top border band measured by horizontal
variance against flat blanking:

| `IF_VB_SP` | top border band | black above it |
|---|---|---|
| **26, the solved value** | **12 rows** | 33 |
| 22 | 17 | 33 |
| 18 | **21 rows** | 35 |
| 14 | 22 | 40 |
| 10 | 21 | 46 |
| 0 | 21 | 60 |

The band is 21 rows tall and the solve shows 12 of them. It reaches full height
at `IF_VB_SP` 18, eight lines earlier than the solve places it, and everything
below that only adds blanking above the picture.

**Moving one edge alone cannot measure this.** Lowering `IF_VB_SP` on its own
grows the captured height, so the band's far edge moves whether the added lines
are picture or blanking, and the result reads as a translation. The height has
to be held.

## It is not a border

`RetroScaler-Acorn.mdf` gives this mode as `h_timings 128,48,40,800,40,0` and
`v_timings 4,23,0,600,0,1` -- a 40 pixel border at each end of the LINE and
**zero border on the frame**. So vertically the source matches VESA DMT exactly,
active starts at line 27, and there is no border to recover. What the window is
cutting into is the card's own displayed lines.

That separates this from the horizontal case, where a source spends porch on
border and the standard's display area crops it.
`the-encoder-tunes-the-left-edge-in-pass-through.md`.

## Where the eight lines could come from

`VideoSourceLine::FrameLagLines` is **-1.5**, and it is the whole vertical
correction: the window is the standard's active start nudged by that much, which
is what puts `IF_VB_SP` at 26 against an active start of 27. This mode wants
about **-9**.

A constant fitted on one source is indistinguishable from an origin or unit
conversion that happens to cancel there, and both of the lag constants are of
that kind -- `CaptureLagFraction` is stated as measured on two sources and
`FrameLagLines` on one, with its own comment saying a second line count is what
would settle its form. A value that is right at the mode it was taken on and
7.5 lines out here is the shape that question predicts.

**One mode cannot decide it.** A second line count is what separates a genuine
pipeline lag from a conversion error, because the two scale differently.

## A second clip the capture cannot reach

The card's bottom border band reads **5 rows at every capture position in the
sweep**, including those that put the whole top band on screen. A window that
cannot move it is not what is cutting it, so the bottom is clipped on the output
side -- the display window against the output raster -- and the two clips are
independent faults.

## The horizontal arithmetic disagrees too

```
/geometry   oh 329   eh 1190   ch 1607
DMT         active start 216/1056 -> 329    agrees
            active       800/1056 -> 1217   against eh 1190, 27 units short
```

Not established as a defect: a stored pan and zoom would account for it, and the
framing was not read back. Read the framing before treating it as one.

## Open

Whether the lag constants describe a delay or absorb a conversion. Whether the
bottom clip is the display window or the raster. Whether the horizontal shortfall
survives a framing that is known to be at its default.
