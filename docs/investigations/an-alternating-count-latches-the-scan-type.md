# An alternating count latches the scan type for the life of the run

`SteadyRun::alternated()` never returns to false. Once a source's line count has
alternated by one, the run reports interlaced for ever, on a source that has been
steadily progressive since — so `Deinterlacer::steer()` never reaches its
progressive branch, `disableMotionAdapt()` is never called, and the
motion-adaptive deinterlacer runs on a progressive source until something forces
a re-detect.

## The mechanism is one skipped branch

`SteadyRun::sample()` widens the pair only when a value agrees with neither end:

```cpp
if (value != high_ && value != low_) {
    const bool widensByOne = run_ > 0 && low_ == high_ && agree(value, high_);
    ...
}
```

A steady stream of 627 after a 627/628 episode satisfies `value == low_`, so the
body is skipped entirely and `high_` stays 628 with `low_` 627. Nothing else
assigns them: `restart()`, `settle()` and `reset()` collapse the pair, and none of
the three is on the sampling path. `alternated()` is `settled() && low_ != high_`,
so it is true from the first alternating pair onwards.

Demonstrated at the host layer — six alternating samples, then two hundred steady
ones:

```
after alternating:        settled=1 alternated=1 value=628
after 200 steady 627:     settled=1 alternated=1 value=628
```

`value()` is wrong by one for the same reason, so the solve carries a count a
line too high as well.

## What it does on the bench

RISC PC on `vga` at 800x600@60, `SYNC 0`, driven by ModeServ's `INTERLACE`.
Nothing else moves; both states are `state: acquired` with the source present.

| | `STATUS_SYNC_PROC_VTOTAL` | `MAPDT_VT_SEL_PRGV` | picture |
|---|---|---|---|
| `INTERLACE ON` | 627/628, alternating | 0 | motion adapt engaged, correctly |
| `INTERLACE OFF` | **627 in 24 of 24** | **0** | green cast, comb tearing, content displaced |

The card names its own state, so the photograph carries the contradiction in one
frame: it reads `SEPARATE SYNC PROGRESSIVE` while the deinterlacer weaves.

**`INTERLACE ON` reproduces it; it is not the condition.** Both of two
consecutive OTA flashes on the same progressive source came back with motion
adapt engaged, no interlaced source anywhere near the bench. Re-acquisition is
enough: the count wobbles while the source settles -- the divider moved 1438 to
1440 across one of those flashes -- and a single alternating pair arms the latch
for the life of the run. A unit that returns from a flash green and comb-torn is
this rather than the flash.

The engaged state is `enableMotionAdapt()`'s and is identified by its own fields
rather than by the one bit that shows:

| field | engaged | released |
|---|---|---|
| `DIAG_BOB_PLDY_RAM_BYPS` | 0 | 1 |
| `DIAG_BOB_WEAVE_BYPS` | 0 | 1 |
| `MADPT_Y_MI_OFFSET` | 0 | 127 |
| `RFF_FETCH_NUM` | 128 | 1 |
| `WFF_ENABLE` / `RFF_ENABLE` | 1 | 0 |
| `MAPDT_VT_SEL_PRGV` | 0 | 1 |

`MADPT_EN_UV_DEINT` and `RFF_LINE_FLIP` read 0 throughout, which is what
separates this from `enableScanlines()` — both write `MAPDT_VT_SEL_PRGV`, so that
bit alone does not say which feature owns the zero.

## `MAPDT_VT_SEL_PRGV` is not a detection read-out

Four functions write it: `enableScanlines()`/`disableScanlines()` and
`enableMotionAdapt()`/`disableMotionAdapt()`. Reading it as *whether interlace
was detected* conflates two features and gives the wrong answer in both
directions — it is 1 on a source correctly detected as interlaced whenever bob is
preferred, and 0 on a progressive source whenever scanlines are on. Read the
table above instead.

## The recovery is a re-detect, not a mode round trip

| clears it | does not clear it |
|---|---|
| `/sc?~` | a source mode round trip |

`INTERLACE OFF` re-applies the mode, so the source leaves and returns and the
engine re-solves — and the latch survives it, because nothing in a re-solve
resets the run. `/sc?~` restores a clean full-screen picture, `MAPDT_VT_SEL_PRGV`
1 and every field in the table back at its released value.

That is the opposite way round from the gate fault this replaces, where the round
trip was the recovery. Reach for `/sc?~`.

## What it supersedes

The gate is fixed. `VideoSourceAcquisition` no longer returns before steering
when the vertical period reads 0 — it measures the scan type on the maintenance
cadence and hands it to `steer()`, with the period kept only as a settling guard.
So `steer()` does run on separate sync, which is why motion adapt engages there
at all, and `STATUS_IF_VT_OK` reading 0 for the whole of both states above no
longer blocks anything.

What is left is the filter's input. `steer()` counts `FilteredPasses`
consecutive readings before acting in either direction, and the interlaced run
can never be broken because `measureScanType()` cannot report progressive again:

```cpp
if (countAlternated())
    return ScanInterlaced;
```

## What would settle it

A narrowing rule on the same run, tested for both directions. The pair widens on
a single sample and must collapse on a run of agreeing ones, or the two
directions are not symmetric and the fault returns in the other axis. The
existing suite covers widening — `a pair alternating by one settles too`, `the
pair widens once and no further` — and nothing covers a source that alternates
and then stops, which is why the latch shipped.

The interlaced reading must survive a genuinely interlaced source's own jitter,
so the collapse cannot be one sample either: a field count that alternates
627/628 will present runs of each.

[interlaced-source-measurement.md](interlaced-source-measurement.md) is why the
count alternates at all;
[the-deinterlacer-had-two-owners.md](the-deinterlacer-had-two-owners.md) is the
second writer of the same registers.
