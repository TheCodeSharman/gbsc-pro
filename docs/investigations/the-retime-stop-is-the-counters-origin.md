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

- **`SP_RT_HS_SP` ABOVE `PLLAD_MD` TEARS THE CAPTURE, and the earlier reading
  that it is inert is REFUTED.** Crept a unit at a time on the bench source at
  `PLLAD_MD` 2200, the picture moves 1.000 ADC samples per unit and stays put to
  0.01 dongle columns all the way to 2200 — and at **2201** every line lands at
  its own offset, in green. Five captures at one frozen value then disagree by
  262 dongle columns where a healthy state repeats to 0.01, and the lit column
  count goes 1329 to 1918 because the picture is smeared across the whole frame
  rather than sitting anywhere.

  The stop is therefore not a control that runs out of range: it is a value the
  counter cannot use, and `retimeStopFor()` returning 0 for a pulse narrower
  than the origin is protecting the picture rather than declining to guess.

  **The "inert" reading was the tear seen through a green feature.** A torn
  capture puts green everywhere, so a clip test finds a feature that never
  leaves and every reading around it reads frozen — which is the same trap
  `counter_origin.py` carries for a window shortened instead of moved.
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

## A zero origin is a deadlock, and only the reference retry escaped it

`retimeStopFor()` answers 0 where the source's sync was not measured, and
`SyncProcessor::writeRetimeStop()` declines to write a 0 — which is right, since
a value invented from the divider alone places every window against an origin
nothing observed. What that leaves depends on what the register already held,
and an input selection resets the sync processor, which zeroes it.

So the block is handed an origin of 0, and **a counter with no origin cannot
retime, so it never counts, so the duty that would replace the 0 is never
measured.** `SyncProcessor::retimeOriginFor()` is the way out: the bare origin,
no sync-width compensation, enough for the counter to run until a real duty
arrives. It places no window, because nothing is solved and the output is away
until a measurement settles.

Measured on `ypbpr`, the Wii in 480p, `SP_RT_HS_SP` 0 beside
`STATUS_SYNC_PROC_VTOTAL` 97. Frozen, the whole PLL group written through
`/sampleclock` — the call the bypass switch makes, which restarts the PLL after
it — six times across two dividers:

| divider | `SP_RT_HS_SP` | `STATUS_SYNC_PROC_VTOTAL` | `STATUS_SYNC_PROC_HTOTAL` |
|---|---|---|---|
| 2040, oversample 2 and 4 | 0 | 100 | 424 |
| 1446, oversample 2 and 4 | 0 | 0 | 212 |
| 2040 again | 0 | 100 | 425 |

The origin stays 0 through every one of them, so **sweeping the clock cannot
escape it** — a reference install goes through `Adc` alone, which writes the
clock group and not the origin.

`VideoPath::writeRetimeOrigin()` is now the one writer, and both the install and
the solve go through it: the measured stop where there is one, the bare origin
where there is not. It previously existed twice, and only the copy
`tryNextReferenceClock()` called carried the fallback — so the reference retry
could escape a zero origin and the divider install could not.

Two things the same measurements rule out, neither to be reached for again:

- **The held clamp is not the cause.** `prepareForDetection()` holds it and the
  release is inside a preset load, so an unmeasurable source keeps
  `SP_NO_CLAMP_REG` 1. Released by hand and held released for 12 s, the count
  stays 97..100 and `STATUS_SYNC_PROC_HLOW_LEN` goes on dithering 1..32.
- **The divider VALUE is not the cause.** 2040 measures this source correctly
  from a boot and fails from the collapsed state, so what separates the two
  states is not the number in `PLLAD_MD`.
