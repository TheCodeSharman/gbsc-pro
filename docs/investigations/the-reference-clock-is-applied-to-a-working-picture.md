# The reference sampling clock is applied whether or not the picture is intact

**BOTH HALVES ARE FIXED, AND THE REFERENCE CLOCK ITSELF IS GONE.** The divider
is now sized from the rate measured, and installed between the two halves of a
measurement rather than before it -- so nothing is applied to a working picture
except where the ADC is not clocking at all.
`the-reference-divider-was-the-bootstrap.md` is what replaced it.

In pass-through `prepareToMeasure()` still returns before installing anything,
and `configurePassThrough()` holds `HdBypass::dividerFor(heldLineRateHz())` so
the engine's held divider is the channel's. On the scaling path an arm spends the
latched disturbance it was raised for, and the sync-type probe acknowledges the
switch its own write makes. What follows is what each fault was and how it was
measured.

`VideoPath::prepareToMeasure()` applied the engine's reference sampling clock
before every measurement:

```cpp
void VideoPath::prepareToMeasure(uint16_t sourceLines)
{
    solveScanMode(sourceLines);
    sampling_.applyReferenceSampling(modeOversample_);
}
```

The reference existed for a good reason -- a count taken through the previous
mode's divider is not the source's, so a known divider was installed, the source
measured against it, and the solve followed. Its value was `WriteLimitUnits`
expressed in ADC samples, **2250 line-doubled and 1124 progressive**. What
replaced it is the count corrected against the divider it was read through,
which needs no second clock.

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

## Scaling: the latch outlives the change it belonged to

`sourceMoved()` reports `interrupt` as well as `count` and `rate`, and the
console prints the count it saw beside the count it solved, so an arm on a
source that has not moved is visible in the log:

```
source moved: interrupt (311 lines, solved 311)
```

**IT REPRODUCES ON THE 640x480 -> 320x256 LEG, EVERY TIME.** That leg is the one
that changes the scan mode, so the divider in force describes the wrong line and
the count wanders before it settles. With a temporary line printed where the
latch is taken, the whole sequence is one mode change:

```
27.15  disturbance taken, no change yet in flight     <- the source's own change
27.56  source moved: unsettled count (227 lines, solved 524)
28.41  externalClockGenSyncInOutRate()                <- the solve
28.68  source moved: interrupt (311 lines, solved 311)
29.53  externalClockGenSyncInOutRate()                <- a second solve
```

**THE LATCH IS SPENT BY WHICHEVER BRANCH READS IT, AND TWO BRANCHES ARM WITHOUT
READING IT.** By the time the disturbance lands the count is already outside the
source bounds, so the branch that reads the latch is unreachable; the change is
armed by the unsettled-count arm instead, which leaves it set. It then fires the
moment the solve lands, on the count that solve has just measured. The reverse
leg arms on the interrupt itself, spends it, and shows no second solve -- which
is the asymmetry, and why one direction of a pair costs more than the other.

`VideoSourceAcquisition::armMove()` is the one place an arm is reported, and it
spends the latch whichever reason it names. The re-measure an arm opens answers
whatever disturbed the separator, so nothing is lost.

**THE PROBE IS THE SECOND OWNER OF THE SAME LATCH.** `SyncMeasurement::hasOwnVsync()`
answers by writing `SP_EXT_SYNC_SEL` and writing it back, which takes the sync
separator in and out of the path -- and the chip latches that exactly as it
latches the source's own disturbance. Measured leaving composite sync, where the
separator is in circuit:

```
76.27  externalClockGenSyncInOutRate()
76.57  source moved: interrupt (311 lines, solved 311)
77.46  externalClockGenSyncInOutRate()
```

The probe acknowledges its own switch. The measurement it runs for is what
answers a disturbance arriving inside its window, so a real one is not lost
either.

**THE RETRY LOOP SURVIVES BOTH, AND THAT IS MEASURED.** The warning this page
carried was that consuming the latch leaves the unit on the csync path -- 74 s
with `SP_VTOTAL` 97. It does not: neither change stops a latch being taken, only
one already answered from being read twice. Walked both ways on the RiscPC with
`SYNC 1`/`SYNC 0`, composite sync converges in 8.5 s to `SP_SOG_MODE` 1,
`STATUS_SYNC_PROC_VTOTAL` 308, `PLLAD_MD` 2050 against `STATUS_SYNC_PROC_HTOTAL`
2050 and `HPERIOD_IF` at the 431 that mode is due, and separate sync comes back
in one solve. Interrupt arms still fire on both legs where the source really
moved.

Each fault costs the leg one arm, one sync-type probe and one solve -- about
1.1 s of the transition, and an encoder relock in the middle of it.

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
