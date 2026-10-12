# The ladder's deletion removed recoveries nothing replaced

`VideoSourceAcquisition::recoverSource()` fired every `RecoveryBudgetMs` for as
long as a source went unacquired, and it was replaced by `noteSourceAbsent()`,
which records a stale frame lock and forgets the sampling phase. What it stopped
doing:

| the ladder's act | what does it on the absent path now |
|---|---|
| `Adc::forgetPhase()` | kept |
| `FrameBuffer::releaseCapture()` where hsync is active | nothing |
| `SyncProcessor::reset()` | nothing -- the sketch's detection and leaving pass-through are the only callers left |
| `VideoPath::restartSamplingClock()` | **nothing: the method has no callers** |

**And the clock it was timed on does not saturate.** The ladder counted
`unacquiredForMs_`, a wall clock. The reference-clock retry that outlived it was
keyed on the gap between `unmeasuredPasses_` and a stamp taken from it, and that
counter stops at `AcquiredPassCeiling` --
`the-recovery-clock-saturates-and-the-engine-goes-silent.md`. So the replacement
was both narrower in what it did and bounded in how long it did it for.

## The stated reason for the deletion is refuted

The deletion is argued in `noteSourceAbsent()`'s own contract: a ladder firing
block resets at a source it could not measure destroyed the reading it was
trying to rescue, measured on a component source at 480p as a correct count of
524 reset to 97 within ten seconds and never returning.

**That decay happens with the ladder already deleted.** Measured on three boots
of the build that has no ladder, console and `/bootlog` carrying no `recovery:`
line at all:

```
sampling: 524 lines x 59.93 Hz -> line rate 31467
sampling: 524 lines x 59.93 Hz -> line rate 31468
sampling: rate 31468 doubled 0 -> divider 1446
sampling: 219 lines x 30.90 Hz -> line rate 6798
sampling: 524 lines x 39.90 Hz -> line rate 0
sampling: 524 lines x 40.04 Hz -> line rate 0
sampling: 524 lines x 39.88 Hz -> line rate 0
sampling: 208 lines x 0.00 Hz -> line rate 0
```

and `STATUS_SYNC_PROC_VTOTAL` 97 from there on. So the block resets were not
what collapsed the count, and what the deletion removed was the recovery rather
than the cause.

## What follows for every recovery this tree records

**A RECOVERY MEASURED BEFORE THE DELETION WAS MEASURED WITH THE LADDER ALSO
RUNNING.** Anything credited to a route -- `/sc?~`, an input toggle, a sample
clock restart -- was observed on a build where the sync processor was also being
reset and the sampling clock restarted every ten seconds underneath it. So a
route that "recovers" a state may have been the ladder arriving next, and the
claim has to be re-measured rather than inherited.

One is already measured failing. The `SP_SOG_MODE` 1 latch on a separate-sync
source is recorded as answering to `/sc?~`; reached by an `ypbpr` to `vga` input
change it does not -- `ADC_SOGEN` 1 on the VGA input, hsync sought on green,
`STATUS_SYNC_PROC_HSACT` 0, and two `/sc?~` calls leaving it exactly there where
a `/restart` cleared it in about twelve seconds.

## What this does not argue

**It is not an argument for restoring the ladder as it was.** A timer that
resets blocks under a source being measured is a second writer on the sync
processor, and the engine's own rule is that a measurement is taken when the
source changes. What it argues is narrower: the acts the ladder performed have
no owner now, and `restartSamplingClock()` having no callers is the
unambiguous part. Either an owner takes each act on a stated trigger, or the
method goes and the claim goes with it.
