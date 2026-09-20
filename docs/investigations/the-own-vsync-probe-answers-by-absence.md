# The own-V-sync probe answers by absence, and pays a second for it

`SyncMeasurement::hasOwnVsync()` establishes the sync type by moving
`SP_EXT_SYNC_SEL` to 0, settling, and polling `STATUS_SYNC_PROC_VSACT` for up to
`OwnVsyncWindowMs`. A source with its own V sync answers in milliseconds. A
composite source answers by never answering, so it spends the whole window.

Measured on the bench RiscPC, forcing a re-probe per sync type:

```
source SYNC 0  ->  own V sync: yes after 203ms / 3ms / 2ms / 53ms / 2ms
source SYNC 1  ->  own V sync: no  after 1000ms / 1001ms / 1000ms
```

With `OwnVsyncSettleMs` of 240 paid unconditionally in front of it, a composite
source costs about **1240 ms** per probe to conclude something by absence, on a
board where the positive case lands in 2 ms.

## The register that answers both directions immediately

With the separator OUT -- the state the probe itself creates and then waits in --
`STATUS_SYNC_PROC_VTOTAL` already separates the two sources:

| source | separator OUT, `VTOTAL` | separator IN, settled |
|---|---|---|
| separate sync | **311**, the true count | 97 |
| composite sync | **50**, implausible | 308 |

Taken twice each, with the configuration written by hand and only the source
moving between readings.

So the probe is watching a register that can only say yes quickly, while sitting
in a state where a different register says yes *or* no immediately. Reading
`VTOTAL` for plausibility there replaces the window with a comparison, and the
answer becomes positive in both directions rather than an absence in one.

## The mirror test also works, and is not needed

Forcing the separator IN and timing how long until `VTOTAL` becomes a plausible
count is a positive test for composite sync:

| source | separator IN -> plausible count |
|---|---|
| composite | **222 ms**, **219 ms** |
| separate | never, over a 2 s window |

It works, and it is repeatable. It is the slower of the two, because the
separator has to acquire before it can report, where the separator-out reading
is available as soon as the counter has settled.

## What has not been measured

Whether `VTOTAL` has reached 311 / 50 within the existing `OwnVsyncSettleMs` of
240 ms. The readings above settled for about 1.5 s. At 50 Hz, 240 ms is about
eleven frames, which is plausible and is not proof. **That measurement gates the
change**: a plausibility read taken before the counter settles answers for the
previous state, which is the same class of error as the probe this replaces.

## Why the cost matters beyond the probe

Nothing re-probes on a sync-type change, because the line count does not move
when a RiscPC changes its sync type. The cheaper the probe, the weaker the
argument for holding a stale answer across a mode change at all.
../../docs/investigations/a-stale-sync-type-leaves-a-composite-source-uncoasted.md
