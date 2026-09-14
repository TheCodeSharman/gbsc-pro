# The reference sampling clock is applied whether or not the picture is intact

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

## What the fix has to separate

The reference clock is the right first move when the sampling in force cannot
measure the source, and the wrong one when it can. The discriminator already
exists in the values `sourceMoved()` carries, and on the pass-through route the
channel's divider is already held by `HdBypass`. What is missing is that
`prepareToMeasure()` asks neither question.

Until it does, the `!rgbhvBypass()` gate and `RGBHVNoSyncCounter` are load
bearing and removing either one breaks pass-through.
