# The recovery clock saturates, and then the engine never measures again

A source the engine cannot measure through the divider it solved used to be
retried for about five seconds and then left alone for the life of the boot.
Measured on a component source at 480p, where the symptom is total silence:
`state: absent`, a live HTTP stack, routes answering from `loop()`, and not one
`sampling:` line on the console across 50 s, a `/sc?~` re-detection and a
`/restart`.

## The reading at the reference clock is correct, every boot

```
input selected: ypbpr, reference divider 2040, applied +30ms
DETECT: enter srcVT=524 HPERIOD=214
DETECT: 22ms, syncFound 2
sampling: 524 lines x 60.00 Hz -> line rate 31500
sampling: 524 lines x 59.93 Hz -> line rate 31468
sampling: 524 lines x 59.93 Hz -> line rate 31467
sampling: rate 31467 doubled 0 -> divider 1446
```

Three correct readings, then the divider the solve sized from them, and nothing
after it. So the source is measurable, the count is the source's, and the
failure follows the installed divider.

**1446 is `VideoPath::dividerCeilingForOutput()`**, not a free choice: it is the
capture a 1080p raster can show, in IF units, which is what
`OutputWindow::maximumCapture()` returns for that raster. `docs/sampling-table.md`
gives 480p as 2046 and is not in conflict -- that table's ceiling is the
counter's, and it never models the output's.

## Why the retry stopped

`unmeasuredPasses_` counts passes since the source was last acquired and stops
at `AcquiredPassCeiling`, which is 255. The reference-clock retry was keyed on
the gap between that counter and a stamp taken from it:

```cpp
if (unmeasuredPasses_ - referenceTriedAtPass_ > ReferenceRetryPasses
    && videoPath_.tryNextReferenceClock(...)) {
    referenceTriedAtPass_ = unmeasuredPasses_;
```

Once both saturate the gap is 0 and never grows again. At
`DetectionIntervalMs` of 20 ms that is 5.1 s, after which nothing retries the
clock, nothing re-probes the sync type, and nothing re-measures the source.

**The silence is not a stalled `loop()`**, which is what makes it expensive to
read: `/samplinglog` and `/testbus` both queue for `loop()` and both answered
and printed throughout. `measureRate()` prints only from `measureLineRate()`, so
a pass returning `NotSteady` or `ClockSettling` is silent while still running --
and a count whose reconciliation is fed a wandering `VPERIOD_IF` never settles,
so every pass returns `NotSteady`.

The fix is to count the passes since the last attempt in their own counter,
reset on a successful advance and held one past the threshold, so the retry
stays reachable for as long as the source is unmeasurable.

## Confirmed on the unit

One 75 s window each on `ypbpr`, the source unmeasurable throughout, counting
`sampling: the reference clock measured nothing`:

| build | retries | span |
|---|---|---|
| before | 17 | 3.03 s to 7.95 s, then nothing |
| after | **235** | 2.21 s to 75.14 s, continuous |

4.9 s is 245 passes at `DetectionIntervalMs`, which is where the stamp meets
`AcquiredPassCeiling`.

## What this does not fix

The reading still fails through the solved divider, so the engine now cycles
the reference clock indefinitely instead of going quiet. Why a divider the
output's raster bounds cannot be measured through is open;
`docs/known-issues.md` carries it.

## The one-shot arm beside it is not this fault

`sourceMoved()`'s unusable-count arm permits one attempt per run of plausible
readings, and that reads like the same defect. It is not the exit here, for two
reasons, both in the code:

- the arm calls `inputTimingsChanged()`, so a mode change is in flight from the
  arm onwards and `runPass()` does not reach `sourceMoved()` again until one
  completes or is refused;
- the guard clears on any plausible reading, so it cannot latch across a source
  that recovers even momentarily.

A fix there would have left the measured state untouched.

## What was refuted on the way

| refuted | by |
|---|---|
| the separator level | the debug pin gives no pulse at any of the 32 levels -- `sog,<level>,...,0` for every one, 3 readings each |
| the retime origin | `SP_RT_HS_SP` written to 63 by hand leaves `HTOTAL` 153 and the count 97 |
| the ADC PLL group | `/sampleclock` applying the whole group and restarting the PLL at 2046, 1446, 1096 and 2040 leaves `lock` 0 and `HTOTAL` hundreds out at every one |

Those three were taken while the bench source was not emitting a usable raster,
so they bound nothing about 480p itself.

**AND THE STATE THEY WERE TAKEN IN WAS THE SOURCE, NOT THE BOARD.** Both inputs
failing, `own V sync: no after 1001ms` on a separate-sync source, and neither
`/sc?~` nor `/restart` nor a mains and USB power cycle clearing it, all read as
the AV module having stopped following input selections. It was the RiscPC
sitting in a mode whose frame rate read back as `F-1`; re-issuing `MODE` brought
`vga` back in about four seconds, and 1280x720 then acquired into bypass with
`PLLAD_MD` 2038 against `STATUS_SYNC_PROC_HTOTAL` 2038. CLAUDE.md carries the
discriminator.

**A build comparison across that state is a comparison across two sources.** A
reading that plain `3b33c085c` fails on `vga` where the same tree plus
uncommitted work acquires does not survive it: the source was re-applied between
the two runs and the mode was never read back.
