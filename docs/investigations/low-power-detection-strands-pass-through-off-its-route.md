# Low power detection strands pass-through off its DAC route

`/sc?~` on a source running in RGBHV pass-through leaves the unit with no
picture and no way back. The engine goes on measuring the source and sizing the
pass-through channel, while the chip's output routing sits on the scaling path
that detection put it back on.

Reproduce at 800x600@60 on `vga`: `/uc?x` to turn `preferScalingRgbhv` off and
settle in pass-through, then `curl '.../sc?~'`. `/uc?x` recovers it in under two
seconds; `/sc?~` does not, and neither does a source mode round trip.

## What the state is

| | engine | chip |
|---|---|---|
| in pass-through | `state: acquired` | `DAC_RGBS_BYPS2DAC` 1, `OUT_SYNC_SEL` 1, `PLLAD_MD` 2038 |
| 20 s and 40 s after `/sc?~` | `state: absent`, `present: false` | `DAC_RGBS_BYPS2DAC` 0, `OUT_SYNC_SEL` 0, `PLLAD_MD` 1912, `STATUS_SYNC_PROC_VTOTAL` 627 |

**The sync processor counts the source perfectly throughout.** `VTOTAL` reads
627 in the same pass that reports the source absent, so nothing about the
measurement path is broken.

`/geometry` does NOT read all zeros here. It reports the last scaling solve's
window, because pass-through never writes one, and the engine's `state` is what
distinguishes the two.

## The mechanism

`passSourceThrough()` claims the DAC route only on the way IN:

```cpp
if (!outputIsPassedThrough()) {
    passThroughSwitch_();
} else {
    resizePassThrough();
}
```

`passThroughSwitch_()` is the only caller of `Chip::enterHdBypass()`, which is
what writes `DAC_RGBS_BYPS2DAC` 1 and `OUT_SYNC_SEL` 1. `outputIsPassedThrough()`
asks the engine's own held `videoPath_.outputMode()`, not the chip.

`goLowPowerWithInputDetection()` runs `setResetParameters()` and the bring-up
behind it, which routes the DACs back to the scaler. The held output mode is
untouched, so the engine still believes it is passed through, takes the `else`
branch on every pass afterwards, and re-sizes a channel whose route nobody
holds.

`setOutputResolution()` states the same hazard for its own case -- taking the
chip off the bypass route with nothing telling the engine leaves it holding a
mode the chip is no longer in -- and guards against causing it there. Nothing
guards against detection causing it.

## Two readings this refutes

**That nothing arms.** Measured twice across the deadlock with the console
attached, arms fire throughout:

```
source moved: unusable count (0 lines, solved 627)    just after /sc?~
source moved: interrupt (627 lines, solved 627)       12 s in, source steady
source moved: interrupt (524 lines, solved 524)       the round trip, SOLVED
source moved: count (627 lines, solved 524)           and back
```

`solved 627` at the first of them is the point: `solvedLines_` is not zero, so
the cold-state guard on `solvedLines_ == 0 && outputIsPassedThrough()` is not
what returns, and a hypothesis resting on that guard being terminal does not
describe this fault.

**That a source mode round trip does not clear it.** It clears the engine's
acquisition and not the picture. 800x600 -> 640x480 -> 800x600 takes the state
back to `acquired` within 20 s, with the routing still 0/0 and the screen still
dark. Acquisition recovering and the picture recovering are separate events
here, and judging the round trip by the screen is what made the arms look
absent.

## Why `/uc?x` recovers it

Turning `preferScalingRgbhv` on makes `passThroughSuitsSource()` false, so
`resolveFromSource()` reaches its `outputIsPassedThrough()` branch and calls
`setOutputMode(resolution_)`. That is the one path that moves the held mode out
of bypass, after which the scaling path is configured in full, routing included.

This is why recovering with `/uc?x` proves less than it appears to: it changes
the output mode, which is an input to several branches, rather than isolating
one.

## Open

`PLLAD_MD` reads 1912 in the deadlock, a value belonging to neither path --
1606 scaling, 2038 pass-through -- and is stable across samples 20 s apart. And
at 640x480 after the round trip it reads 2038, the 800x600 pass-through
divider, against a 524-line source. Whether `resizePassThrough()` is reaching
those states at all is not established.
