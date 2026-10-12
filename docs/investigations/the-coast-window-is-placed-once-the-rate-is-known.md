# The coast window is placed once the rate is known

`SP_H_CST_SP` is the horizontal coast stop position, and the engine left it at
`SyncProcessor::DefaultCoastStop` -- 0x100, what the bring-up writes -- on a
source it had measured. The window its own arithmetic asks for is
`27000000 / lineRateHz * 0.968`, which is 830 on the Wii at 480p.

## What the default costs

Measured on the Wii at 480p over `ypbpr`, automation frozen so nothing re-solves
underneath the reading, 60 samples of `STATUS_SYNC_PROC_VTOTAL` per row:

| `SP_H_CST_SP` | 524/525 | distribution |
|---|---|---|
| 830, the formula | 95.0% | `524:57, 454:2, 450:1` |
| 256, the default | 11.7% | `521:49, 524:7, 455:2, 451:2` |
| 830 again | 95.0% | `524:57, 454:1, 451:1, 450:1` |

The default pins the count at **521** in 49 of 60 samples against a true 524.
That is not noise: it is a systematic three-line shortfall, because the window
coasts over the wrong part of the line.

## Why a three-line shortfall is fatal

The presence verdict is one reading:

```cpp
const bool plausible = VideoSignal::countIsSource(lines);
const bool held = countHeld(lines);
sourceState_ = !(plausible && held) ? SourceAbsent : ...
```

521 is plausible -- it is inside the range any source may run -- so the run is
what fails, and `SourceAbsent` follows from a single count that disagrees with
the solved one. Absent means searching, searching re-runs the search
configuration, and the count stays wrong, so the source cannot measure its way
back.

## Why it was never placed

`placeCoastWindow()` takes `sampling_.lineRateHz()` and refuses to write for a
searching source. In the acquire path it runs before the rate has been
measured, so `coastLineFor(0)` is 0 and `acquireCoastWindow()` returns without
writing. The only other caller is the sketch, gated on `acquiredPasses() >= 7` --
and the count the default produces takes the source out of acquired before the
seventh pass, so that retry is unreachable. The window is therefore placed by
neither.

`holdSolvedSource()` places it instead: the rate is measured there, the state is
acquired so the searching gate is open, and a solve is the one event after which
the arithmetic has everything it needs.

## Measured on the bench, unfrozen

The same 75 s watch either side, sampling every 2.5 s:

| | `SP_H_CST_SP` placed | VTOTAL 524 | acquired |
|---|---|---|---|
| before | 2 of 28 samples | ~11% | flapping |
| after | 22 of 29 | 69% | 52% |

## The window spans the line, and the stop is the whole of the choice

`SP_H_CST_ST` is `H coast start position (total-this value)`, so it counts
backward from the line end. The pair the engine writes is therefore
`[total - 16, 0.968 x total]`, a window that wraps the line start and leaves
only the 12 counts between the stop and the start uncoasted -- so the
non-auto window coasts the line, and `SP_H_CST_SP` alone decides how much of
it.

Measured on the Wii at 480p, each value re-asserted before every sample so a
solve cannot take it back, 60 s a row:

| `SP_H_CST_SP` | the line it coasts | 524/525 | acquired |
|---|---|---|---|
| 834, the engine's `0.968 x total` | 97% | 92.0% | 80.0% |
| 862, the whole `total` | 100% | 89.5% | 78.9% |
| 1800, past the line end | -- | 12.0% | 16.0% |

So the two are indistinguishable and a stop past the end of the line is not:
the window must stop INSIDE the line, and where inside does not matter. The
0.968 is arbitrary -- it is upstream's `updateCoastPosition()`, where it
multiplied an average of eight `HPERIOD_IF` reads -- but nothing is bought by
replacing it with 1.0.

What that makes wrong is the DEFAULT. `[total - 16, 256]` coasts 272 counts of
a 862-count 480p line and of a 1728-count SD line: **32% and 16%**, which is
the 11.7% row above. A static default is wrong for every source, and a maximal
one is worse than a short one, so the value wants deriving from the shortest
line the engine will accept rather than replacing with another constant.

## The coast lengths cannot be measured on this source

`SP_PRE_COAST` has had three writers holding 7, 9 and 16, and removing one of
them does not make the field measurable. The pair is swept through
`overrideCoast()`, so the engine re-applies the candidate on every solve, with
the engine's own 7/6 between every candidate as the control:

| | 524/525 | acquired | rate plausible |
|---|---|---|---|
| owner 7/6 | 13.8% | 13.8% | 100.0% |
| pre 9 | 0.0% | 0.0% | 100.0% |
| owner 7/6 | 0.0% | 0.0% | 86.7% |
| pre 16 | 0.0% | 0.0% | 0.0% |
| owner 7/6 | 0.0% | 0.0% | 0.0% |
| pre 5 | 10.3% | 0.0% | 79.3% |
| owner 7/6 | 82.1% | 75.0% | 96.4% |

**The control spans 13.8% to 82.1% for one unchanged configuration**, which is
wider than any candidate's distance from it. The source strands its held rate
and takes minutes to come back, so adjacent rows are not independent and a
60 s row measures the recovery rather than the coast. That is what closes this
question for now, and it is NOT the writer count: one writer fewer changes
none of it.

So 7 stands on what the field means rather than on a measurement. It is the
number of lines before vertical sync at which the coast becomes valid, the
coast has to cover the irregular part of the vertical interval and no more --
longer holds the PLL across lines carrying good hsync -- and 480p's 6-line
vertical sync, NTSC's 3 lines of equalisation and PAL's 2.5 all fit inside it.

## What this does not fix

The source still falls out, and `SP_H_CST_SP` still returns to the default
several times a minute. Guarding `applyForSearch()` so a measured window
survives a search does **not** stop that, measured: 64.5% correct counts and
`SP_H_CST_SP` at 256 in 7 of 31 samples, both within noise of leaving it alone.
The reset therefore arrives through one of `forgetPositions()`'s other callers
clearing `coastPlaced_` first, after which the search writes the default
legitimately.

About 5% of readings are a near-450 count even with the window placed at 830.
That population is not explained here, and it is enough on its own to arm a
re-solve.
