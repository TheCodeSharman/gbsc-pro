# Known issues

Open defects and unsettled questions, each with what was measured and what would
settle it. A row leaves this page when the behaviour is fixed or the question is
answered, and the evidence goes to `investigations/`.

**This is not a work queue.** The refactor's order is
`video-source-acquisition.md`; this page is what is wrong with the machine
regardless of which step is in flight.

## Reaches the picture

### An input change taken while passed through never settles

Pass-through permitted, output already passed through, and the input changed:
the engine sits in its no-sync branch indefinitely -- `m:0`, `s: 0`, the run
pinned -- with the line rate arriving correctly on `HPERIOD_IF` and
`STATUS_SYNC_PROC_VTOTAL` stuck at 97. The route is re-decided only on a settled
measurement, so the state holds itself; `/sc?~` only clears it once the input is
one that can lock. The same change with `preferScalingRgbhv` set completes in
under 40 s in both directions.

The stale quantity is the ADC PLL's crossover row and VCO gain, not the divider:
`dividerFor()` gives 2039 at every rate involved, so the group is set for
99.7 MHz against a source wanting 64.0 MHz and `STATUS_MISC_PLLAD_LOCK` reads 0
throughout.
`investigations/an-input-change-under-pass-through-never-settles.md`.

### 640x480@60 is captured too narrow and magnified to fill

`eh` 623 of `ch` 1023 is 61% of the line where the matched raster puts active
video at 80%. The raster match itself is right -- sync duty 11.50% against DMT's
12.00%, inside the 1.5-point tolerance, and `ov` 35 / `ev` 480 exact -- so the
fault is in what the capture window is solved to, not in which raster was
matched.

### Composite sync re-solves every two to four seconds, and the sink drops it

On the bench RiscPC at 320x256@50 with `SYNC 1`, the sync processor's count
alternates 308/309. Each change is a real source move as far as
`VideoPath::sourceMoved()` is concerned, so it arms a solve, which runs a
1000 ms `own V sync` probe and a re-solve, and the cycle repeats:

```
57.82  source moved: count (309 lines, solved 308)
59.06  own V sync: no after 1001ms
59.67  sampling: 308 lines x 50.56 Hz -> line rate 15625
59.87  source moved: count (309 lines, solved 308)
61.11  own V sync: no after 1000ms
```

The sink reports no signal throughout, while every register reads healthy --
`SP_CS_CLP_ST` 14, `SP_CS_CLP_SP` 76, `HPERIOD_IF` 431,
`STATUS_SYNC_PROC_HTOTAL` 2050, PLL locked. The separate-sync leg on the same
source, mode and cable is stable at 311 with a clean picture.

**It is not always like this.** The same leg carried a picture earlier in the
same session, before the unit had been flashed, with identical clamp and coast
windows. What separates a csync leg that works from one that does not is open;
an OTA flash before it is the one difference so far, and `/sc?~` on the
separate-sync leg is what recovers the unit afterwards.

The steadiness run is what should absorb a one-count alternation, and on the
interlaced Wii it correctly refuses to call such a source acquired at all.
Here the count settles enough to solve and then moves again, which is the
opposite end of the same mechanism.
`../CLAUDE.md`, the mode-detect and steadiness-run notes.

### The clamp window sits inside the sync pulse, on both sync branches

Two instances: 320x256 on composite sync, clamp 14..76 against a 144-sample
pulse; 640x480 on separate sync, clamp 11..65 against a 126-sample pulse. It
reaches the picture -- at a clamp inside active video the greys take the colour
beside them. `SyncProcessor::acquireClampWindow()`'s fractions are the fault and
it is general, not a composite-sync quirk.

### `HPERIOD_IF` rails, and the recovery ladder is not certain

Long-standing and documented in `../CLAUDE.md` and
`investigations/hperiod-if-railing.md`. Worth repeating here only for what a
session needs to know: the rungs are a source mode round trip, an
`ADC_INPUT_SEL` bounce, then a cold boot, and **a rung that fails once may work
on a second attempt** -- measured this way round, a round trip and a bounce both
leaving 511/255 with `STATUS_IF_HT_OK` 0, and a second round trip restoring 431
in 4 of 4 samples.

## Measured wrong, no picture consequence found yet

### `Deinterlacer::steer()` gates on `STATUS_IF_VT_OK`

`STATUS_IF_VT_BAD == 0` was proposed as matching the evidence better. The two
are complementary on this bench -- `VT_OK` 1 / `VT_BAD` 0 in 8 of 8 on composite
sync, `VT_OK` 0 / `VT_BAD` 1 in 5 of 5 on separate -- so neither is the wrong
gate on these readings, and the flicker the proposal rested on has not been
reproduced. Open only in that nothing has exercised the gate: the bench RISC PC
is progressive, so the deinterlacer has nothing to engage for even on the
composite leg where the flag lets it through.

### `DAC_RGBS_ADC2DAC` reads 0 in pass-through

`rgbhv-bypass-trap.md` has it at 1 as one of the two tells that the ADC-to-DAC
route is in force. Measured 0 with `OUT_SYNC_SEL` 1 and a correct full-screen
passed-through picture, so either the tell is wrong or the route is reached
another way.

### `HD_VB_SP` keeps its resting value on an input-change entry

20, which is what `applyVerticalBlanking(0)` leaves, where the Wii's published
raster puts active video at 36. The active start line handed to the bypass
switch was zero on an entry taken by changing input from a scaled `vga`. Which
entries resolve the raster match in time is open.
`video-source-acquisition.md`.

## Costs time rather than correctness

### A mode change into a taller frame stalls seconds in the field-rate spin

Timed with `SamplingLog` at 25 ms across 311 -> 524: **5.0 s of stall in 21
passes**, individual passes taking 914, 794 and 783 ms. `getSourceFieldRate()`
is a blocking spin with no `yield()`, one field period nominal and up to
3 x 250 ms on retries, and `agreedRate()` wraps that in three more attempts. The
reverse change is 2.89 s.

A bounded *poll until `STATUS_IF_HT_BAD` clears* is the candidate replacement:
`HT_BAD` re-locks within 25 ms measured and 1.4 ms nominal, against a 20 ms
blocking spin for the fallback. **The bound is essential** -- on the
separate-sync fault the measurement never converges and the flag stays 1
indefinitely.

**DERIVING THE FIELD RATE FROM THE LINE COUNT IS NOT THE FIX, AND WOULD REMOVE
THE ONLY CROSS-CHECK.** `STATUS_SYNC_PROC_VTOTAL` and `VPERIOD_IF` are both
LINE COUNTS -- RD-5725-1.1 gives the second as "input source V total lines" --
so neither carries a time base and neither states a rate. `HPERIOD_IF` is the
only register that does, as "input source H total pixels / 4" against the 27 MHz
reference. So `fieldRate = lineRate / lines` can only be computed from
`HPERIOD_IF`, which makes it the same reading rearranged rather than a second
one: `lineRateFrom()` already multiplies in that direction.

`measureLineRate()` calls `getSourceFieldRate()` **only** where the counter rate
is refused or uncorroborated -- exactly where `HPERIOD_IF` cannot be trusted.
Substituting an algebraic rearrangement of it there would agree with a railed
counter by construction, `ratesAgree()` would pass, and the railed rate would be
adopted. The fast path already takes no pin measurement at all.

The pin is not used for `VPERIOD_IF`, which is a plain register read. It carries
the FIELD RATE, and FrameSync's input and output vsync sampling, which needs a
phase and an output period that no register reports.

## Dead code whose fate is undecided

### `applyForScalingRgbhv()` and `applyScalingChargePump()` have no callers

Their only call site went with `loadScalingRgbhvPreset()`. Whether each is a
preset-era put-back that dies with the tables or a behaviour to restore is
undecided. `applyForScalingRgbhv()` overlaps `applyForSyncType()` on
`SP_SOG_MODE` and the overflow protect, so wiring it back as-is would put two
owners on those.

## Untried experiments with a known payoff

### The display clock could ask for 129.6 MHz rather than 108

`OutputMode::EngineCeilingHz` is 108 MHz on a usability argument that no longer
holds on its own terms: it rested on the zoom floor landing exactly on the
default framing at `scaleMin` 500, and the floor is now `raster / 4`, leaving
315 units of travel rather than none. 129.6 MHz is already measured as working
and sharp, and buys a third more horizontal resolution. Not tried.
