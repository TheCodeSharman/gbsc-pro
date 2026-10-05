# The preset path's waits are measured dead

`applyPresets()` and `doPostPresetLoadSteps()` held four blocking waits worth
427 ms of a 615 ms preset load. Each was instrumented on the unit -- a build
printing the elapsed milliseconds and the condition at entry -- before any of
them was removed, because a wait that is load-bearing somewhere the bench does
not reach is removed once and diagnosed for a session.

## What each one cost

| wait | where | measured |
|---|---|---|
| `delay(400)` | after IF, VDS and DEC are released from reset | 400 ms, on a boot and on the first load after one |
| `delay(30)` | before the coast and clamp window placements | 30 ms, every load |
| `while (!hsyncActive())`, up to 2002 ms | after the block restart | **1 ms on a boot, 2 ms on component** |
| `delay(300)` | after the separator walk the wait above arms | **never reached** |

The paths, on `32f257d97`:

| path | `delay(400)` | hsync wait |
|---|---|---|
| vga boot, separate sync | taken | entered with hsync ALREADY ACTIVE, 1 ms |
| vga `/sc?#`, first after a boot | taken | not entered -- scaling RGBHV skips the block |
| vga `/sc?#`, settled | not taken | not entered |
| ypbpr `/sc?#` | not taken | entered with hsync ALREADY ACTIVE, 2 ms |

`doPostPresetLoadSteps()` ran 108 to 122 ms end to end, and the boot's own
`DETECT: found ... presets=` line put the whole of `applyPresets()` at 615 ms.

## Why the hsync wait never waits

**Nothing in a preset load stops the sync processor counting.**
`Chip::resetVideoBlocks()` releases `SFTRST_SYNC_RSTZ` rather than pulsing it,
and the block is never held anywhere else on the path, so the source is counted
straight through the load. `Adc::restartPll()` does disturb what it counts in,
but it runs some tens of milliseconds of I2C writes before the test, and the
count is back by then: the measurement is `hsyncActive()` true at entry on every
path, the boot's own load included.

So the wait costs one register read, and its `>= 1500 ms` arm cannot fire. What
that arm does -- walk the sync separator, re-apply the dynamic sync-processor
settings -- is `SyncRecovery`'s `LiftSogFloor`, `SyncProcessorDynamic` and
`ReopenSogSeparator`, at a cadence that does not hold `loop()`.

## Why the 30 ms settles nothing

The mode change is armed a hundred lines above it, so nothing measurable about
the source is true either side of the delay, and the two window placements it
precedes are undone twice before the function returns:
`SyncProcessor::forgetPositions()` runs after `resetVideoBlocks()` and again
further down, and the clamp window is placed a second time at the function's
tail. `loop()` places both again -- the clamp at `acquiredPasses() >= 4`, the
coast at `>= 7` -- against a source that has settled by then.

## Why the 400 ms is not the part's

It follows `SFTRST_IF_RSTZ`, `SFTRST_VDS_RSTZ` and `SFTRST_DEC_RSTZ` being
released, on a load that found the blocks held. The part asks for no such
settle: `Chip::resetVideoBlocks()` holds and releases the same chain back to
back on every mode change, with no delay between, and that is the path a
working picture comes back on.

## What it bought

| | before | after |
|---|---|---|
| `presets=` on a vga boot | 615 ms | **188 ms** |
| `DETECT:` total | 2525 ms | 2102 ms |

The picture is unchanged on every bench path: vga separate sync spread 109.4
before and 109.5 after, vga composite sync 108.7, a ypbpr boot 92.2, and
`/sc?#` on each leaves `/geometry` reading what it read before.

## What this does not license

**It is an argument about these four waits and not about blocking in general.**
The separator walk and the sampling-phase search are blocking by design and
`VideoSourceAcquisition` owns them -- they wait out a settling window a 20 ms
poll cannot express, which is why that class takes a clock and a watchdog feed.
The two `delay(100)`s in `applyPresets()`'s no-hsync branch are the settle of a
probe, between moving `ADC_INPUT_SEL` and reading what arrived, and a probe that
reads before its own write has landed measures the input it left.
