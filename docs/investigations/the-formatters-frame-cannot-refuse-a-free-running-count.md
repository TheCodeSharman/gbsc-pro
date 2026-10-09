# The formatter's frame cannot refuse a free-running count

**REFUTED ON THE BENCH. Do not build this gate.** It is a convincing model —
the chip really does hold a correct second measurement of the source through a
whole stall — and it measured 1 of 10 `ypbpr` legs acquired against a control of
14 of 20.

The model: the sync processor counts in ADC clocks, so a free-running ADC PLL
hands the engine a line count that is nothing to do with the source and holds
still enough to pass the steadiness run. The count then pays for a field rate,
and `rateSettled()`'s attempt cap accepts whichever in-band noise sample is
current. The input formatter measures the vertical through its own path, so it
looks like the one witness on the chip that can refuse such a count.

## What the chip really does hold

`Tv5725::SamplingLog` at 25 ms across one `ypbpr` leg that never acquired, 1362
samples over 38 s, Wii in 480i, RISC PC at 800x600@60 as the outgoing source:

| | reading |
|---|---|
| `STATUS_IF_VT_OK` == 1 | **1358 of 1362** |
| `VPERIOD_IF` | **524 in 1358 of 1362** |
| `STATUS_SYNC_PROC_VTOTAL` | 97, 98, 99, 100, 101, 102, 103, 106, 115, 119 |
| `STATUS_MISC_PLLAD_LOCK` == 1 | **0 of 1362** |
| `HTOTAL` within 2 of the divider | **2 of 1362** |

524 is the Wii's 480i frame, so that much of the model holds: for the whole of a
stall the formatter carries a correct, steady frame beside a count the same chip
cannot justify. On a healthy separate-sync source the formatter measures no
vertical at all — `VPERIOD_IF` 65 and 78 on an acquired 800x600@60 RISC PC with
`STATUS_IF_VT_OK` 0 — and `InputFormatter::verticalPeriod()` already answers 0
there, so a gate built on it is unreachable on every separate-sync source.

`reconciledFrame()` is not the comparison, which is the first thing to find out.
It tries `VPERIOD_IF + 1` over one and over two, and on 480i the total is 525 —
odd, so the factor-2 branch is skipped and the factor-1 difference is 264
against `VerticalSyncMaxLines` of 8. **It returns 0 on an acquired Wii exactly
as it does in the stall.** A gate needs a comparison admitting a half frame:
`count + 1` within a few lines of the frame, or of half of it, the half covering
both a formatter holding twice the frame (measured at 623 against 308 on
unserrated composite) and a sync processor counting a field (524 against 260 on
the Wii).

## Why it fails anyway

**A free-running count lands on the source's own field count often enough that
the gate is inert where it matters.** Measured on a boot that did not acquire in
120 s with the gate in force:

```
sampling: 265 lines x 15.98 Hz -> line rate 0
sampling: 264 lines x 45.19 Hz -> line rate 11977
sampling: 258 lines x 33.61 Hz -> line rate 8706
sampling: 263 lines x 83.51 Hz -> line rate 22046
```

The counts are 258..269 against a true field of 260/261, so every one agrees
with the formatter's frame and is admitted. **What is wrong in that state is the
field rate** — 15.98, 45.19, 33.61 and 83.51 Hz against a source running 59.94 —
and `VideoSignal::FieldRateMinHz` of 30 passes three of the four. `/geometry`
reported `lineRateHz` 7663 on that stall with the gate in force.

Across the counts a stall has actually produced, the gate refuses the gross ones
and admits the near ones, which is the wrong half:

| count | off a field of 262 | |
|---|---|---|
| 97..119 | 143..165 | refused |
| 211, 289, 311, 343 | 51, 27, 49, 81 | refused |
| 258, 263, 264, 269, 270 | 4, 1, 2, 7, 8 | admitted |

**And the held frame latches with no way out.** `VPERIOD_IF` spans two registers
and tears — 1255 in 539 of 721 samples with 615 and 1267 among the rest — so the
frame has to be held across two agreeing readings rather than read per sample,
or a torn reading refuses a count that is arriving correctly. Held, it is then
only cleared by `modeChanged()`, so a frame latched during a stall refuses every
count for the rest of the leg, the correct one included. Three of the ten legs
measured still reported the outgoing source's 37879 at the end, which is nothing
measured at all. That is the same trap `HeldRateRejectionLimit` exists to let
the held rate out of, and this gate had no equivalent.

Twenty-leg measurement, 800x600@60 on `vga` as the predecessor, both scored to
`sync pad: driven`:

| | control | with the gate |
|---|---|---|
| `ypbpr` shown a picture | **14 of 20** | **1 of 10** |
| `ypbpr` spread | 15.1..28.3 s, median 22.2 | the one leg at 31.3 s |
| `vga` | 20 of 20, median 6.3 s | 20 of 20 |

## What this leaves

**Both measurements are downstream of one cause, and a gate on either is
treating a symptom.** The sync processor counts in ADC clocks, and the field rate
is timed off `DEBUG_IN_PIN` through the test bus, which carries sync-processor
signals — so a free-running ADC PLL corrupts the count and the rate together.
The lock column in the first table is the finding: `STATUS_MISC_PLLAD_LOCK` 0 in
1362 of 1362 samples, and `HTOTAL` within 2 of the divider in 2 of 1362, while
every configuration register reads correct.

It also does not say the formatter is a rate reference. `VPERIOD_IF` is a count
of lines rather than a period in time, so it corroborates the line count and
nothing else. `HPERIOD_IF` is the one register stating a period against the
chip's own 27 MHz, and it rails to values that are wrong and steady —
`hperiod-if-railing.md`.
