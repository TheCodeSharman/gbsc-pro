# The border is measured against the opposite edge

`card_edges.edge_wholeness()` reports how much of the source's outermost pixel
survives on the emitted frame. The reference is the OPPOSITE edge of the same
frame. This is what that replaced and why nothing absolute works.

## The card's solid green is not the border's amplitude

`PATTERN CARD` draws its frame on the outermost pixel -- one source pixel, in
`&00FF0000` green -- and the measure is the MASS of the green dominance in the
run at each edge. Dividing that by `the card's solid green x the pixel's width
in output columns` reads a border the capture window opened hundreds of columns
before as mostly missing:

| mode | columns of black before the border | reported |
|---|---|---|
| 320x256@50 | 242 | 0.36 |
| 800x600@60 | 186 | 0.50 |
| 640x512@50 | 254 | 0.68 |

The capture samples one source pixel between 1.3 and 4.3 times, so at low
density every sample straddles the castellated ring beside it. Green dominance
is `g - max(r, b)`, which a blend with the ring's white or yellow does not
attenuate but DESTROYS -- so the mass falls with sampling density and the modes
that failed were the ones sampling their own pixels fewest times. Measured on
`vga` at 640x512@50, 1.86 emitted columns per source pixel, the border 254
columns clear of the frame's edge:

```
profile   0.0  0.0  22.5  22.5  43.8  43.6  57.1  56.7  6.0  5.8  0.8  0.9  0.0
peak      57.1   against the card's solid green at 196   = 0.29 of it
mass     246.3   against amplitude x source_px = 364.3   = 0.68
```

**It is not the picture filters.** Peaking and the six-tap filter toggled off at
one framing on 800x600@60, with the window, the divider and the scale unmoved,
leave the same border between 0.41 and 0.53.

**And the synthetic suite could not see it.** `blocks()` in
`test_card_edges.py` draws a hard-edged rectangle exactly one source pixel wide
at full amplitude, so its mass IS `amplitude x source_px` and every case reads
1.00. `test_an_attenuated_border_clear_of_the_frames_edge_is_whole` carries the
bench's shape instead.

## What makes the opposite edge a reference

It is the same feature through the same pipeline at the same sampling density,
so every term that varies between modes cancels. The premise it rests on is
that **at most one edge can be clipped**: the capture window's EXTENT is the
raster's own active fraction to within half a unit on every mode measured, so a
window placed wrong is SHIFTED rather than narrowed -- it opens before the
source's active region at one end and cuts into it at the other. Both edges cut
at once needs a width short by the sum of the two, which `full_margins.py`
sizes directly.

**It is the CLASSIFIED edge rather than the outermost run.** The card's own
blocks outweigh a one-pixel border many times over, so where one edge is off
the frame the outermost run is a block: normalising the surviving edge against
it reported a whole far border as 0.01 of itself.

## What the measure's own spread is

Taken at FULL framing, where the window carries the source's blanking at both
ends and the whole of the black is accounted for by it, so neither border can
have been cut. 1080p, 16:9, separate sync:

| mode | axis | near mass | far mass | ratio |
|---|---|---|---|---|
| 320x256@50 | down | 495.3 (109 rows clear) | 543.2 (80 clear) | 0.91 |
| 800x600@60 | across | 261.0 (184 columns clear) | 288.6 (80 clear) | 0.90 |
| 640x512@50 | across | 356.3 (254 clear) | 269.1 (202 clear) | 0.76 |
| 640x512@50 | down | 296.5 (27 clear) | 295.1 (13 clear) | 1.00 |

On 640x512@50 across, the source's active window spans 1018 of the 1337 capture
units the full framing holds, which is 1462 of 1920 columns -- leaving 458 for
blanking against the 456 measured, so the black at both ends is the source's and
both borders are whole. The two differ by 24% at the same width, six columns
each, so it is AMPLITUDE rather than extent: what differs between the two ends
of a line is not established.

**So a reading in the high seventies is inside the instrument.**
`WHOLE_ENOUGH` is 0.75 and the acceptance is three quarters of the pixel, which
is a requirement rather than a figure fitted to the measure -- a case reading
0.76 is not evidence that a quarter of its pixel is gone.

## A halved border is not the spread, and the round trip says which

800x600@60 down at full framing reads 362.9 at the top and 181.1 at the bottom,
the bottom run one output row where the top is two. Four source mode round
trips with the window untouched give 324.7, 324.9, 324.7, 324.6 at the top and
162.2, 162.3, 162.2, 162.2 at the bottom -- repeatable to a tenth, so the
halving is not a sub-pixel landing. The picture's last row carries half the
border and twelve rows of black follow it, which is the window stopping inside
the border and our own blanking beyond.

**That is the case the sliver test pins**, and it is why a rule keyed on "black
beyond the run, therefore whole" is not available: at the far end our blanking
is indistinguishable from captured source blanking. Separating the two needs
`VDS_Y_OFST` lifting the picture.
