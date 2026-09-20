# A sync type change arms no probe, because the line count does not move

A source can change its sync type without changing anything the engine watches
for. The RISC PC sets its type from CMOS, so `SYNC 0` and `SYNC 1` present
separate and composite sync on one cable at one line count -- 311 at
320x256@50, 524 at 640x480@60, either way. Nothing in `source moved: interrupt |
count | rate` distinguishes them, so no sync-type probe is armed, and the
separator stays out of the path on a source that needs it.

On the scaling path the engine acquires anyway and the cost is a refused duty.
In pass-through it cannot, and the source stays absent until a recovery rung
reprobes the type about fifty seconds later.

## What the stuck state is

640x480@60 on `vga`, pass-through (`preferScalingRgbhv` off), `SYNC 1`. Read
inside the window where the source will not acquire, three rounds over 30 s:

| | pass-through, `SYNC 0` | pass-through, `SYNC 1` |
|---|---|---|
| `/geometry` | `acquired`, `ch` 1493, 31500 Hz | **`absent`** for the whole window |
| `SP_SOG_MODE` | 0 | **0** |
| `SP_EXT_SYNC_SEL` | 0 | **0** |
| `STATUS_SYNC_PROC_VTOTAL` | 524 | **524** |
| `PLLAD_MD` / `STATUS_SYNC_PROC_HTOTAL` | 2039 / 2039 | 2039 / 2039, 2038 |
| the engine's measurement | 60 Hz, line rate 31500 | `524 lines x 15.32 Hz -> line rate 0` |

**The count is right and the divider is latched.** What is wrong is the field
rate: 15.32 Hz where 60 is due, about a quarter of it, repeated identically for
as long as the state stands. `rateFollowsCount()` refuses it, the line rate
comes out 0, and the source is never acquired.

**`SP_SOG_MODE` 0 on a composite-sync source is the finding.** The separator is
not in the path, so nothing extracts vertical sync from the composite signal,
and the field rate is measured off a pin carrying edges only intermittently.

An earlier reading of this state has the vertical count wandering -- 98 / 235 /
884 / 1852, never 524. That does not survive denser sampling: the count reads
524 in every sample of every round. Point reads over HTTP over-report a steady
register as intermittent, which `docs/investigations/hperiod-if-railing.md`
establishes separately.

## What the test bus says

`/testbus?ms=25`, the same five selections either side, the sync type the only
thing moving:

| selector | `SYNC 0`, working | `SYNC 1`, stuck |
|---|---|---|
| 0x00 `InputVsync` | 2-4 transitions in every sweep | **0 in 5 of 12 sweeps**, 2 in the rest |
| 0x0a `SyncProcessor`, `vs_act_det` | 98 | **0 in all three rounds** |
| 0x0a `SyncProcessor`, retiming | 862 | 856-901 |
| 0x0a `SyncProcessor`, out proc | 0 | 0-4 |

So vertical sync activity is what stops arriving, and the retiming module beside
it carries the same rate throughout. That is consistent with the separator being
out of the path rather than with a stage having failed: `vs_act_det` has nothing
to detect on a composite signal the separator never saw.

**The readings above were taken before the instrument was sound**, on a sweep
that wrote `SP_TEST_MODULE` without `SP_TEST_SIGNAL_SEL` and enabled no
`PAD_BOUT_EN`. Both sweeps inherited the same signal, so they are comparable to
each other, and neither says which signal it read. The register evidence stands
without the sweep. `known-issues.md` carries what changed.

## What clears it

The recovery ladder's re-probe, and nothing before it. It sits at pass 44 now,
after the cheap sync-processor tweaks and before the rungs that cannot move a
sync path -- the overflow protect, which fires only on a source already held as
csync, the clock restart and the full reset. Measured there: the probe answers
`own V sync: no after 1001ms` 14.9 s into the failure, the separator goes into
the path, and the field rate reads 31318..31679 Hz against the 15.32 that stood
for the whole window before.

**It could not be moved without the livelock.** `own V sync found, the run
restarts` put the pass counter back to 2, and a separate-sync source answers yes
on every cycle, so the re-probe was a CEILING: whatever position it took, no
rung past it ever fired. At 151 that cost four rungs; at 44 it would have cost
eight. Own V sync is proof of a SOURCE, which is a reason not to move the mux --
so it is the input toggle's precondition now and the counter keeps climbing.

The cost was measured in both directions. Over sixteen `SYNC 1` / `SYNC 0` round
trips on the scaling path, 30 of 32 phases settled after the change against 12
of 12 on a smaller sample before it -- and the pre-existing intermittent
acquisition miss on this bench is 29 of 32, so 2 of 32 is that rate rather than
a new one. Composite legs settled in 4.7..16.0 s, separate in 4.8..16.1 s.

For reference, the ladder before the move:

```
recovery: release capture at pass 32
recovery: hsync overflow protect at pass 48
recovery: restart sampling clock at pass 60
recovery: full reset at pass 150
recovery: reprobe sync type at pass 151
own V sync: no after 1001ms
sampling: 521 lines x 59.76 Hz -> line rate 31195
```

**The reprobe was the rung that works and it was the last one.** Nothing before
pass 150 addressed the sync type, so the cheaper rungs each ran against a source
whose separator is out of the path and could not help: about 56 s absent, after
which the correct field rate arrived immediately.

## The same miss on the scaling path

320x256@50, six `SYNC 1` / `SYNC 0` round trips. Composite sync switched the
type in five of them (`own V sync: no after 1000ms`, `SP_SOG_MODE` 1) and in one
it was never probed at all -- the source was acquired at 308 lines inside a
second, on the separate-sync configuration, and the duty came out the complement:

```
duty: 2334 pulse / 2506 divider, htotal 2506, negative, NO EDGE
duty refused: 931/1000 outside 41..152, falling back to 70/1000
```

`2506 - 2334 = 172`, so the pulse was measurable and the reading was the
inverse of it. The guard refuses the 93% and substitutes a default rather than
taking the complement, so the capture window's head is placed from a number
nothing measured.

The register's two readings are `known-issues.md`'s own row. What this adds is
a trigger: the complement state correlates with the sync path the source is
being read on, and an unprobed type change is one way into it. On a settled
separate-sync source at 320x256@50 the same register reads 178 (7.10%) or 2330
(92.98%), `2506 - 2330 = 176`, so a duty read off it from outside the engine is
out by `1 - duty`.

## What is refuted

**A source returning from composite sync to separate sync re-acquires.** Twelve
phases over six round trips on 320x256@50, the return settling in 4.8-5.8 s
every time, `ch` 1145-1159, 311 lines, duty 7.06-7.11%, 15625 Hz, and a clean
photographed picture. The composite direction is the slow and variable one:
4.8 s, 8.9 s, 9.9 s, 7.9 s, 14.0 s, 28.4 s.

The round trip on the pass-through route is a different state and still fails.

## What the re-probe does not reach

**The scaling path never runs the ladder**, because it acquires the composite
source on the wrong path: the count is plausible and steady either way, so
nothing arms a re-probe and nothing fails. Measured over ten round trips, the
composite leg settled with `SP_SOG_MODE` 0 in five of ten, each with the duty
refused. `known-issues.md` carries it as its own row.

**And pass-through still does not acquire** -- 0 of 6 legs, with the separator
in the path, `HTOTAL` 1700..1720 against a `PLLAD_MD` of 2039 and `VTOTAL` 522.
That is the route rather than this fault: csync at the same 31.5 kHz acquires on
the SCALING path at `PLLAD_MD` 1566 with `HTOTAL` 1566. `known-issues.md` has
the comparison and the polarity lead.

**The field-rate bus was tried as the fix and reverted.** Every engine caller of
`TestBusRateMeasurement::sourceFieldRateHz` passes `false`, so the branch that
reads a csync source's vertical sync off the separator is unreachable from the
engine and the input V pin is counted instead -- which is what reads 15.32 Hz.
Making the selection follow the sync type is the tidier shape and it removes a
second owner, and it is measured WORSE where it matters: csync legs at
320x256@50 went from a 7.8..7.9 s acquisition to 11.9..18.5 s over ten legs,
median about 18 s, while pass-through still did not acquire. The separator's
out_proc output is evidently a poorer thing to time a field off than the pin is.
Do not reinstate it without measuring that leg.

## What arms it now

`VideoSourceAcquisition::sourceMoved()` counts consecutive passes on which the
held sync type is separate and `STATUS_SYNC_PROC_VSACT` reads 0, and arms a
re-probe at `VsyncAbsentArmPasses`. On the bench, `SYNC 1` produces
`source moved: no source V sync`, `own V sync: no after 1001ms`, and the csync
path with the coast in force; `SYNC 0` brings it back to separate H/V.

## Refuted: the line count is the cheap discriminator

The proposal was that `STATUS_SYNC_PROC_VTOTAL` read with the separator out
answers the sync type immediately -- the true count on a separate-sync source
and an implausible one on a composite source -- which would replace a probe that
concludes composite only by timing out.

Measured across two sources, two sync-processor parameter sets and both
separator states, automation frozen and every register written by hand so that
nothing but the source moves:

| parameters | separator | separate sync | composite sync |
|---|---|---|---|
| separate arm | out | 311 | **308** |
| separate arm | in | 97 | 97 |
| csync arm | out | 311 | **308** |
| csync arm | in | 97 | 97..107 |

And at 640x480@60, separate arm, separator out: **524 against 524**.

There is no cell where an implausible count identifies the composite source. At
the bench mode both readings are plausible and differ by the three lines the
vertical pulse occupies; at the second mode they are identical.

**The earlier table that showed a 50 against a 311 does not reproduce in any of
the eight cells.** Its two composite rows were taken under different
sync-processor parameters from its separate rows -- pulse ignore, coast and
`SP_DLT_REG` all differ between the arms -- so the reading tracked the
configuration. A discriminator has to be measured with everything but the source
held still.

**`STATUS_SYNC_PROC_HSPOL` is not one either.** It separates the two sync types
at 320x256@50, reading 1 and 0, and reads 0 for both at 640x480@60. It is a
polarity measurement that follows the mode, not the sync type.

## What is still open

The composite direction spends its time in two ways worth separating. One is a
polluted held rate: garbage readings walk the divider (30357 Hz to divider 1290,
4627 to 4012, 7352 to 2664), after which correct readings are refused against
the held value and `line rate 0` repeats for 12 s before it recovers. The other
is `duty: ... UNLOCKED`, where `STATUS_SYNC_PROC_HTOTAL` reads 2467 against a
`PLLAD_MD` of 2506 for about 4 s while every duty is read through an unlocked
PLL.
