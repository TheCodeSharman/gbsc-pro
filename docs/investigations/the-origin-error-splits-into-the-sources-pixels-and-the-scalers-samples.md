# The origin error splits into the source's pixels and the scaler's samples

Measured against the mode file at full framing, undoubled modes put their picture
2 to 42 source pixels further along the line than their published raster states,
and the spread has been read as a function of the line rate.

It is two terms, and only one of them is the scaler's.

## A term constant in SOURCE PIXELS cannot be the scaler's

The chip sees sync edges and analog video. **The source's pixel clock is nowhere
in the signal** — the horizontal axis has no native resolution, and how many
samples a line is cut into is `PLLAD_MD`, which is ours. So nothing inside the
TV5725 can produce a delay that holds constant in the source's pixels while the
sampling density moves, because nothing inside it can measure that unit.

A term in that unit is therefore upstream of the chip, and it has to come off
before anything the engine does is judged.

## The measurement

`full_margins.py` at full framing, `PATTERN CARD`, `ANIM OFF`. The lead is the
black before the card's green frame, against the mode file's sync + back porch +
left border. `d px` is in source pixels and `d samples` is the same reading
through the mode's own samples per source pixel, which is `PLLAD_MD / htotal`.

Five H-negative modes, all at 31.47 kHz and 59.9 Hz, all solved at `PLLAD_MD`
1442, spanning a **four-fold range of samples per source pixel**:

| mode | htotal | samples/px | `HLOW_LEN` | d px | d samples |
|---|---|---|---|---|---|
| 1280x480 | 1600 | 0.901 | 167 | **+3.1** | +2.8 |
| 720x480 | 858 | 1.681 | 100 | **+2.7** | +4.6 |
| 640x480 | 800 | 1.803 | 169 | **+2.4** | +4.3 |
| 360x480 | 532 | 2.711 | 170 | **+3.4** | +9.1 |
| 320x480 | 400 | 3.605 | 148 | **+3.3** | +11.8 |

14% spread in source pixels against 58% in ADC samples, and the sync width runs
from 100 to 170 samples across the set without moving either figure. By the
argument above this whole family is the SOURCE's term, and the scaler's is zero
here.

**The source emits its active video about 3 of its own pixels after the mode
file's nominal start.** The card is not drawn inset — `h active` reads +0.0 on
every mode, so the two green edges are exactly `active - 1` apart and the frame
is on the framebuffer's outermost pixels. It is the whole active region that is
late.

## The twins are what prove the split

Two modes whose horizontal timings are the same multiple throughout present an
identical sync waveform, so the engine solves the same divider and the same
retime stop and the sync path cannot tell them apart. Only the pixel clock
differs, by exactly two. `docs/bench-sources.md`.

`800x600@60` against `1600x600@60`, 40.0 against 80.0 MHz:

| | d px | d samples |
|---|---|---|
| raw | +6.1 / +8.5 | +8.2 / +5.8 |
| less 3.0 source pixels | 3.1 / 5.5 | **+4.2 / +3.7** |

Raw, the twins agree in neither unit. Take a source-pixel constant off and they
agree in **ADC samples** — the unit the chip works in. Nothing was fitted to get
that: the pair is matched by construction, and only one decomposition makes it
consistent.

## What is left is positive-sync only

The residual, in ADC samples, after the source's 3.0 pixels:

| sync | modes | line rate | residual |
|---|---|---|---|
| H-negative | 640x480, 720x480, 320x480, 1280x480, 360x480 | 31.5 kHz | −0.5 .. +1.1 |
| H-negative | 1024x768@60 | 48.4 | −1.1 |
| H-positive | 800x600@60, 1600x600@60 | 37.9 | +4.2, +3.7 |
| H-positive | 1280x1024@60, 1280x960@60 | 64.0, 60.0 | +14.3, +15.2 |

**`SyncProcessor::RetimeOriginSamples` at 63 is already right on negative sync**,
over six modes and two line rates. The defect is a positive-sync one, it grows
with line rate, and it is in ADC samples — a unit the chip can observe, so it is
correctable.

## The measured pulse is biased on the same boundary

`STATUS_SYNC_PROC_HLOW_LEN` against the mode file's own sync width at the divider
in force, nine modes:

| sync | bias in samples | in time |
|---|---|---|
| H-negative | −5.4, −3.1, −3.2, −4.4 | −77.6, −68.3, −73.2, −62.6 ns |
| H-positive | +1.7, +1.7, +2.4, +2.3, +2.1 | +31.2, +31.1, +26.0, +26.6, +26.7 ns |

Both are tighter as a time than as a count — 11% against 29% on negative sync,
9% against 17% on positive — so this is a delay rather than a counting error, and
the two polarities are about 98 ns apart. `retimeStopFor()` subtracts this
reading, so it reaches the origin directly. At two to five samples it is not the
whole residual, but a positive-going source is measured through an extra
inversion in `SP_HS_INV_REG`, and that the pulse bias and the residual separate
on the same boundary is the reason to look there first.

## What the other controls rule out

Each is a control rather than a curve: the quantity named is the only one moving.

**The pixel clock and the back porch.** The twin pairs above, and the same
pairing at 75 Hz — 49.5 against 99.0 MHz, back porches 3.23 against 1.78 us.

**The sample rate.** The 60 Hz pair samples at 54.5 MHz and the 75 Hz pair at
54.7 MHz, and they read 21 samples apart.

**The line rate, as the sole variable.** The five modes at the head of this page
share one and still disagree by nine samples.

**The measurement's anchor.** `full_margins.py` maps dongle column zero to the
capture window's start; `counter_origin.py` creeps the window's own start until
the feature leaves and so carries none. On three 60 Hz modes the two differ by a
constant:

| mode | anchor-free | against the mode file | difference |
|---|---|---|---|
| 1024x768@60 | +0.1 | +2.1 | 2.0 |
| 800x600@60 | +5.1 | +8.2 | 3.1 |
| 1280x960@60 | +15.0 | +17.7 | 2.7 |

The anchor is worth about 2.6 samples and is constant within one output raster,
so the spread beside it is the counter's own — and the cheaper instrument is
valid for comparisons taken at one raster.

## What has not been established

Why the positive-sync residual grows with line rate. Three points carry it,
+4.0 at 37.9 kHz and +14.8 at 60 to 64 kHz, which is two clusters rather than a
mechanism. A fourth positive-sync line rate between them is what would say
whether it is a line; `1600x1200@60` at 75.0 kHz and `1024x768@75` at 60.0 kHz
are both in the monitor definition.

Whether the source's 3 pixels are VIDC20's output pipeline or the mode file's
convention for where active starts is not separated here, and does not need to
be: either way it is the source's and the scaler cannot see it.

## Traps

- **`full_margins.py` needs both of the card's green edges** to measure its
  ruler. A mode whose picture overruns the emitted frame has only one, and the
  lead is then read from the near edge with the ruler computed instead —
  `units / htotal`, which agrees with the separation to 0.10% on every mode where
  the pair is found.
- **The 75 Hz output raster does not give a repeatable reading.** 1125 lines at
  75 Hz is not a standard mode, and `800x600@75` read +8, +29, +17 and +15
  samples on four acquisitions while `1600x600@75` twice lost the picture off the
  right of the frame. Every 60 Hz mode here repeats to 0.3 samples.
- **`d px` and `d samples` answer different questions**, and the split above is
  exactly why. Say which unit before comparing two modes.
- **`counter_origin.py`'s vertical creep reports its own failure** by disagreeing
  with its expectation. `1024x768@60` crept 4..39 expecting 29 and crossed at
  5.5, which is the run-up clamped away at the floor rather than a reading.
