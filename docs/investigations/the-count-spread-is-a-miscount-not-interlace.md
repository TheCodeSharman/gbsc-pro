# The count spread is a miscount, and it is not the interlaced half line

Two steadiness runs advance over `STATUS_SYNC_PROC_VTOTAL` and they ask
different questions. The solve's own run asks whether a field carries a half
line, which is the scan type. The presence poll asks whether the source moved.
A single spread cannot serve both, and `Tv5725::SteadyRun` takes it from the
caller for that reason.

## What the sync processor does to a count that is not interlaced

Measured on the Wii at 480p on `ypbpr`, a progressive source whose raster is
525 lines:

| reading | share of samples |
|---|---|
| 521 | 79% |
| 524, the correct count | 19% |

So the processor's miscount is **three lines**, one way, on a source standing
still. At `SteadyRun::InterlacedPairSpread`, which is 1, every crossing between
the two values read as the source leaving and arriving: **400 of each in 110
seconds**, so the source was never present for two consecutive polls.

`SourceMeasurement::CountPairSpread` is **3** because of that, and it is the
presence poll's spread alone.

## Why the solve's own run keeps the interlaced spread

Widening it changes nothing. That run stops being fed once the rate is taken,
so it settles after `SteadySamples` with too few crossings to widen a pair, and
reports the same value either way. Only "has the source moved" is asked at the
wider spread.

## Why a wide spread cannot merge two modes

A pair is earned by the count RETURNING -- `CrossingsForInterlace` crossings --
where a one-way move holds and is adopted instead. So a spread of 3 does not
make a 311-line source and a 313-line source one source; it makes a source
dithering between 521 and 524 one source.

## What it costs elsewhere

The spread is wider than every narrow restoration of the vertical sync an
unserrated composite source costs the counter, so an adopted frame that differs
from the solved count by 3 or fewer lines agrees with it and arms nothing. That
is why the adoption arms a re-solve of its own rather than relying on the count
arm. `../known-issues.md`, under the frame hold racing the capture's sizing.
