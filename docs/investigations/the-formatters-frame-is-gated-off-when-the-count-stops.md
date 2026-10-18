# The formatter's frame is gated off when the count stops

`VideoSourceAcquisition::sourceMoved()` tells a sync processor that has STOPPED
counting from a source standing still by asking a second instrument: the input
formatter's own frame, through `SourceMeasurement::frameMovedFrom()` and
`countContradicted()`. Both read `InputFormatter::verticalPeriod()`, which
returns **0 whenever `STATUS_IF_VT_OK` reads 0** -- and on `vga` composite after
a mode change into 640x512@50 that bit reads 0 for as long as the state lasts.
A period of 0 is "no reading", and both guards map it to "nothing moved", so
the engine holds a dead count indefinitely.

## What the guard sees

Instrumented in `sourceMoved()`, on `vga` composite, 320x256@50 -> 640x512@50
by ModeServ and nothing else touched:

```
probe: lines 312 solved 312 frame 0 solvedFrame 623 contradicted 0 moved 0 passes 0
```

`frame 0` is the whole finding. `VPERIOD_IF` itself reads **1056..1078 across
53 samples** in that state -- half-lines of the ARRIVING 534-line frame, the
right answer -- while `STATUS_IF_VT_OK` is 0 and `STATUS_IF_VT_BAD` is 1, so
the accessor throws the reading away.

`lines` equals `solvedLines_` because `SourceMeasurement::countFrom()` returns
the held frame 312 where the corrected count is 309 and the two are within
`VerticalSyncMaxLines`, so the inner guard's first clause holds and its second
-- the one that exists to break the tie -- can never answer.

## Three instruments and all of them blind

| instrument | reading in this state |
|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | **308 for over five minutes**, frozen by the departing mode's coast window at `SP_H_CST_SP` 1672, `STATUS_SYNC_PROC_HLOW_LEN` 0 |
| the formatter's frame | 0, gated off by `STATUS_IF_VT_OK` |
| `TestBusRateMeasurement::sourceFieldRateHz()` | no vsync edge on `DEBUG_IN_PIN` -- the console prints `vsyncEdges(): no INPUT vsync` every pass, so `rateMoved()`'s expensive half fails `VideoSignal::isVideo()` and returns false |

The one register that did move is `HPERIOD_IF`, 431 when solved against 251
here, and it is the cheap half only: it opens the expensive half rather than
arming anything, and the expensive half is the third instrument above.

Throughout, `/geometry` reports `state: acquired`, `present: true` and the
departing mode's timing, and `printInfo()` reads `u: 0 s: ff` -- the acquired
run saturated at `AcquiredPassCeiling`. Detection passes run at
`DetectionIntervalMs` 20 ms and `ContradictedArmPasses` is 64 of them, so the
arm is 1.3 s away whenever it is reachable at all. `/sc?~` clears the state:
534 lines, divider 1746, acquired.

## Where the bit comes good the guard fires

The same mode change with `STATUS_IF_VT_OK` 1 arms promptly. In one capture the
IF status byte read 9 (`HT_OK | VT_BAD`) from the change onward and flipped to
3 (`HT_OK | VT_OK`) at 21.8 s; the arm fired at 24.4 s, 22 ms before the sync
processor started counting again. In another it fired 2.3 s after the pad was
driven. So the guard works and its availability is what varies.

## What this costs the rule that `STATUS_IF_VT_OK` explains nothing

That rule held while the bit gated only `Deinterlacer::steer()`. Since the
second instrument was added it gates acquisition too, and a bit documented as
reading 0 in the normal course on some sync arrangements now decides whether
the engine can notice that its own count has died. Either the guard must not
read "no reading" as "no movement", or the frame has to come from somewhere the
bit does not gate.
