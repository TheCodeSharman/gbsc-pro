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

## Why the generic tier is what the bench RISC PC gets today

The envelope spans **11.7% to 98.1%** of the line, taken from what real sources
put there, so nothing is cropped and what is captured beyond the picture is
black. On the bench mode that is exactly what the engine solves: `oh 129`,
`eh 951` of `ch 1101` is 11.7%..98.1%.

`X320 Y256 C256 F50` matches no standard raster, so it falls to that tier and
the window takes in both porches and both borders. The picture is then a little
over half of what is captured, which is the convenience the Acorn tier is for.

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

The second is the safer default and the first is the one that "fills the
screen". The choice is the tier's to state, per mode, because the border is a
per-mode authoring decision with no relationship to line rate -- 3.1% to 22.0%
across `AKF50`.

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
share a share-of-line layout and one entry serves both. Where two Acorn modes
share a raster and differ in layout, the tier must either agree on one answer or
decline and fall through.

## Placement is measured from the HSync leading edge

Every share above is of the line measured from the HSync leading edge.
Translating one into `IF_HB_ST2`/`IF_HB_SP2` needs the capture write origin.
`SyncProcessor::RetimeOriginSamples` is the engine's term for it and
`docs/known-issues.md` carries what is still unresolved in it -- a scaler term
that moves with the divider, and a source term that differs between two modes
nothing on the chip can tell apart. **A placement rule is only as good as its
origin**, so a tier's window has to be judged against the picture, not against
the arithmetic that produced it.

## What would show the tier is right

The card's own frame is the feature to measure, because it is drawn at a known
share of the source's active width. `picture_jitter.py` reports each frame's lit
edges and `card_edges.py` reads the card's frame directly; a tier that matched
should put the source's outermost drawn pixel at a predictable place, and the
comparison is against the same mode captured under the generic tier.

Read `hdmi_capture.borders()` with care on this source: an isolated dim blob at
columns 1880..1899, peak luma 44.8, puts the reported right margin at 35 where
the content ends at column 1588 and the true margin is 331.
