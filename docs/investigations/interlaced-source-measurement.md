# The sync processor cannot measure an interlaced source accurately

Measured on a Wii at PAL 576i over YPbPr, 2026-08-26 — the first interlaced
source this project has had.

| quantity | reads | truth | error |
|---|---|---|---|
| line rate from `HPERIOD_IF` (431) | 15625.0 Hz | 15625.0 | exact |
| the engine's `lineRateHz` | 15969 | 15625 | **+2.2%** |
| `STATUS_SYNC_PROC_VTOTAL` | 310 | 312.5 | −0.8% |
| `VPERIOD_IF` | 624 | 625 | −0.16% |

`STATUS_SYNC_PROC_VTOTAL` counts a **field** and `VPERIOD_IF` a **frame**, which
is why 310 against 624 is not a disagreement. But 576i carries **312.5** lines
per field, and a line counter cannot hold the half. The engine derives its line
rate from that count and a field rate measured beside it, and lands 2.2% high.

`HPERIOD_IF` is exact because it does not count lines at all: it measures the
line period against the chip's own 27 MHz, so interlace is invisible to it.
`27e6 / ((431 + 1) * 4)` is 15625.0 Hz to the digit.

## What it costs today

The framing a user tunes is stored against the source's measured identity, and
this source saved as:

```
0 310@52 = 462 9356 387 8887
```

**52 Hz**, for a 50 Hz source. The key is wrong, so a later solve that measures
51 or 50 will not match it and the framing will not come back. On a progressive
source the same mechanism works — `311@50` and `524@60` are both correct in that
same file.

## What it does not establish

That the engine should switch to `VPERIOD_IF`/`HPERIOD_IF`. The geometry model
is built on `STATUS_SYNC_PROC_*` throughout, and **those count in ADC samples
while the IF registers count in IF units against 27 MHz** — the downstream
arithmetic assumes the former. `HPERIOD_IF` also has its own failure modes;
`hperiod-if-railing.md` records three, one of which is a stable wrong value that
every health check scores as healthy.

What is established is narrower: on an interlaced source the sync processor's
line count is short by half a line by construction, and anything derived from it
inherits that. A source key built from it is wrong by enough to miss.

## The other thing interlace does

The last captured line alternates between fields, so a capture window that
reaches into input blanking shows a single shimmering line at the bottom while
everything above it sits still. It reads as a deinterlacing fault and is not one
— it is the framing, and pulling the vertical extent in clears it. The rest of
the picture is stable throughout, which is the tell: bob judder moves the whole
image.


## NTSC 480i alternates where PAL 576i held steady

The same console, the other output mode, measured 2026-09-12 over 1417 samples:

```
STATUS_SYNC_PROC_VTOTAL   260 x736, 259 x681   -- two values, near evenly
VPERIOD_IF                524 x1417
```

576i above reads a single steady 310, so **the half-line is expressed as an
alternation in one mode and absorbed into a constant undercount in the other**.
Both are short of the true field: 259.5 against 262.5, and 310 against 312.5.

**So an alternating count is not an interlace detector.** One interlaced source
alternates and another does not, which rules out the obvious reading of the
2026-09-12 measurement. Nor is the ratio `VPERIOD_IF / STATUS_SYNC_PROC_VTOTAL`,
which is about 2 on both -- and is 2.02 on the progressive RISC PC under
composite sync as well, because `VPERIOD_IF` counts the doubled IF line rather
than the source's.

**What it costs is acquisition.** `VideoSourceAcquisition::countHeld()` needs four
consecutive identical counts, which an alternating field count never supplies, so
480i never reaches `acquired`: no solve runs for the mode, the output clock is
never seeded, and the picture rolls while every register reads correct.
`mode-detect-answers-before-any-measurement.md`.


## The scan type is the half line in `VPERIOD_IF`, and line doubling inverts its parity

An interlaced field carries a half line. `VPERIOD_IF` is the only count on the
board with the resolution to hold one -- and it has that resolution only where
the input formatter doubles the line, which is what puts the count in half lines.
So the parity that means interlaced is not fixed: **it inverts with
`IF_HS_DEC_FACTOR`.**

Measured with `INTERLACE ON|OFF` over ModeServ, one machine, one cable, one
input, composite sync throughout, only the mode's line rate and the interlace
flag moving:

| mode | line rate | `IF_HS_DEC_FACTOR` | progressive | interlaced |
|---|---|---|---|---|
| 320x256@50 | 15625 | 1 | 623 odd x519 | 624 even x519 |
| 640x200@60 | 15697 | 1 | 523 odd x534 | 524 even x536 |
| 640x480@60 | 31690 | **0** | **524 even x526** | **525 odd x529** |

3163 samples, every state unanimous. The Wii on `ypbpr` fits it from the other
side: 480i is line doubled and reads `VPERIOD_IF` 524 x531, and 480p is not
doubled and reads 524 as well. **The same period, two scan types** -- which no
table of broadcast totals and no fixed parity can separate, and which is why the
480p bench source was read as interlaced.

The rule that holds across all eight states is one line:

    interlaced  <=>  (VPERIOD_IF + lineDoubled) is odd

`SourceMeasurement::scanTypeFor()` is that, and `scanType()` applies it to the
doubling the engine currently holds. The count must be a plausible vertical
total first -- doubled counts are halved before that check -- or the answer is
`ScanUnknown` and the caller leaves the deinterlacer where it is.

**The classification cannot supply this.** `s0_00..05` is byte-identical across
a real interlace change at 15 kHz and 50 Hz, `a7 00 00 00 40 10` both ways, so
`STATUS_IF_INP_INT` and `STATUS_IF_INP_PAL_INT` report a vertical-period family
and nothing about scan. Consulted first, as `Deinterlacer` used to, it engaged
the motion-adaptive deinterlacer on the progressive RISC PC and held it there
through a real interlace change in both directions -- measured on the bench,
`MAPDT_VT_SEL_PRGV` 0 with `WFF_ENABLE` and `RFF_ENABLE` 1 in all three states.
With the measurement answering, the same three states read off, engaged, off.

**It only works where `VPERIOD_IF` does**, which is with the sync separator in
the path. On separate sync the register holds debris -- 33 to 101 on the bench
source -- and `STATUS_IF_VT_OK` reads 0 beside it, which is the gate the caller
gives it. There the scan type has no source at all.

**`SourceMeasurement::countIsSerrations()` still takes the classification**, and
it is the second consumer of the same unreliable bit. It asks whether a count
could have doubled, which only an interlaced source can do. On every state
measured here it reaches the same verdict either way, so there is no fault to
chase -- but the measured scan type is the better input and is now available
beside it.
