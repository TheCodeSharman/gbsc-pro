# The own-V-sync probe's blind settle was the cost, not the window

`SyncMeasurement::hasOwnVsync()` establishes the sync type by moving
`SP_EXT_SYNC_SEL` to 0 and polling `STATUS_SYNC_PROC_VSACT` for up to
`OwnVsyncWindowMs`, timed from the moment the path moves. A source with its own
V sync answers in 2 to 3 ms. A composite source answers by never answering, so
it spends the window and then asks the separator instead.

The baseline, with `OwnVsyncSettleMs` of 240 paid in front of the poll and the
figures read from the end of it:

```
source SYNC 0  ->  own V sync: yes after 203ms / 3ms / 2ms / 53ms / 2ms
source SYNC 1  ->  own V sync: no  after 1000ms / 1001ms / 1000ms
```

That is about **1240 ms** for a composite source to conclude something by
absence, and 242 ms for a separate-sync source to conclude something the bit
was already reporting.

## The register that would answer both directions -- REFUTED

The proposal was that with the separator OUT -- the state the probe itself
creates and then waits in -- `STATUS_SYNC_PROC_VTOTAL` already separates the two
sources:

| source | separator OUT, `VTOTAL` | separator IN, settled |
|---|---|---|
| separate sync | 311, the true count | 97 |
| composite sync | 50, implausible | 308 |

Those were taken with the configuration **written by hand**, and that is the
confound. **In the state `SyncProcessor::applyForSyncType(false, divider)`
actually creates, the register reads 0 on BOTH sync types.** Measured on the
bench by tracing it every 2 ms for 1200 ms from the moment the path moves:

| source | mode | first plausible count | at 240 ms | at 1200 ms |
|---|---|---|---|---|
| separate sync | 320x256@50, scaled | never | 0 | 0 |
| separate sync | 1280x720@60, pass-through | never | 0 | 0 |
| composite sync | 320x256@50, scaled | never | 0 | 0 |

The real call writes more than the two fields the hand configuration set --
`setSubCoast()`, `applyPulseIgnore()`, the coast pair -- and the count does not
survive it. So the register carries no information in the probe's own
configuration, and it cannot replace the window. **The separator-IN column
stands**, and is what the negative branch already uses.

**The settle in front of the window was the real cost, and it is gone.** VSACT
timed from the instant the path moves, with no blind settle:

| probe | first high | held from |
|---|---|---|
| first after a detection cycle | **294 ms** | 315 ms |
| second | **0 ms** | 20 ms |
| third | **0 ms** | 22 ms |
| fourth | **0 ms** | 20 ms |

Three of four answer immediately, so the 240 ms paid in front of every probe
bought nothing in the common case -- the poll's own budget covers the same
reacquisition. On composite the bit is dead flat: **0 edges across 1500 ms**, so
there is no flicker to guard against and no early signal to read either.

`OwnVsyncSettleMs` is kept where it is the separator's own acquire time, in the
negative branch. `OwnVsyncWindowMs` is 600, which covers the 294 ms tail and the
443 ms the earlier distribution reaches on the same clock.

**What makes bounding it safe is the three-state answer.** A premature no used
to latch composite separation onto a separate-sync source; now the negative
branch asks the separator for a frame, a separate-sync source yields 97 rather
than a count any source runs, and the answer is `NoSource` -- which latches
nothing and is probed again on the next pass.

Measured either side on `vga` at 320x256@50, two probes per `/sc?~`:

| | two probes took |
|---|---|
| before | 3.11 s to 4.44 s, **1.33 s** |
| after | 4.14 s to 4.15 s, **~10 ms**, answering after 3 ms and 2 ms |

**The negative direction is still an absence**, bounded at 600 ms rather than
1240. No register in the separator-out state says "composite", so it cannot be
made positive there, and the separator-IN test that does say it positively needs
the separator to acquire first -- which is why it stays second, after the cheap
poll that costs a separate-sync source nothing.

## What the mirror test is still good for

Forcing the separator IN and timing how long until `VTOTAL` becomes a plausible
count is the positive test for composite sync, and it is what the negative
branch runs:

| source | separator IN -> plausible count |
|---|---|
| composite | **222 ms**, **219 ms** |
| separate | never, over a 2 s window |

## Why the cost matters beyond the probe

Nothing re-probes on a sync-type change, because the line count does not move
when a RiscPC changes its sync type. The cheaper the probe, the weaker the
argument for holding a stale answer across a mode change at all.
../../docs/investigations/a-stale-sync-type-leaves-a-composite-source-uncoasted.md
