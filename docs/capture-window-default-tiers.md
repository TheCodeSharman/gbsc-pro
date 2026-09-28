# The capture window default: CEA, VESA, Acorn, generic

The default capture window is chosen by an **ordered lookup with a generic
fallback**, not by a formula and not by a per-source branch. A measured source
is offered to each tier in turn and the first that matches supplies its own
active window; a source matching none gets the envelope.

    CEA-861  ->  VESA DMT  ->  Acorn  ->  generic envelope

`docs/vesa-gtf.md` is why there is no curve: GTF emits no valid raster below
about 20 kHz at all, and no formula supplies the **border**, which is black
active video and so electrically identical to back porch. A mode file states
`sync, back porch, left border, display, right border, front porch` separately,
which is the one place that distinction is written down.

## What a default is for

**The pan and zoom controls already reach full screen on any source.** A default
is a convenience: where the source is one the firmware can recognise, it should
open on a sensible framing rather than on the envelope. Nothing here adds a
capability, and nothing here may cost one -- a tier that guesses wrong must
leave the controls able to recover, which the envelope always does.

## What the generic tier does, and why a 15 kHz source wants better

The envelope spans **11.7% to 98.1%** of the line, taken from what real sources
put there, so nothing is cropped and what is captured beyond the picture is
black. A source reaching it opens on `oh 129 eh 951` of `ch 1101`.

That is what `X320 Y256 C256 F50` got before the Acorn row: the window takes in
both porches and both borders, the picture is a little over half of what is
captured, and the emitted frame carries a black surround the user has to pan and
zoom away. The tier's whole job is to open on the picture instead.

## What the Acorn tier keys on and supplies

A stock Acorn mode file -- `AKF50` and friends, never the hand-authored
`RetroScaler` file, which predicts the bench machine and is worthless as
evidence about RISC OS timings at large. `tools/gbsc-pro-hwtest/mdf_modes.py`
parses them.

For the bench mode, `h_timings:36,30,44,320,44,38` over a 512-pixel line at
8 MHz:

| | source pixels | share of line |
|---|---|---|
| sync | 0..36 | 7.0% |
| back porch | 36..66 | 5.9% |
| left border | 66..110 | 8.6% |
| **display** | **110..430** | **62.5%** |
| right border | 430..474 | 8.6% |
| front porch | 474..512 | 7.4% |
| border + display | 66..474 | 79.7% |
| **today's generic envelope** | | **11.7%..98.1%** |

So the tier has two defensible answers and they are not the same convenience:

- **display alone**, 21.5%..84.0%, opens on the picture with the border trimmed.
- **border + display**, 12.9%..92.6%, opens on everything the source draws.

**It states display alone**, for two reasons. The CEA and DMT rows already do:
those standards state no border, so their `active` IS their display, and a tier
stating the border as picture would put a different thing in the same column.
And the border is black active video, so a window opened on it shows the
source's own border as a black bar the controls then have to trim -- where a
window opened on the display alone overruns into a border that is black anyway.
A black bar at the edge reads as the scaler failing to fill the screen; the
overrun costs content that is not there.

## Matching, and what makes a match safe

The firmware sees sync edges, not mode files, so a tier can only key on what is
measurable: the frame height, the field rate, and the hsync duty.
`Tv5725::SourceTiming` already matches the standards tiers that way and
`SyncDutyTolerance` is the window it allows.

Two hazards carried over from the existing tiers:

- **A duty taken while the sync processor is not counting matches nothing**, and
  the fallback is then the envelope rather than a wrong tier. That is the right
  failure direction and must stay that way.
- **Key vertical on field rate, never line rate.** 50 against 59.94 Hz is a 20%
  gap; the line rates are 0.7% apart and not safely separable.

Acorn modes are dense in `(height, field rate)` -- several share 256 lines at
50 Hz with different widths and pixel clocks -- and width is exactly what the
chip cannot see, since the horizontal axis has no native resolution. **So the
tier cannot distinguish `320x256` from `640x256` by measurement**: both are 512
pixel clocks at 8 MHz, same raster, same duty. Their `h_timings` are
`36,30,44,320,44,38` and `36,30,44,640,44,38` scaled to the same line, so they
share a share-of-line layout and one entry serves both.

**SEVEN MODES REACH THE ONE 15.6 kHz ROW**, and they do not all share a layout.
At 312 lines and 50.08 Hz the monitor definition offers `320x250`, `320x256`,
`640x250`, `640x256`, `1056x250`, `1056x256` at a 7.03% duty and `768x288` at
7.42%, which `SyncDutyTolerance` cannot separate from the rest:

| modes | display starts | display runs |
|---|---|---|
| 320x250, 320x256 | 21.48% | 62.50% |
| 640x250, 640x256 | 21.68% | 62.50% |
| 1056x250, 1056x256 | 18.62% | 68.75% |
| 768x288 | 15.43% | 75.00% |

The row states the `320`/`640` layout, which four of the seven share exactly.
The other three overrun: `1056` loses 4.2% of its picture width at the left and
4.9% at the right, `768x288` 8.1% and 8.6%. **Overrun rather than a black bar is
the direction chosen**, and the controls recover either way -- but a tier
covering a family this wide cannot be flush for all of it, and what would split
it is a measurement the chip cannot make.

## Placement is measured from the HSync leading edge

Every share above is of the line measured from the HSync leading edge.
Translating one into `IF_HB_ST2`/`IF_HB_SP2` needs the capture write origin.
`SyncProcessor::RetimeOriginSamples` is the engine's term for it and
`docs/known-issues.md` carries what is still unresolved in it -- a scaler term
that moves with the divider, and a source term that differs between two modes
nothing on the chip can tell apart. **A placement rule is only as good as its
origin**, so a tier's window has to be judged against the picture, not against
the arithmetic that produced it.

**THE VERTICAL ORIGIN IS A COUNT OF COUNTER UNITS AND DOES NOT DOUBLE.**
`VideoSourceLine::DoubledFrameOriginUnits` is 10 against
`FrameOriginUnits` 7, and the correction lives on the counter rather than in a
row of the table, because a row states a raster and the origin is the chip's.
Carried in the source's own lines instead it doubles with the counter and opens
a doubled source's window two lines early -- measured as 10 rows of the source's
top border shown at the top of the emitted frame and the last two lines of the
picture lost off the bottom.

## What the tier measures, at `X320 Y256 C256 F50`

`PATTERN CARD` draws a one-pixel frame on the source's outermost pixels, so
`card_edges.py` reads where the source's first and last drawn pixel land on the
emitted frame. Against the generic tier the same source read 1685x1033 of
1920x1080 with the source's border and blanking shown as black at every edge.

| | generic tier | Acorn tier |
|---|---|---|
| capture window | `oh 129 eh 951` of 1101 | `oh 237 eh 688` |
| | `ov 38 ev 582` of 624 | `ov 72 ev 512` |
| emitted picture | 1685 x 1033 | **1895 x 1076** |
| card's frame, top | row 47 | **row 2** |
| card's frame, bottom | off the panel | **row 1077** |
| card's frame, left | column 200 | **column 0** |
| card's frame, right | column 1588 | off the panel by ~22 columns |

**The right-hand edge is not the tier's.** It is the horizontal capture origin,
which `docs/known-issues.md` carries open at both ends of the cable: the same
measurement on `800x600@60` and `640x480@60` loses the right edge too, and those
take their window from the DMT rows this change does not touch. What the doubled
path wants there is `InputFormatter::LineDoubleReset` 147 where the constant is
160, deliberately split with the 60 Hz modes' 175.

Read `hdmi_capture.borders()` with care on this source: an isolated dim blob at
columns 1880..1899, peak luma 44.8, puts the reported right margin at 35 where
the content ends at column 1588 and the true margin is 331.
