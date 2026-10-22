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

## A second candidate: the broad pulse in `STATUS_SYNC_PROC_HLOW_LEN` -- REFUTED

The vertical interval's long low period appears in `HLOW_LEN` on composite sync
and never on separate sync, which reads as a positive composite test needing no
path change at all -- no separator flip, so no settle and no self-inflicted SOG
switch to acknowledge. **It is circular, and the 2x2 is what shows it.**

Both sync types in both arrangements, the arrangement held across a source sync
change because a sync-type change arms no probe. `vga`, 320x256@50, read with
`/samplinglog?width=500&lo=0&hi=400`, which bursts eight reads per poll,
histograms them and rejects the two-byte tearing crossings itself:

| arrangement | source | readings | above the band | lowest | highest | sync processor |
|---|---|---|---|---|---|---|
| csync | composite | 16448 | 39 | 118 | **2730** | HSACT 1, VTOTAL 308 |
| csync | separate | 1808 | **733** | 2 | 4029 | HSACT 0, VTOTAL 98 |
| separate | separate | 17704 | **0** | 155 | 156 | HSACT 1, VTOTAL 311 |
| separate | composite | 2736 | **3** | 80 | 560 | HTOTAL 2892 against divider 1446, VTOTAL 475..1944 |

The two MATCHED cells reproduce the signature exactly -- the broad pulse at
2730 on composite, and 155 or 156 in every one of 17704 readings on separate
sync with nothing above the band ever. The two MISMATCHED cells destroy it: it
false-positives on **41% of readings** on a separate-sync source, and finds the
broad pulse in 3 readings of 2736 on a composite one.

**The signature is downstream of LOCK, and lock is downstream of the
arrangement.** In both mismatched cells the sync processor is not counting the
source -- HSACT 0 beside VTOTAL 98 in one, HTOTAL at twice the divider with the
count wandering 475 to 1944 in the other -- so the register holds noise spanning
2 to 4029 rather than a pulse width. **A reading that needs the arrangement to be
right already cannot be what chooses the arrangement.**

So `HLOW_LEN` joins the family of status fields that read like verdicts and are
none of them a verdict: `STATUS_MISC_PLLAD_LOCK` is a duty cycle,
`STATUS_SYNC_PROC_VSACT` reports the path already configured, `STATUS_IF_VT_OK`
reports a measurement separate sync never completes, and this one reports a
pulse width the block only measures once it has locked.

**What survives is the structure the probe already has.** The only non-circular
tests are the ones that CHANGE the arrangement and observe the result, which is
what moving the separator in and out does. No register read in a single
arrangement can answer it, and that now covers both candidates: the count reads
0 in the probe's own configuration, and the pulse width reads noise in the wrong
one.

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
