# The retime stop is the counter's origin, and 93% is only right at a 7% duty

`SP_RT_HS_SP` decides where the input formatter's line counter takes its origin.
The engine wrote `0.93 x PLLAD_MD` on every source. That fraction is `1 - duty`
for a source whose sync pulse takes 7% of the line, and for nothing else — so
every source with a different duty has its whole capture window placed wrong, by
an amount no register dump can see.

The rule that reproduces the source's published raster is

```
SP_RT_HS_SP = PLLAD_MD - STATUS_SYNC_PROC_HLOW_LEN + 63
```

— the retimed pulse laid on top of the incoming one, less a fixed offset in ADC
samples between the window's stop and the origin the counter actually takes.

## How it is measured

At full framing the capture window spans the whole capturable line, so the
emitted frame carries the source's own blanking at both ends: the black before
the picture is the back porch and the black after it is the front porch. Walking
`SP_RT_HS_SP` moves the picture one capture unit per register unit, and the value
that puts BOTH porches on the standard's own figures at once is the answer.

1024x768@60, `PLLAD_MD` 1440, against DMT's sync+back 296, active 1024, front 24:

| `SP_RT_HS_SP` | sync+back | active | front |
|---|---|---|---|
| 1339 — `0.93 x MD` | 316.1 | 1020.7 | 8.2 |
| **1360** | **295.9** | **1025.7** | **23.3** |
| 1400 | 258.7 | 1025.7 | 60.5 |
| 1440 — `MD` | 222.1 | 1025.1 | 97.7 |

The active width reads 1025 against the stated 1024 at every setting, which is
what says the instrument is sound rather than the answer flattering.

Four states, the divider held away from its own choice on three of them, with
`HLOW_LEN` read rather than derived:

| source | `PLLAD_MD` | `HLOW_LEN` | stop that framed the raster | offset |
|---|---|---|---|---|
| 1024x768@60 | 1440 | 141 | 1360.0 | 61.0 |
| 1024x768@60 | 1200 | 118 | 1144.6 | 62.6 |
| 1024x768@60 | 960 | 94 | 929.6 | 63.6 |
| 640x480@60 | 1444 | 169 | 1338.5 | 63.5 |

The offset is constant to ±1.5 against a crossing interpolated from 8..12 unit
steps and an `HLOW_LEN` that itself wobbles ±1.5. It is **not** a function of the
divider: 1440 and 1444 give 61.0 and 63.5.

## Why a percentage cannot be the form

Holding one source and moving only the divider gives `MD - SP` of 80.0, 55.4 and
30.4 at `MD` 1440, 1200 and 960 — a straight line with a non-zero intercept. A
fixed count would give 80 at all three and a fixed percentage 53.3 at 960.
Rewritten against the measured sync width, which scales with the divider on a
fixed source, the same three points are one sync width less a constant.

## It predicts the error on every mode

The band at 1024x768@60 and its absence at 640x480@60 are the same defect seen at
two duties. What the engine wrote against what the rule wants, with the resulting
displacement of the source's active start:

| mode | engine wrote | rule wants | predicted | measured |
|---|---|---|---|---|
| 640x480@60 | 1342 | 1338.5 | −2.5 px | −1.8 |
| 800x600@60 | 1337 | 1325 | −8.8 px | −4.5 |
| 800x600@72 | 1123 | 1130 | +6.0 px | +11.1 |
| 1024x768@60 | 1339 | 1360 | +19.6 px | +22.0 |

640x480@60 frames flush because at an 11.8% duty the inherited fraction lands
within three samples of correct. 1024x768@60 does not because at 9.8% it is 21
samples out.

## The 63 is measured and not derived

It is the one number here with no account of itself. What is known:

- It is a count of **ADC samples**, not a fraction and not a time: it holds
  across a 480-unit range of divider on one source and across two sources whose
  dividers match to four units.
- **All four states run oversampling ratio two, and ratio one wants 16 more.**
  The decimators are out of circuit there and the captured video lands that much
  later in the counter. `SyncProcessor::UndecimatedOriginSamples` carries it.
  [`the-capture-origin-varies-by-mode-at-one-line-rate.md`](the-capture-origin-varies-by-mode-at-one-line-rate.md)
- Both sources are **undoubled**. Whether it is the same count on a doubled line
  is untested, and an IF unit is two ADC samples there.
- Putting it in `SP_RT_HS_SP` rather than downstream is what keeps the counter's
  origin meaning what the rest of the engine already assumes — `CaptureWindow`
  places every window from unit 0, and `SourceTiming`'s table states its start
  columns from the sync pulse's leading edge. Expressed downstream instead it
  needs a SIGNED origin lead, because `VideoSourceLine::originLeadUnits` carries
  video arriving ahead of the origin and this arrives behind it, and it needs a
  doubling-aware conversion the register itself does not.

## What this leaves contaminated

`VideoSourceLine::SeparatorOriginPerThousand` is 70, fitted on composite sync with
the retime wrong by 61 samples on the source it was fitted against. It has to be
re-measured, and how much of its 70 per mille survives is the test of whether the
offset was being counted twice.

`OutputMode::HsyncStartPx` and `OutputMode::TransmittedWindowDelayPx` were tuned
on 640x480@60, where the retime error is about 2.5 source pixels, so they can
have absorbed no more than that. `SourceTiming`'s `VerticalOriginLines` is on the
other axis and a horizontal retime cannot reach it.

## Traps

- **`SP_RT_HS_SP` above `PLLAD_MD` is inert.** The stop sits past the end of the
  line, the picture does not move at any value, and every reading around it is
  frozen — which reads as a control that does nothing rather than as a value out
  of range.
- **A held divider outlives the run that set it.** `/sampleclock?hold=` is not
  cleared by a mode change, so the next measurement runs at the previous one's
  divider and says nothing about the state it names.
- **The transmitted window's latch goes stale when the magnification moves.** A
  divider walk with no re-lock accumulates black at the left that no pan can
  close, and it fits a straight line convincingly enough to look like a pipeline
  delay. Walk with a `PAD_SYNC_OUT_ENZ` toggle at each step, or read only states
  whose link has re-acquired.
- **The card's green frame is unmeasurable below about 1.4 capture units per
  source pixel.** It is one source pixel wide and falls between samples, which is
  most modes once `dividerCeilingForOutput()` binds. Measure the ring's edge
  instead, skipping the first ten columns for the bright artefact that sits ahead
  of any blanking.
