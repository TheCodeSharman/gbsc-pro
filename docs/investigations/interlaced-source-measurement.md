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
