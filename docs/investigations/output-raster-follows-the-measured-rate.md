# The output raster is not deterministic for a fixed source

`Geometry::solveRaster()` derives the horizontal total from the frame height and
the **measured** field rate at a fixed clock. That measurement wobbles, and the
raster follows it — so two solves of the same source, minutes apart, produce
rasters several pixels apart. Nothing re-solves the raster afterwards, so
whichever value the solve happened to land on is the one the picture runs at
until the next mode change.

Measured on the bench RiscPC at 320x256@50, output 1080p:

```
after /sc?)   raster 1916x1125  HSCALE 524  VDS_DIS_HB_ST 1849   lineRate 15575
after /sc?B   raster 1911x1125  HSCALE 526  VDS_DIS_HB_ST 1842   lineRate 15622
```

The arithmetic closes exactly, which is what identifies the cause:

| lineRate | field rate | horizontal total | implied clock |
|---|---|---|---|
| 15575 | 50.080 Hz | 1916 | 107.948 MHz |
| 15622 | 50.232 Hz | 1911 | 107.991 MHz |

Same clock to 0.04%. A 0.3% move in the measurement is a 5 px move in the
raster, and `VDS_HSCALE` and both output windows follow it.

It is not a per-path difference. Six consecutive `/sc?B` cycles gave 1916 every
time with the rate steady at 15575; the 15622 reading is the jitter, not the
path.

## What this refutes

`test_a_preset_load_leaves_the_engines_values_not_the_sketchs` reported these
four registers as *"doPostPresetLoadSteps() won the race for them"*. It does not
and cannot:

- `loadComputedPreset()` writes flags only — `GBS_PRESET_ID`, the option bits,
  and `rto->` state. No raster register.
- `doPostPresetLoadSteps()` never writes `VDS_HSYNC_RST`, `VDS_HSCALE`,
  `VDS_DIS_HB_ST` or `VDS_HB_ST`. It only *reads* `VDS_DIS_HB_ST`, to copy into
  the `VDS_EXT_HB_*` pads, which are disabled.

The engine wrote all four, both times. The test compares two solves taken
against two different measurements and attributes the difference to a writer
that no longer exists — the scaling preset tables having been deleted.

**The direction gives it away.** A race would favour one path consistently; here
the load path left the *larger* raster and the reset path the smaller, which is
the reverse of what the test's message describes.

## The live consequence

A test that compares raster-derived registers across two separate solves is
comparing two measurements, and will fail intermittently on a healthy unit. Any
such comparison has to establish that the measured rate agreed at both points,
or restrict itself to registers the rate does not reach.

Whether the raster *should* track the measurement this closely is a separate
question and is not settled here. `docs/firmware-geometry-engine.md` records
the ordering constraint the raster solve sits inside; nothing re-solves it once
chosen.
