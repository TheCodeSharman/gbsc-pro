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


## The scan type is in `VPERIOD_IF`'s parity, and the classification is consulted first

A real interlace change on the RISC PC -- `INTERLACE ON|OFF` over ModeServ, same
machine, cable, input, mode and sync type -- moves one thing:

```
INTERLACE OFF   VPERIOD_IF 623 x800, all odd    VTOTAL 308   s0_00..05  a7 00 00 00 40 10
INTERLACE ON    VPERIOD_IF 624 x779, all even   VTOTAL 309   s0_00..05  a7 00 00 00 40 10
INTERLACE OFF   VPERIOD_IF 623 x814, all odd    VTOTAL 308
```

2393 samples, no exceptions. **The Mode Detect classification is byte-identical
across the change**, so `STATUS_IF_INP_INT` and `STATUS_IF_INP_PAL_INT` carry no
interlace information at 15 kHz and 50 Hz -- they report a vertical-period family.
The half-line is in `VPERIOD_IF`, as odd against even.

**`Deinterlacer` already implements the parity test and asks the classification
first.**

    bool Deinterlacer::sourceIsInterlaced(uint16_t verticalPeriod) {
        if (ModeDetect::sourceIsInterlaced())  return true;
        if (ModeDetect::sourceIsProgressive()) return false;
        return periodIsInterlaced(verticalPeriod);
    }

`periodIsInterlaced()` opens with a parity check and matches 624 as
`InterlacedPalPeriod` and 623 as `ProgressivePalPeriod` within tolerance, so it is
right in both states. `ModeDetect::sourceIsInterlaced()` reads
`STATUS_IF_INP_PAL_INT`, which is 1 in both. **So the wrong answer wins on this
source**: the firmware calls the progressive RISC PC interlaced whenever it is on
composite sync.

**The order is what is wrong, not either test.** The classification is right
about the family and the rate and cannot see the half-line; the period
measurement sees the half-line and needs a family to interpret it against. For
scan type the measurement is the authority.

**And it only works where `VPERIOD_IF` does**, which is with the sync separator
in the path. On separate sync the parity is debris, so the scan type has no
source there at all. `vperiod-if-on-rgbhv.md`.
