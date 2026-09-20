# `acquire()` is not `optimizeSogLevel()`, and the difference is a refusal

Moving `tuneSogLevelPreemptively()` into `Tv5725::SyncOnGreen` left one call
reaching back into the sketch: the handover, taken once the level is too low to
step any further, which ran `optimizeSogLevel()`.

Replacing it with the class's own walk looks like removing a redundant hop.
Both end in `SyncOnGreen::acquire()`, both start from `DefaultLevel`, and the
sketch's wrapper appears to add nothing a class that owns the level does not
already know:

```cpp
// what it was
} else if (badSamples_ > HandoverThreshold) {
    optimizeSogLevel();

// what it looked equivalent to
} else if (badSamples_ > HandoverThreshold) {
    choose(DefaultLevel);
    acquire(nowMs, putInForce);
```

It is not equivalent. `optimizeSogLevel()` **refuses**:

```cpp
if (rto->boardHasPower == false || rgbhvBypass()) {
    Tv5725::SyncOnGreen::choose(Tv5725::SyncOnGreen::DefaultLevel);
    return;                       // chooses, and does NOT walk
}
```

## What the refusal is worth

During a detection sweep the handover is reached far faster than a source can
lock, and each walk that runs steps the level down again. Without the refusal
the level ratchets to the floor and stays there.

Measured on the bench RiscPC, `vga`, 320x256@50, after a csync leg, on one
source state and two builds:

| | `SP_SOG_MODE` | `SP_VTOTAL` | level | picture |
|---|---|---|---|---|
| walking unconditionally | 1 | 0 | **2** | none |
| refusing under bypass | 0 | 311 | **12** | clean |

The level thrashes visibly on the way down — 13, 7, 2, 8, 3, 13, 8, 3, 12, 7,
2 over twenty seconds — because the walk's own floor reset keeps putting
`DefaultLevel` back and the next pass takes it apart again. It settles at 2,
which is a sync separator nearly shut, and the source never acquires. **An ESP
restart does not recover it**, because the sweep the restart runs is the sweep
that causes it.

So the walk is injected: `SyncOnGreen::tune()` takes an `escalate` action, and
the sketch passes `optimizeSogLevel`. The class owns the level, the ratchet and
the window; the caller owns whether the level may be walked at all, because
that depends on the path the source is on and the class cannot see it.

## Why this is expensive rather than merely wrong

**No host test can see it.** The tuning tests pass identically either way —
they seed a bus, call the pass and assert the level, and a walk that should not
have run still produces a plausible level. The full suite was green across all
47 binaries before the fault was found and after it was fixed.

**And the settled state looks right.** On a source that is already locked the
handover is never reached, so the two versions are indistinguishable until a
detection sweep runs. It takes the sync-type round trip to reach it, which is
why `docs/video-source-acquisition.md` names that reproduction as mandatory
for anything touching the level rather than leaving it to the author.

## The general shape

A guard in the routine being extracted is part of the operation, not
scaffolding around it. Where the guard reads state the class has no access to —
`rgbhvBypass()`, `rto->boardHasPower` — the temptation is to drop it as an
implementation detail of the old caller. It is the opposite: it is the part of
the behaviour that the class cannot reconstruct, and so the part that has to be
handed in.
