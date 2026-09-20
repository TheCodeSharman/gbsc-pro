# The band down the left on composite sync is output-side

The grey band between the panel's left edge and the picture, seen on the bench
RiscPC with `SYNC 1`, is not captured picture and not the clamp. It is removed
by `VDS_DIS_HB_SP`, with the picture unmoved.

It is a different object from the bar at the right-hand edge, which is captured
line tail and is removed from the input side.
[the-hbin-start-blanks-the-captured-tail.md](the-hbin-start-blanks-the-captured-tail.md)
describes that one. The two look alike and do not share a mechanism.

## Reproduction

`printf 'SYNC 1\n' | nc 192.168.88.10 6502` at `MODE X320 Y256 C256 F50` on
`vga`, default framing. The engine settles at `STATUS_SYNC_PROC_VTOTAL` 308
against the source's 311 and `PLLAD_MD` 2050 against the 2206 the separate-sync
leg solves, and the picture is narrower and further right. The band fills what
the picture no longer covers.

On the separate-sync leg the picture is wider and reaches the panel edge, so
there is nothing to see. **The band is only visible because the csync solve
produces a smaller picture**, which is the fault to chase rather than the band.

## It does not follow the source

Swapping the source pattern changes the picture by 86 grey levels and the band
by under 10, measured as a column mean over rows 100..620 of a rectified
`tv-snap`:

| region | `PATTERN PM5544` | `PATTERN CARD` |
|---|---|---|
| band, photo columns 90..110 | 150 | 160 |
| band, photo columns 120..140 | 92 | 83 |
| picture, photo column 300 | 162 | 76 |

That is the test the right-edge bar passes and this one fails: that bar follows
the source, so it is captured content.

## It does not follow the clamp

`SP_CS_CLP_ST` swept 14, 140, 152, 164, 176, 188, 200, 212 with the stop 60
counts above it, every write read back to confirm it landed, automation frozen,
the engine's own 14/76 repeated at the end as a control. The band is present at
every value and its level moves less between treatments than between the two
controls.

**The clamp does reach the colour, and its upper bound is where active video
starts.** At 164..224 the picture is clean. At 212..272 the greys take on the
colour of whatever is beside them and the whole picture washes out -- the clamp
is sampling picture rather than back porch, so the black reference follows the
image. The sync pulse measures `STATUS_SYNC_PROC_HLOW_LEN` 144 of
`STATUS_SYNC_PROC_HTOTAL` 2050, so the back porch is the narrow window between
144 and roughly 200 on this source.

## `VDS_DIS_HB_SP` removes it, and the picture does not move

`VDS_DIS_HB_SP` 113 -> 300 blanks the band away completely. The card's left
border stays at the same photo column, so the window is being closed over
something that is not the picture.

The engine's 113 is the write start its own model predicts: `VDS_HB_SP` 8 plus
`55 + 25m` at a magnification of `1024 / VDS_HSCALE` = `1024 / 513` = 1.996
gives 112.9. So the window opens where the model says the first written pixel
lands, and roughly 200 output pixels of non-picture stand beyond it. Either the
write-start model does not hold on this leg, or the capture window starts before
the source's active video and what stands there is captured input blanking that
the clamp never referenced to black.
[scaler-geometry-model.md](../scaler-geometry-model.md)

## The csync clamp is computed from a register that rails

`SyncProcessor::acquireClampWindow()` takes its line length from
`clampLineLength()`, which reads `HPERIOD_IF` on the csync branch and
`STATUS_SYNC_PROC_HTOTAL` on the separate branch, and applies a different
fraction pair to each. The two land in the same place as a fraction of the line
-- 0.67% and 1.0% of a line whose sync pulse is 7% -- so **both branches place
the clamp inside the sync pulse**, and the separate-sync leg has a clean picture
doing it.

What the csync branch does carry is the dependency. `HPERIOD_IF` was observed at
4, 431 and 511 in one session, and at 4 the window collapses to 1..2. **Which
sync leg a reading came from does not predict it** -- `SYNC` re-applies the mode,
so reaching the csync leg performs the mode change that clears the fault, and a
comparison across legs compares two instances rather than two configurations.
The register the engine already refuses to measure the line rate from is the one
this computation trusts.
[hperiod-if-railing.md](hperiod-if-railing.md)
