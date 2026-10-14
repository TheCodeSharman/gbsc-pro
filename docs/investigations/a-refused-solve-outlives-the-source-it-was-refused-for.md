# A refused solve outlives the source it was refused for

An input excursion that crosses the pass-through permission can leave the
engine measuring the arriving source correctly on every pass and never
presenting it. `/geometry` reads `state: acquired` with **`present: false`**,
and the capture fields hold the *departing* source's numbers beside the
arriving one's line rate.

The console cycles three lines at a few hertz indefinitely, and the measurement
in them is healthy:

```
rate match: the HD bypass channel carries the video
measured: 524 lines x 59.93 Hz -> line rate 31468, clock 2038@31468 htotal 2038
duty: 152 pulse / 2038 divider, htotal 2038, negative
```

`/restart` clears it. Nothing else tried did.

## What `present` is

`VideoSourceAcquisition::sourceIsPresent()` is `SourceAcquired &&
!VideoPath::changing()`, and `changing()` is `modePending_ || solvePending_`.
So `acquired` beside `present: false` is not a contradiction about the source
at all -- it says one of those two flags is stuck, and the state says which.
`configurePassThrough()` runs on every accepted pass-through pass and clears
`modePending_`, so the stuck one is `solvePending_`.

## How it gets stuck

`solveFromMeasurement()` ignores what `solveWindows()` returned. A refused
solve therefore sets `solvePending_` while the same pass clears `modePending_`
and reports `PollSolved`, which is the intended retry: the next pass finds the
deferred solve and runs `resolveFromSource()` against a settled source.

Pass-through is where that retry has no exit. `resolveFromSource()` answers the
route before it solves anything, and the pass-through arm returns `true`:

```cpp
if (passSourceThrough())
    return true;
```

Nothing on that path writes a window, so nothing discharges the deferred solve,
and `runPass()` takes the same branch on the pass after it. The cost is not
only the verdict:

- `poll()` leaves presenting to the passes that did **not** solve, so
  `presentWhenSettled()` is never reached and the output is never given back.
- `keepSourceComing()` asks `sourceIsPresent()` to choose between
  `noteSourceAbsent()` and `maintainSource()`, so a source with a picture on
  the panel is maintained as an absent one.

## The measurement

`vga` scaled, the permission granted, then `/input?src=ypbpr`. Reproduced on
the second round of two; the state then held for as long as it was watched,
with `STATUS_SYNC_PROC_VTOTAL` counting the arriving source perfectly
throughout.

| | `state` | `present` | capture | `lineRateHz` |
|---|---|---|---|---|
| stuck | acquired | **false** | 1443x750 | 31465 |
| settled | acquired | true | 1443x750 | 31468 |

**AND THE CAPTURE PAIR IS NOT A WITNESS, THOUGH IT READS LIKE ONE.** A window
of 1443x750 -- the departing VGA 720p's -- beside the arriving Wii's line rate
looks like one source measured and another one's geometry. It reads the same in
the settled state, because pass-through solves no window and the fields are
held state: what they carry is whatever the last SCALING solve left, which is
the departing source after an excursion that crossed the permission and 0x0
after a restart. `present` is the only reading that separates the two.

After the fix, eight rounds of the same crossing: 16 selections, every landing
`present: true`, and no landing read acquired-and-absent on any of fifteen
one-second samples. Before it, the same sequence stuck on the second round of
two.

## Why it does not fire on every excursion

Both halves have to land in the same window: a solve refused against the
departing source, and an arriving source the route hands to pass-through. An
excursion between two sources that are both already passed through attempts no
solve, so there is nothing to refuse -- ten consecutive selections with
`scale-rgbhv` clear came back presented. Crossing the permission in the middle
of the excursion is what puts a refusal and a hand-over in the same window.

## The fix

`configurePassThrough()` clears both flags. It is the one place both arms
reach -- `passSourceThrough()` ends in `setOutputMode(&ModeBypass)` whichever
branch called it -- and it is already where the mode change is discharged, for
the same reason: pass-through solves nothing, so neither pending act has
anything left to wait for.

`sizeCaptureWindow()` had already reached the same conclusion from the other
end, clearing `solvePending_` where no raster is solved.
