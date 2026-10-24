> **RETRACTED, 2026-10-24.** The headline below rests on a confounded
> instrument. `frame_cells.py` anchors its cell grid to the lit picture, and
> `SP_RT_HS_SP` pans that picture 0.81 output columns per unit -- so the
> readings at 34 and at 68 scored different content, and the cell boundaries
> landed differently on a grating fine enough for that to decide the answer.
>
> Re-measured with the grid anchored to the picture: the two values are within
> 1.2x on the worst cell and 34 is about twice as good on the bulk. **68 is
> `ntsc_1280x720`'s preset constant, not a derivation**, and it puts the card
> 27 columns left -- clipped at the left, blanking at the right.
>
> What survives is at the foot of this page, under *What was measured
> afterwards*. The rest is kept because the refuted models are still refuted,
> and because the confound is worth seeing: it is the same shape as every
> other measurement here taken from a moving origin.

# The pass-through shimmer is the capture counter's origin

A sub-pixel horizontal shimmer on `vga` at 1280x720@60 in pass-through, read as
flicker on the test card's finest grating rather than as movement. It is
`SP_RT_HS_SP` — the capture counter's origin — and the clock is innocent.

Measured with `frame_cells.py`, 100-frame clips captured 1:1 off the dongle at
1280x720 so nothing is resampled, scoring the cell grid's worst contrast
modulation. Automation frozen, the source's `MODE` re-issued between readings so
each one follows a real re-lock, `PA_SP_S` held at 30 throughout.

| `SP_RT_HS_SP` | worst cell | where |
|---|---|---|
| 34, which the engine computes | 0.0690, 0.1368 | the finest wedge block |
| 68, which the reference build writes | **0.0075, 0.0109** | off the wedge entirely |
| 2049 | no picture at all | — |

Nine to thirteen times, on one register, with the divider, the oversampling
ratio, the VCO, the charge pump and the sampling phase all held.

## Why a whole-picture instrument cannot see it

The disturbance is confined to the finest block of PM5544's frequency wedge —
about one 64-pixel cell of a 1280x720 frame. `picture_jitter.py` reports four
bands each spanning the full width, so that cell contributes a few per cent of
the reading: the same clip reads 0.019 whole-picture against 0.075 at the cell.

Two channels are needed, not one. A cell adjacent to the worst flags on contrast
modulation and not on displacement, because detail sampled near the limit does
not move when the grid shifts under it — its amplitude changes instead. A
displacement-only instrument is blind to half of it.

The gate on contrast is load-bearing. A flat cell's spatial spread IS its noise,
so over the card's white circle the modulation ratio runs to 0.15..0.28 — higher
than the grating that is genuinely shimmering. Ranked on the ratio alone, the
still part of the picture wins.

## Why the grating and not the rest

720p60 is the worst row in `../sampling-table.md`: 1650 source clocks a line,
`PLLAD_MD` 2038, so 1.24 samples per source pixel against the 2.0 that would
carry arbitrary per-pixel content, and `converted / 2px` of 0.62 where 1.0
clears Nyquist. The finest grating's fundamental sits at about 81% of the
sampler's Nyquist, which makes that cell an amplifier for any movement in when
the line is sampled. Nothing else in the frame is near enough the limit to show
it.

## What is refuted

**The sampling clock, in every term.** Holding the raster and changing only the
ADC clock, four readings per state:

| | ratio | `FS` | VCO | picture median | worst cell |
|---|---|---|---|---|---|
| md 2038 | 1 | 0 | 91.7 MHz | 0.0014..0.0017 | 0.0708..0.0843 |
| md 1740 | 1 | 1 | 156.6 MHz | 0.0097..0.0102 | ~0.15..0.24 |
| md 1740 | 2 | 1 | 156.6 MHz | 0.0094..0.0105 | 0.129..0.168 |
| md 1444 | 2 | 0 | 130.0 MHz | 0.0012..0.0013 | 0.0199..0.0285 |

The two md 1740 rows are indistinguishable, which was read as **the
oversampling ratio changing nothing** about the shimmer. **That reading does
not survive the retraction at the head of this page and is withdrawn**, in
neither direction: the ratio moves the capture origin by
`UndecimatedOriginSamples`, 16 ADC samples, added at ratio one and not at
ratio two, so those two rows had the picture panned 16 samples apart and the
cell grid scored different content -- the same confound the nine-to-thirteen
figure fell to. Scoring it honestly wants a per-pixel measure, and
`/sampleclock?os=` did not move `PLLAD_CKOS` when it was tried on 2026-10-24,
so nothing has measured it since. The high-VCO rows look like a gain effect and are
not one: the reference build runs **167.1 MHz with `FS` 1** and reads picture
median 0.0019, worst 0.0253..0.0272 over four re-locks. Both ratio 1, both
`FS` 1, VCOs within 7%, and five times apart — so neither the VCO nor the gain
bit separates them.

**The charge pump.** Three interleaved passes, 50 uA to 1 mA. At md 1444 the
loop improves monotonically to the field's maximum (0.0074 down to 0.0010),
which says it is current-starved; at md 1740 it is flat from 100 uA upward and
never beats 0.0079. No pump setting recovers the bad state, and a single-pass
sweep that showed a minimum at 500 uA did not reproduce.

**A low VCO gain at a high VCO.** Forcing `FS` 0 at 156.6 MHz does not lock:
`STATUS_SYNC_PROC_HTOTAL` reads 92 counts under the divider.

**The sampling phase search.** `PA_SP_S` lands bimodally — the phase scan's
heavy-dither band centres on 13 or on 22 with nothing between, and the search
picks the band's antipode every time, 29 or 6. That is correct behaviour, and
the two outcomes differ 7.2x in picture quality over 183 acquisitions. Relaxing
the scan's clean threshold from exact to a dither of 1, 3 or 9 changes the
chosen band in zero of 118 scans. `PA_SP_S` is a marker of which state the lock
fell into, not a cause: the register is held at 30 across the origin readings
above and the 9x remains.

## What a scan says and does not say

The scan carries no signal about quality: clean count correlates with the score
at r = −0.10 and +0.08 within each band, and the two bands have the same mean
clean count, 17.8 against 18.6. What does predict the band is whether the leg
changed input — on `vga-pass`, 85 of 89 input-changing acquisitions landed the
bad band against about a third otherwise. The same split reverses on
`vga-scaled-720p`, so it is not a general law about input changes.

## The computed origin, and the one that works

`retimeStopFor()` does **not** silently decline a narrow pulse. It wraps:

```
stop = (divider + origin - sync) % divider
```

With `RetimeOriginSamples` 63, `UndecimatedOriginSamples` 20 and a measured sync
of 49 samples, that is `(2038 + 83 - 49) % 2038 = 34` — exactly what the
register holds. The value is computed, not left behind.

It is the origin that is wrong. The header states 63 as measured rather than
derived, "across four states at oversampling ratio two, all sampling about 1.12
ADC samples for every pixel the source sent". This mode samples 1.235, and
`the-capture-origin-varies-by-mode-at-one-line-rate.md` already records that the
origin moves between modes at one line rate.

The lag term that exists to adapt it is **disabled on this route**:
`HdBypass::applyPassThroughSampling()` passes `0.0f` for `pixelSamples`, so
`originVideoLagSamples()` returns 0. Passing the real figure does not close the
gap either — `4.4 * (1.235 - 1.12)` is one sample against the thirty-four
wanted. **No derivation yet produces 68**, and that is the open question.

## The silent writer

```cpp
void SyncProcessor::writeRetimeStop(uint16_t samples)
{
    if (samples == 0) return;
    SP_RT_HS_SP::write(samples);
}
```

`retimeStopFor()` returns 0 for `!pulse.isPulse() || divider == 0`, and the
writer then leaves whatever the register held, with nothing said. The register
places the whole capture window, so a source whose pulse is not measured is
framed from the previous source's origin and no reading distinguishes that from
a value chosen for it.

## Reading the two builds side by side

`be58a24f4` instrumented, on the same source, is steady where this fork is not:
`PA_SP_S` 8 on every acquisition, worst cell 0.0253..0.0272 over four, and its
worst cell is not the grating. Its sync-processor group differs in more than the
origin.

| | `be58a24f4` | this fork |
|---|---|---|
| `SP_RT_HS_SP` | 68 | 34, computed |
| `STATUS_SYNC_PROC_HLOW_LEN` | 47 | 52 |
| `SP_H_PULSE_IGNOR` | 255 | 99 |
| `HD_HSYNC_RST` | 1023 | 2046 |
| `PLLAD_MD` / `KS` / `CKOS` / `FS` | 1856 / 1 / 1 / 1 | 2038 / 0 / 0 / 0 |
| `PA_SP_S` | 8, fixed | searched, bimodal |

`HD_HSYNC_RST` 1023 against a divider of 1856 is the other unexplained
difference: the reference plays out about half the samples per line that it
takes, where this fork sizes the channel raster to the divider.

## What the instruments were

`frame_cells.py` is the cell grid and the two channels. `transition_soak.py`
takes the transition hundreds of times unattended and scores each acquisition,
which is what turned an intermittent symptom into a distribution — 891 legs,
and the `PA_SP_S` split rests on 183 of them. `tools/reference-build/` and the
images in `~/Projects/gbsc-pro-backups` are what made the build comparison one
flash rather than a rebuild.

## What was measured afterwards, 2026-10-24

**The register pans the capture, and that is most of what it does.** One unit
is one ADC sample, `1 / samplesPerPixel` source pixels. Read off the card's
border checks at 1280x720@60, only 33..36 framed the card with nothing lost at
either end, and the engine's computed 34 was inside it. The framing bar is now
`tools/gbsc-pro-hwtest/test_passthrough_acceptance.py`.

**The sampling phase changes how much of the grating SURVIVES, not how much it
moves.** `PA_ADC_S` swept at one acquisition of 1280x720@60, measured per
pixel over the wedge with no cell grid to align:

| `PA_ADC_S` | 8 | 16 | 20 | 24 |
|---|---|---|---|---|
| wedge contrast, grey levels | 45.4 | 54.9 | 56.4 | 55.7 |
| movement, grey levels | 5.2 | 4.6 | 5.3 | 5.7 |

Repeated, the contrast figures returned 45.4 and 56.4 to within 0.06. So the
phase is worth about 20% of the finest grating's amplitude, and the movement
term does not follow it.

**An earlier reading that the optimum is 8, stable over five re-locks, is
withdrawn.** It scored the worst 64px cell, and two runs of the same phases at
the same origin, divider and sync width ranked them in opposite orders purely
by where the grid fell. A grating near the sampler's Nyquist cannot be scored
on a grid that is not anchored to it.

**`PA_ADC_S` is never tuned.** `Adc::acquirePhase()` sets it to
`oversample == 4 ? halfSampleOn(MidField) : MidField` -- the middle of a
five-bit field, which is not a sampling instant. Only the sync processor's
phase is searched. The optimum measured across three densities was 0 at
2.000 samples per source pixel, 24 at 1.500 and 8 at 1.235, on the cell metric
since withdrawn; the densities are measured from the pan slope rather than
assumed from DMT.

**Why the phase matters below two samples per pixel and not above.**
`the-sampling-phase-cannot-reach-the-shimmer.md` swept all 32 steps at
320x256@50 on the scaling path and found nothing, with a sound argument: the
line-doubled grid is 1100 samples against 512 source pixels, so the sampler
already visits 275 positions within the source pixel and a global offset moves
them together. That mode samples 2.15 per pixel. 720p60 in pass-through
samples 1.235, below Nyquist, which is where a beat exists for a phase offset
to move. Both measurements stand.
