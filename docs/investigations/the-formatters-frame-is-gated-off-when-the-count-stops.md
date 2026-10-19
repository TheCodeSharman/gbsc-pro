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
bit does not gate. **The first route is the one taken**, in `rateMoved()`
rather than in the frame -- see below.

## The bit is what separates the mode that wedges from the ones that do not

Four mode changes out of 320x256@50 on `vga` composite, nothing else touched,
each given 45 s to reach a geometry that holds for 8 s:

| into | settles | `STATUS_IF_VT_OK` |
|---|---|---|
| 800x600@60 | 0.6 s, `cv` 628, divider 1342 | 1 |
| 640x480@60 | 1.5 s, `cv` 525, divider 1272 | 1 |
| 240x352@70 | 4.7 s, `cv` 449, divider 1272 | 1 |
| 640x512@50 | never -- `state: acquired` on the DEPARTING mode's `cv` 624 and divider 2200, count 308 | **0** |

The mode-change path is healthy on three of four. The one that wedges is the
one whose formatter stops reporting, and that is the same bit the guard reads.

## The line period carries the arm where the field rate cannot

`rateMoved()` is the instrument that was left, and it refused for a reason of
its own: the cheap half saw `HPERIOD_IF` move, the expensive half could not
measure a field rate at all, and `VideoSignal::isVideo()` failing was read as a
reason to do nothing. **A reading that is missing cannot refuse one that
moved**, so a line period that moved now arms on its own where the field rate
is unmeasurable, and `forgetHeldRate()` goes with it -- the count has not
moved, and `measureLineRate()` refuses a rate that changed at an unchanged
count.

One mode change each way on `vga` composite, 320x256@50 -> 640x512@50, nothing
else touched:

| | first arm | as |
|---|---|---|
| before | 22.97 s | `contradicted count`, the bit having come good at ~21.8 s |
| after | **4.37 s** | `TIMINGS change (rate), 312 lines, solved 312` |

Through the whole window before the arm the console printed
`vsyncEdges(): no INPUT vsync` every pass, which is the quiet pin this keys on.

## Why the healthy states stay quiet

The bit reading 0 is the normal state on separate sync, so the arm had to
separate a gated measurement from a gated signal, and it does because they are
different things: `TestBus::selectInputVsync()` drives the formatter's vertical
**signal** onto `DEBUG_IN_PIN`, where `STATUS_IF_VT_OK` gates only its
**measurement register**. A source whose bit reads 0 while it is well therefore
still times a field rate, and the arm never reaches the period.

Measured with the console open and nothing else moving: the bench RISC PC on
separate sync for 120 s, the same source on composite for 90 s, and the Wii on
sync on green at 720p for 90 s -- no arm in any of them, and the console
silent but for auto gain settling.
