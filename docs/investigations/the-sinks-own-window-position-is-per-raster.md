# The sink's own window position is per raster, in steps, and picture at the edge lands short of the aperture on some rasters

The MS9288A has a position of its own for the start of the window it
transmits, one per output raster, which it uses whenever the line carries
nothing but black at our aperture's edge when the sync pad returns. Measured
across the stock AKF50 set into 1080p and 720p it is not a function of the
raster total, the clock or the field rate that any term fits: within an output
it steps with the field rate in bands, two sources sit off their raster's
band, and where that position is before our aperture a line with picture at
the aperture's edge lands 6.6 to 10 units before the aperture, settled or not.

Measured with `framing_sweep.py --tier S` (`--probes sink`): per state, the
default framing acquired, the capture panned 24 IF units earlier so the
source's own back porch or border is what the aperture's edge carries,
automation frozen, `PAD_SYNC_OUT_ENZ` taken away for 1.5 s and returned, and
the horizontal aperture walked into the picture, each step read as an edge at
the slope the raster predicts and the median taken. Every AKF50 mode into
1080p, fifteen into 720p, five of those twice.
`framing_report.py --sink` prints the rows and the fits from
`sweeps/framing-20260930T094634Z.jsonl.gz`, `…T101647Z…` and `…T103658Z…`.

## The position per raster

Raster units; `own` is the window's start with black at the edge, `model` is
where `OutputMode::solve()` opens the aperture, and `sink px` is the start in
the sink's own pixels, `own x total / T`.

| output | T | Hz | clock MHz | states | own | spread | model | own − model | sink px |
|---|---|---|---|---|---|---|---|---|---|
| 1080p | 1592 | 60.32 | 108.0 | 2 | 157.4 | 0.4 | 160 | −2.6 | 217.5 |
| 1080p | 1596 | 60.15 | 108.0 | 1 | **144.5** | — | 160 | −15.5 | 199.2 |
| 1080p | 1598 | 60.10 | 108.0 | 1 | 159.2 | — | 160 | −0.8 | 219.1 |
| 1080p | 1600 | 60.00 | 108.0 | 2 | 158.5 | 0.0 | 160 | −1.5 | 218.0 |
| 1080p | 1602 | 59.94 | 108.0 | 1 | 158.2 | — | 160 | −1.8 | 217.3 |
| 1080p | 1604 | 59.87 | 108.0 | 1 | 156.9 | — | 160 | −3.1 | 215.2 |
| 1080p | 1706 | 56.25 | 108.0 | 2 | 158.7 | 0.1 | 160 | −1.3 | 204.6 |
| 1080p | 1914 | 50.16 | 108.0 | 1 | 171.2 | — | 160 | +11.2 | 196.8 |
| 1080p | 1916 | 50.08 | 107.9 | 4 | 171.7 | 1.0 | 160 | +11.7 | 197.2 |
| 1024p | 1350 | 75.00 | 107.9 | 1 | 318.0 | — | 311 | +7.0 | 397.7 |
| 1024p | 1392 | 72.81 | 108.0 | 3 | 328.0 | 0.4 | 321 | +7.0 | 397.8 |
| 1024p | 1446 | 70.08 | 108.0 | 2 | 331.0 | 0.3 | 334 | −3.0 | 386.4 |
| 1024p | 1448 | 70.00 | 108.0 | 1 | 332.7 | — | 334 | −1.3 | 387.8 |
| 720p | 1920 | 75.00 | 108.0 | 2 | 393.4 | 0.4 | 398 | −4.6 | 338.0 |
| 720p | 2058 | 70.00 | 108.0 | 1 | 395.6 | — | 398 | −2.4 | 317.2 |
| 720p | 2388 | 60.32 | 108.0 | 1 | 395.8 | — | 398 | −2.2 | 273.5 |
| 720p | 2394 | 60.15 | 108.0 | 2 | **382.3** | 0.0 | 398 | −15.7 | 263.5 |
| 720p | 2396 | 60.10 | 108.0 | 1 | 397.2 | — | 398 | −0.8 | 273.6 |
| 720p | 2400 | 60.00 | 108.0 | 2 | 395.6 | 0.5 | 398 | −2.4 | 272.0 |
| 720p | 2402 | 59.94 | 108.0 | 2 | 396.1 | 0.0 | 398 | −1.9 | 272.1 |
| 720p | 2406 | 59.87 | 108.0 | 1 | 397.7 | — | 398 | −0.3 | 272.8 |
| 720p | 1920 | 56.25 | 81.0 | 2 | 307.0 | 0.0 | 304 | +3.0 | 263.8 |
| 720p | 2154 | 50.16 | 81.0 | 2 | 302.0 | 0.0 | 304 | −2.0 | 231.4 |
| 720p | 2156 | 50.08 | 81.0 | 2 | **344.6** | 0.0 | 304 | +40.6 | 263.8 |

The 1024p rows are the 70 to 75 Hz sources the encoder cannot carry at 1080p,
where the engine falls back and the model's start is the rate clamp's. Every
raster measured more than once agrees to within a unit, and the five states
measured twice repeat to 0.1.

## No term places it

- **Not the raster total.** At 1080p the position is 157 to 159 from T 1592
  to 1706 and 171.5 at 1914 and 1916: a step of twelve units where a line in
  T would put four. The two 720p rasters that share T 1920 -- 75 Hz at
  108 MHz and 56.25 Hz at 81 MHz -- sit 86 units apart.
- **Not the clock or the time after the pulse.** In the sink's own pixels the
  1080p rasters sit at 215 to 219 at 60 Hz, 204.6 at 56.25 Hz and 197 at
  50 Hz; 720p at 272 to 274 at 60 Hz, 338 at 75, 317 at 70, 264 at 56.25;
  1024p at 397.7 for 75 and 72.8 Hz and 387 for 70 Hz. In microseconds after
  our hsync the 720p rasters at 108 MHz all read 3.64 to 3.68 and the ones at
  81 MHz 3.73 to 3.78, with the 1080p rasters at 1.46 to 1.59.
- **The fits say the same.** Over the own-position rows the start against T
  fits worst 11.3 units at 1080p and 3.2 at 1024p; against the clock, 15.3
  and 2.8; the width against the mode's fraction of the line fits to 1.7 and
  0.6, which is the width the earlier records already had.

What the sink keys on is its own identification of the line, in bands of
field rate: at 1080p the 56 to 60 Hz rasters take one position and the 50 Hz
rasters another; at 1024p the 75 and 72.8 Hz rasters take one and the 70 Hz
pair another. Within a band the position is a constant in the sink's pixels
to within two.

## Two sources sit off their raster's band

- **360x480@60**, 525 lines at 60.15 Hz: 144.5 at 1080p where its neighbours
  at 60.10 and 60.32 Hz sit at 157 to 159, and 382.3 at 720p where they sit at
  396 to 398 -- 14 to 16 units early at both outputs, twice at 720p, and the
  transition into it in the Tier C record read 142.3.
- **320x256@50 into 720p**, T 2156 at 81 MHz: 344.6 twice, against 302.0 twice
  for 640x512@50 two units of T and 0.08 Hz away on the same clock. Into
  1080p the same two sources agree, 171.7 and 171.2.

The output raster, clock and sync of each pair are alike to every instrument
here, so what separates them is not established. A placement keyed on
anything the engine solves from carries these as errors of 14 and 41 units.

## Picture at the aperture's edge lands short of it where the own position is before the aperture

Tier C placed every 1080p state through a source mode change. Read against
the aperture of the same state:

| placed | states | at the aperture's edge |
|---|---|---|
| on the own position | 15 of 28 | the source's own black |
| on the aperture, within 2.3 | 8 of 28 | picture: 768x288, 1056x250 and 1056x256 on the 1916 raster, and the 72.8 and 75 Hz sources on the 1024p fallbacks -- every raster whose own position is AFTER the aperture |
| on its own content, 4 units in | 640x512@50 | 5.5 units of border, then picture |
| 6.6 to 10 units BEFORE the aperture | 640x480@60, 1280x480@60, 800x600@56, 1600x600@56 | picture, on the 1600 and 1706 rasters, whose own position is 1.5 units BEFORE the aperture |

A settled pad toggle with picture at the edge lands where the transition did:
151.9 against 151.8 on 640x480@60 and 153.5 against 153.0 on 800x600@56,
with black panned to the edge in the same run landing on the own position at
158.3. So the short placement is the sink's own, keyed on the raster and on
what the line carries, and not on when the pad returned; an earlier reading
of this table that put it down to the transition compared picture-at-edge
transitions against black-at-edge toggles.
`a-transition-and-a-settled-toggle-place-the-window-alike.md`.

## What follows

- The model's twenty units after the porch is within three units of the own
  position on every 720p raster, on the 56 to 60 Hz rasters at 1080p and on
  the 70 Hz fallbacks; it is 11.5 units early on the 50 Hz 1080p rasters --
  the everyday source -- and 7 units early on the 75 and 72.8 Hz fallbacks.
  A per-output constant cannot close those; a table per output and field-rate
  band can, and the two sources above are outside any table.
- An aperture placed AT the own position frames a source with black at the
  edge flush at both ends on the 1916 raster, which is the everyday case. A
  source with picture at the edge on the 1600 and 1706 rasters lands 6.6 to
  10 units short however the pad returns, and what the sink keys on there is
  not established.
- A capture that opens exactly on the picture puts picture at the aperture's
  edge on every source, and the sink's pull then lands the window on the
  aperture and the right-hand bar goes with it -- on a settled lock. That is
  the capture's row rather than the output side.
- The four short states are the measurement for any change to the aperture's
  placement: the same four, settled, through a pad toggle, before and after.

`investigations/the-encoder-places-its-window-when-the-sync-pad-returns.md`
is the rule this rests on; `investigations/the-encoder-window-start-is-per-source.md`
carries the earlier per-source readings, which are the transition placements
above.
