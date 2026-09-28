# The capture origin varies by mode at one line rate, and the unit is unsettled

`SyncProcessor::RetimeOriginSamples` is 63 ADC samples, added to the retime stop
the input formatter's line counter takes its origin from. Measured against the
mode file at full framing, undoubled modes want anything from 62 to 91, and the
spread has been read as a function of the line rate.

**It is not the line rate alone**, and what the varying quantity IS has not been
established. This page is the controls that narrow it and the two traps that
have produced wrong answers here.

## The line-rate model is refuted

Five H-negative modes, all at 31.47 kHz and 59.9 Hz, all solved at `PLLAD_MD`
1442 and so at one sample rate of 45.38 MHz, spanning a four-fold range of
samples per source pixel. `full_margins.py` at full framing, `PATTERN CARD`,
`ANIM OFF`:

| mode | htotal | samples/px | `HLOW_LEN` | d px | d samples |
|---|---|---|---|---|---|
| 1280x480 | 1600 | 0.901 | 167 | +3.1 | +2.8 |
| 720x480 | 858 | 1.681 | 100 | +2.7 | +4.6 |
| 640x480 | 800 | 1.803 | 169 | +2.4 | +4.3 |
| 360x480 | 532 | 2.711 | 170 | +3.4 | +9.1 |
| 320x480 | 400 | 3.605 | 148 | +3.3 | +11.8 |

One line rate, one field rate, one polarity, one divider, one sample rate — and
nine samples of disagreement. Whatever varies the origin, the line rate is not
it, and the sync width runs from 100 to 170 samples across the set without
ordering the result either.

## TRAP: these readings carry an anchor, and it is not a constant in source pixels

`full_margins.py` maps dongle column zero to the capture window's start.
`counter_origin.py` creeps the window's own start until the feature leaves and
so carries no anchor. On three 60 Hz modes:

| mode | anchor-free | against the mode file | difference |
|---|---|---|---|
| 1024x768@60 | +0.1 | +2.1 | 2.0 |
| 800x600@60 | +5.1 | +8.2 | 3.1 |
| 1280x960@60 | +15.0 | +17.7 | 2.7 |

**The anchor is about 2.6 ADC samples**, and it is constant within one output
raster — so an anchored reading is sound for comparing two modes at one raster,
and the fifteen-sample spread beside it is the counter's own.

But 2.6 samples is 2.89 SOURCE PIXELS at 0.901 samples/px and 0.72 at 3.605.
**So leaving it in flattens the `d px` column and manufactures a constant that is
not there:**

| | relative spread across the five |
|---|---|
| raw, source pixels | 14.1% |
| raw, ADC samples | 57.9% |
| anchor removed, source pixels | 68.0% |
| anchor removed, ADC samples | 96.3% |

With it removed neither unit is constant, and **no claim that the correction
belongs in source pixels rather than ADC samples survives this table.** Subtract
the anchor before converting an anchored reading to source pixels, or do not
convert it.

## The twins are indistinguishable to the chip and still differ

`1600x600@60` is `800x600@60` with every horizontal field doubled at twice the
pixel clock, so every one of them is the same TIME. From the mode file:

| | 800x600@60, 40 MHz | 1600x600@60, 80 MHz |
|---|---|---|
| sync | 128 px = 3.2000 us | 256 px = 3.2000 us |
| line | 1056 px = 26.400 us | 2112 px = 26.400 us |
| active starts | 432 px = **5.4000 us** | 216 px = **5.4000 us** |
| polarity, field rate | 0, 60.32 Hz | 0, 60.32 Hz |

**Every timing the TV5725 can observe is identical.** It solves the same
`PLLAD_MD` 1442 and the same retime stop, its sync processor sees the same
waveform, and the filed lead is the same 294.1 samples for both. The only
difference is that it samples a faster pixel clock. So the two pictures must
land in the same place.

They do not. `d samples` reads **+8.2 against +5.8**, and the anchor is
common-mode across a pair at one output raster so it cancels out of that
comparison. Each mode repeats to 0.3 to 0.4 samples across runs, so the 2.4
samples between them is outside the noise.

**That 2.4 samples is the open anomaly of this page.** A pixel-clock explanation
of it is SUSPECT and unverified: nothing that reaches the retiming can see the
source's pixel clock, and a source-side delay common to the source's own sync
and video cancels, because the scaler times video from the sync edge. An
explanation would need the source's sync and video paths to differ in depth, and
nothing here measures that.

**Verify the 2.4 samples before building on it.** The cheap check is to repeat
the pair several times in one session with `counter_origin.py`, which carries no
anchor at all, rather than with the anchored instrument used here.

## What the other controls rule out

Each is a control rather than a curve: the quantity named is the only one moving.

**The sample rate.** The 60 Hz pair samples at 54.5 MHz and the 75 Hz pair at
54.7 MHz, and they read 21 samples apart.

**The back porch.** `800x600@75` and `1600x600@75` share a sync waveform and a
line period but not a back porch — 3.23 against 1.78 us — which the filed lead
accounts for.

## The measured pulse is biased by polarity

`STATUS_SYNC_PROC_HLOW_LEN` against the mode file's own sync width at the divider
in force, nine modes:

| sync | bias in samples | in time |
|---|---|---|
| H-negative | −5.4, −3.1, −3.2, −4.4 | −77.6, −68.3, −73.2, −62.6 ns |
| H-positive | +1.7, +1.7, +2.4, +2.3, +2.1 | +31.2, +31.1, +26.0, +26.6, +26.7 ns |

Both are tighter as a time than as a count — 11% against 29% on negative sync,
9% against 17% on positive — so this is a delay rather than a counting error, and
the two polarities are about 98 ns apart. `retimeStopFor()` subtracts this
reading, so it reaches the origin directly, and a positive-going source is
measured through an extra inversion in `SP_HS_INV_REG`. At two to five samples
it is not the whole spread.

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
- **`d px` and `d samples` answer different questions**, and the anchor converts
  between them differently per mode. Say which unit a figure is in, and whether
  the anchor is still in it.
- **`counter_origin.py`'s vertical creep reports its own failure** by disagreeing
  with its expectation. `1024x768@60` crept 4..39 expecting 29 and crossed at
  5.5, which is the run-up clamped away at the floor rather than a reading.
