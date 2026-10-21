# An absent source is not a sync type

A source that has gone away is indistinguishable from a composite-sync source
by every instantaneous measurement the chip offers. It drives horizontal sync,
it drives no vertical, and the separator put into the path extracts a count.
That is the definition of composite sync, and it is also what a RISC PC looks
like for about eight seconds after a Ctrl-Break reboot.

Two arms in the idle acquisition path read that state as evidence about the
sync type and discarded the held answer. The probe that followed ran into the
blackout, concluded composite, and latched -- after which `SP_SOG_MODE` 1 with
`ADC_SOGEN` 1 sought hsync on green, where a separate-sync RGBHV source puts
none, `STATUS_SYNC_PROC_VTOTAL` read 97 or 99, and `PAD_SYNC_OUT_ENZ` was left
1 so nothing reached the encoder at all. `/sc?~` recovered it in about five
seconds by forgetting the answer.

## Both arms, each caught on a different reboot

```
measure: no source V sync, re-probing the sync type
measured: own V sync no, composite or SOG after 1001ms
```

`sourceStoppedDrivingVsync()` armed after `OwnVsyncSettleMs` of V absence --
about 240 ms, sized from the sync processor's reacquisition rather than from
anything a source does.

```
measure: start, TIMINGS change (unusable count), 5 lines, solved 750
```

The unusable-count arm, on the reading that a count no source runs is the wrong
path's signature.

## Why steadiness does not separate them, though it was already required

The unusable-count arm already demanded a settled count: `if (!held ||
unusableCountArmed_) return false`. The count of 5 above satisfied it. A source
that is off holds a small constant for as long as any steadiness run needs, so
requiring the bad count to settle rejects nothing.

Nor does the VALUE separate them. A wrong sync path collapses
`STATUS_SYNC_PROC_VTOTAL` to 97, and so does a source with no signal -- the
same number from opposite causes.

## What the counts actually were

Across one reboot, with the engine unable to read the source for about eight
seconds:

| t | count | field rate |
|---|---|---|
| 46.81 | 270 | 0.00 Hz |
| 47.79 | 625 | 0.00 Hz |
| 47.81 | 313 | 52.17 Hz |
| 52.41 | 338 | 50.17 Hz |

Individually plausible, never twice the same, and none of them a frame. An
intermediate fix that asked the separator for two agreeing counts 20 ms apart
passed here, because a rebooting source clears that easily -- the console said
`own V sync no, the separator finds V` and latched composite anyway.

## What holds instead

Nothing in the idle path concludes anything about the sync type. The
unusable-count arm still arms a re-measure, which is correct -- the source did
move -- and leaves the held answer alone. `sourceStoppedDrivingVsync()` is
deleted with its state and both call sites.

The probe itself answers in three states rather than two, so one taken with
nothing to measure latches nothing: where no V arrives on its own pin it puts
the separator IN and asks whether a frame can be extracted at all. That is a
positive test in both directions rather than a conclusion drawn from absence,
which is what `the-own-vsync-probe-answers-by-absence.md` proposed as the
mirror test and recorded as not needed.

**The capability given up is following a sync-type change on a live source.**
`/sc?~`, an input change or a re-detect re-establish the type. That is the same
limitation `known-issues.md` already carries for a source that changes its sync
type on one cable, widened from mode changes to every idle pass.

Measured after the deletion: four reboots, all acquired, recovery 2.48 s,
0.97 s and 2.54 s, no probe run and no composite conclusion, with the
unusable-count arm firing twice and keeping the held type.
