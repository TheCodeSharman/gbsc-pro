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
