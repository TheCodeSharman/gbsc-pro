# Step 4's gate is right, and wiring it runs an escalation ladder that is not

`runSyncWatcher()` classifies the source with `getVideoMode()`. Replacing that
with `Geometry::sourceIsPresent()` is step 4 of `docs/retiring-the-sync-watcher.md`,
and the measurement says the engine is the one telling the truth.

## The two answers, measured

The bench RiscPC on `vga`, 320x256@50, after a sync-type round trip, with a
source that is locked and countable:

| | answer |
|---|---|
| engine, `/geometry` `present` | `true`, `lineRateHz` 15625, every sample over 40 s |
| sketch, `printInfo()` | `m:0 u: 96 s: 0` -- `getVideoMode()` 0, `noSyncCounter` pinned at 150, `continousStableCounter` 0 |

`STATUS_00` reads `0x00` and `STATUS_16` reads `0x0f` at the same moment. The
classification takes the `STATUS_00` path for anything below
`videoStandardInput` 14, a scaling RGBHV source sits at 3, and so it is called
absent while the register that would have classified it reads fine.
`docs/investigations/scaling-rgbhv-standard-latches-the-no-sync-branch.md`.

## What wiring it does

The gate change itself is two conditions. What it changes is how far the no-sync
branch gets: previously `noSyncCounter` sat pinned at 150 and cycled, and with
the engine answering, the branch advances and reaches recoveries that had not
been running.

Measured on the same round trip, with the gate wired:

```
own V sync: yes after 2ms        (every ~4 s, forever)
SP_SOG_MODE      1               (csync)
SP_VTOTAL       97               (a separate-sync source counted through csync)
ADC_SOGCTRL      1               (pinned at the floor)
SyncType::isCsync()  false       (the HELD type says separate)
```

**The register and the held sync type disagree, and nothing reconciles them.**
The correction was gated on `SyncType::isCsync()`, which is false, so it never
fired: the message "own V sync found while configured for csync -> separate H/V"
is absent throughout. The probe runs, answers "yes" every time, and the register
stays at 1.

**That half now has an owner.** `Geometry::reacquireSyncType()` is the `% 150`
rung: it probes and applies the answer to `SyncProcessor` and `ModeDetect`
whatever the held value said, so a register on the wrong path is reconciled
rather than left. Measured on the bench recovering exactly that state unaided --
`SP_SOG_MODE` 1 -> 0 with `STATUS_SYNC_PROC_VTOTAL` 0 -> 311 and a clean picture
held afterwards.

**`prepareSyncProcessor()` writing `SP_SOG_MODE::write(1)` bare is REFUTED as
the cause.** It was the leading candidate, being the one write that skips
`SyncProcessor::applyForSyncType()` and `SyncType::forget()`. Tested directly:
`SP_SOG_MODE=1` written by hand on a healthy locked source is undone within
10 s, so the register is actively maintained there. Whatever maintains it is not
running in the recovery path, and which writer wins in that state is still
unknown.

## Three ways to wire it, all measured, none of them right

The ladder now has named owners for every rung, so the gate was tried again.
Three predicates, each on the bench, sync-type round trip and
`/input?src=rgbs` with nothing attached. The number that separates them is
`STATUS_SYNC_PROC_HTOTAL` against `PLLAD_MD`: locked they are equal, and every
build with the ladder untouched settles at 2250 against 2250.

| predicate on the escalation branch | round trip | `/input?src=vga` after `rgbs` |
|---|---|---|
| the engine alone, `!sourceIsPresent()` | **3 of 4 settle at `HTOTAL` ~3250 against a 2250 divider, `STATUS_MISC_PLLAD_LOCK` 0** -- a scrambled picture with every config register correct | not reached |
| unchanged, with only the maintenance branch keyed on the engine | 3 of 3 at 2250, 1-3 sync-out drops in 41 | recovers |
| both must agree the source is gone | 3 of 3 at 2250, 1-2 drops in 41, and `HPERIOD_IF` at its correct 431 | **does not recover** -- `SP_SOG_MODE` 1 and `SP_VTOTAL` 97 on a separate-sync source, the level walking, for minutes. `/sc?~` clears it |

**The engine alone withholds recoveries the sync-type transition needs**, and
what it costs is the ADC PLL: the escalation stops running on a source the
engine calls present, and the sync processor is left counting a line the ADC is
not sampling.

**The middle row looks best and is the one to distrust.** Both branches run on
the same pass there -- the escalation still keyed on the classification, the
maintenance on the engine -- and each zeroes the other's counter, so
`noSyncCounter` never reaches 8 and `continousStableCounter` never reaches 2.
Neither ladder advances at all. The good numbers are the absence of both, not
the presence of maintenance, and shipping it would record an accident as a
design.

## A fourth wiring, on a better answer, and why it also fails

`Geometry::sourceState()` answers in three states rather than two: the count
steadiness run is only the VERTICAL half, and a source holds a correct steady
count while the ADC samples a line it is not locked to. The horizontal half is
`SourceMeasurement::dividerLatched()`, and `sourceIsPresent()` also excludes a
mode change in flight, because the state is published by the idle pass and a
change in flight leaves the verdict taken BEFORE the source moved standing.

Both branches keyed on that one answer: three of three round trips still settle
near 3250. **The answer is not the problem -- it reports the state correctly,
`unlocked` with `present` false, so the escalation does run.** The escalation
then cannot recover it, and every ADC clock register reads byte-identical to the
good state.
`docs/investigations/an-unlocked-adc-pll-is-invisible-in-the-registers.md`.

**So the gate does not fail on the classification any more. It fails because
withholding the escalation causes a fault the escalation cannot repair**, and
what the escalation was doing to prevent it is unknown -- a `PLLAD_LAT` pulse,
the phase sweep and `PLLAD_FS` are all ruled out. What the fault IS is settled:
the ADC PLL's loop is open and its VCO free-runs, measured by walking the
divider underneath it and watching `STATUS_SYNC_PROC_HTOTAL` refuse to follow.

## The blocker is the disagreement itself

**No single predicate over the two answers is right, because they disagree and
each ladder needs a different one.** The escalation needs an answer that goes
false while a transition is still settling, which the engine's steadiness run
does not; the maintenance branch needs an answer that is true on a source the
engine can measure, which `getVideoMode()` is not. Wiring either to the other's
predicate breaks the ladder that was working.

So step 4 does not wait on step 7 any more -- that is done. It waits on
`getVideoMode()` no longer mis-classifying a scaling-RGBHV source, which is
`docs/retiring-mode-detect.md` and step 12, and on the standard byte that forces
`videoStandardInput` to 3 carrying two facts at once.

## What has to happen first

Step 7 is done: every rung named, every register under the class that owns it.
It was not sufficient. What the measurements above add is that the ladder was
never the whole risk -- the classification the gate replaces is load-bearing for
the escalation in a way the engine's answer cannot substitute for.

Two things found while getting here are fixed and shipped, and both stand on
their own:

- `sourceIsPresent()` held stale-true through a failing solve, because the
  solving branch never reaches `sourceMoved()`. A reader gated on it would have
  had its recovery withheld exactly when it was needed.
- A second owner of the sync type in the 900 ms RGBHV check, flipping it to
  csync off `STATUS_INT_SOG_BAD` -- a bit that is permanently set on a
  separate-sync source, because the separator has nothing to slice there.

## The reproduction, for whoever lands it

A sync-type round trip over ModeServ -- `SYNC 1`, `SYNC 0`, repainting with
`PATTERN` after each because `SYNC` does not. A plain mode change is NOT a
substitute: measured, an 800x600 excursion and back leaves no blanking at all.

The witness is `PAD_SYNC_OUT_ENZ`, s0_49 bit 2. On a source the engine calls
present it must stay 0: the fault drops it for the length of each re-solve, so
the encoder is shown a dropout every few seconds and the television stays dark
with every register reading correct. Measured before the gate, 8 drops in 1262
reads over 25 s.

**THE DARK TELEVISION IS NOT THE GATE'S DOING, and reading it as such attributes
a standing fault to whatever change is in flight.** The same round trip on a
build with the gate absent leaves the television dark too, twice out of two,
with `SP_SOG_MODE` 0, `STATUS_SYNC_PROC_VTOTAL` 311 and the sink's own display
naming the mode it is locked to. The re-solve churn behind it -- `source moved:
interrupt` re-arming every few seconds with the count equal to the solved count
-- is there without the gate as well. What the gate changes is how far the
ladder advances, not whether the encoder is shown dropouts.
