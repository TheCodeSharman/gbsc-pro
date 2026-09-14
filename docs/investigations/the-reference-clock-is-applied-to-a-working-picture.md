# The reference sampling clock is applied whether or not the picture is intact

**THE PASS-THROUGH HALF IS FIXED.** `prepareToMeasure()` returns before
`applyReferenceSampling()` when the output is passed through, and
`configurePassThrough()` holds `HdBypass::dividerFor(heldLineRateHz())` so the
engine's held divider is the channel's. What follows is what the fault was and
how it was measured; the scaling half below did not reproduce.

`VideoPath::prepareToMeasure()` applies the engine's reference sampling clock
before every measurement:

```cpp
void VideoPath::prepareToMeasure(uint16_t sourceLines)
{
    solveScanMode(sourceLines);
    sampling_.applyReferenceSampling(modeOversample_);
}
```

The reference exists for a good reason -- a count taken through the previous
mode's divider is not the source's, so a known divider is installed, the source
is measured against it, and the solve follows. `../capture-limits.md` has what
the value is: `referenceDivider()` is `WriteLimitUnits` expressed in ADC
samples, **2250 line-doubled and 1124 progressive**.

**The cost is that installing it destroys the picture.** That is acceptable on a
source whose picture is already gone, which is what a mode change means. It is
not acceptable on a source that is running, and two paths reach it that way.

## Pass-through: the channel's divider is not the engine's

`HdBypass::dividerFor()` sizes the channel from the line rate and caps at the
channel counter -- 2039 for anything at or above about 40 kHz, because
`MaxChannelLine - RasterGuardSamples` is reached before the clock bound.
`applyHorizontalFromChannelLine()` then writes `HD_HSYNC_RST` and `HD_HB_ST`
from that same number. So in pass-through the chip is sampling at the
CHANNEL's divider, and the engine's held divider is the reference.

Nothing puts the channel's divider back after a measurement.
`VideoSourceAcquisition::passSourceThrough()` moves the route only where it is
not already there -- re-running the switch on a working picture would drop sync
output -- so a source that STAYS passed through is left on whatever
`prepareToMeasure()` installed.

Measured at 800x600 on `vga`, RiscPC via ModeServ:

| | `DAC_RGBS_BYPS2DAC` | `PLLAD_MD` | `HD_HSYNC_RST` |
|---|---|---|---|
| at the entry | 1 | **2039** | 2047 |
| after a re-measure | 1 | **1124** | 2047 |

The raster stays sized for 2039 samples while the ADC delivers 1124, so the
picture keeps about half the line: photographed with the left third of the
panel black and the right-hand side cut off. Held coherent, the same source
fills the screen exactly, which is what pass-through does when the encoder gets
the source's own raster.

**This is what stops the two recovery ladders being merged.** `runSyncWatcher()`'s
no-sync branch excludes a bypassed source, and `RGBHVNoSyncCounter` is the
separate watch that covers it. Removing the exclusion lets `SyncRecovery` reach
pass-through, every rung ends in a re-measure, and the divider is clobbered on
the next pass.

## The fix, measured

RiscPC on `vga` via ModeServ, `preferScalingRgbhv` off, entered at 800x600 and
then moved to 1024x768 -- which also passes through, so the switch does NOT
re-run and `prepareToMeasure()` is the only thing touching the sampling clock:

    source moved: count (805 lines, solved 627)
    source acquired: 805 lines, 2039 samples against divider 2039
    sampling: 805 lines x 60.68 Hz -> line rate 48913

| | `DAC_RGBS_BYPS2DAC` | `PLLAD_MD` | `HD_HSYNC_RST` |
|---|---|---|---|
| at the entry | 1 | 2039 | 2047 |
| after a re-measure | 1 | **2039** | 2047 |

Photographed full screen and coherent. `2039 samples against divider 2039` is
the second half proving out -- before it the engine held whatever the last
scaling solve chose, so `dividerLatched()` compared the counted line against a
clock that was not delivering it and a passed-through source read as unlocked.

## Scaling: a re-arm is not proof the picture moved

`sourceMoved()` reports `interrupt` as well as `count` and `rate`, and the
interrupt fires on a source that has not moved -- the console prints the count
it saw beside the count it solved, so the two being equal is visible in the log:

```
source moved: interrupt (627 lines, solved 627)
```

`CLAUDE.md` states the same rule for reading that line. A re-arm with an
unchanged count re-installs the reference clock on a picture that was correct,
which is a visible disturbance for no measurement gained.

**IT DID NOT REPRODUCE, AND THE RECORDED EXAMPLE MAY NOT BE THIS PATH.** On the
settled bench source, 95 s of console carried no engine line at all -- no
interrupt, no re-arm, no solve. A 320x256 to 640x480 change converged in two
solves and two sync-type probes and then went quiet. The 627 lines in the line
above is 800x600, which is a pass-through candidate, so that observation may
have been the channel's half seen from the other side.

**DO NOT CONSUME THE LATCH TO FIX IT.** The interrupt re-arms are the retry loop
that lets the sync-type probe converge: removing them leaves the unit on the
csync path indefinitely, measured at 74 s with `SP_VTOTAL` 97. Any change here
needs a reproduction first, and there is not one.

## What is left

The reference clock is the right first move when the sampling in force cannot
measure the source, and the wrong one when it can. `prepareToMeasure()` now asks
which route is carrying the video, which is the question that had an answer.

**THE TWO RECOVERY LADDERS HAVE MERGED.** The divider is no longer clobbered by
a rung that ends in a re-measure, which was the whole reason the
`!rgbhvBypass()` gate and `RGBHVNoSyncCounter` were load bearing, and both are
gone. What the empty-input reproduction showed is that the exclusion was also
costing the rungs entirely: an RGBHV source that was not scaling got
`RGBHVNoSyncCounter`'s single reset every ~33 s and none of `SyncRecovery`'s
eleven.
