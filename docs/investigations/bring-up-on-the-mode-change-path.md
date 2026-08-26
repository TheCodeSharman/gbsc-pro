# The bring-up on the mode-change path is not redundant

**Status: RESOLVED. The guard is in, and the answer was the arming rule rather
than the field list.** What the bring-up inside the load did that nothing else
did was undo *bypass*, so the bypass switches arm it. An ordinary mode change,
with no bypass in it, skips the re-init it never needed. The history below is
kept because three attempts failed on the same misreading — that the second
bring-up was a duplicate of the first — and the evidence that killed each one is
what named the real rule.

`Tv5725::BringUp::init()` runs twice on a settled unit: once in `setup()`, and
again as the first statement of `doPostPresetLoadSteps()`. The second looks
redundant once the first exists, and removing it is attractive — the whole point
of moving bring-up to boot is that a mode change should not re-initialise the
chip.

It is not redundant. Removing it changes seven fields and the output stops
working.

## What was tried

`BringUp::armed()`, a static flag set by `holdAllBlocks()` and cleared by
`init()`, with the call in `doPostPresetLoadSteps()` guarded on it:

```cpp
if (Tv5725::BringUp::armed())
    Tv5725::BringUp::init();
```

The reasoning: `holdAllBlocks()` is what discards a block's configuration, and
`setResetParameters()` calls it, so low power and the RGBHV watchdog re-arm for
free. `setOutModeHdBypass()` holds the blocks and then reaches
`doPostPresetLoadSteps()`, so it re-arms too. `bypassModeSwitch_RGBHV()` holds
nothing and needs only that a bring-up has happened, which boot now guarantees.
Everything else — an ordinary mode change — should find the flag clear and skip.

## What the bench said

Measured twice. The second run is the one to trust: both builds were reached by
an identical route — OTA flash, wait for the configuration registers to hold
still, `/uc?a` restart, wait again — because the first run compared a cold-boot
route against a restart route, and a diff across routes reports the route.

**The unit falls into RGBHV bypass on a source that should scale.** The bench
RiscPC at 320x256@50 measures 311 lines, far below the 535-line threshold, and
the guarded build routes it through bypass anyway:

| field | brought up | bring-up skipped |
|---|---|---|
| `DAC_RGBS_ADC2DAC` | 0 | **1** |
| `OUT_SYNC_SEL` | 0 | **1** |
| `PAD_SYNC1_IN_ENZ` | 1 | **0** |
| `PAD_SYNC2_IN_ENZ` | 1 | **0** |
| `PAD_TRI_ENZ` | 0 | **1** |

Those five are `bypassModeSwitch_RGBHV()`'s own writes, so the firmware did not
drift into that state — it *chose* it. The VDS meanwhile still holds a scaled
raster, which is why the picture is wrong rather than absent: the output is
routed ADC-to-DAC while the scaler is configured to scale.

The rest of the difference follows from that. Fourteen more fields move, and the
informative ones are the display PLL and the ADC PLL — `PLL_IS`, `PLL_MS`,
`PLL_ADS`, `PLL_VCORST`, `PLLAD_FS`, `PLLAD_ICP` — plus `SFTRST_HDBYPS_RSTZ`
released where a bring-up holds it, `MEM_PAD_CLK_INVERT`, and the two
auto-polarity bits. The raster itself lands at 1915 brought up and a stable 1910
skipped, with `VDS_HSCALE` 436 against 437, so the solve is running against a
differently seeded display clock.

## Why bypass, and what it means

**It is not `ModeDetect`.** The obvious explanation — that `ModeDetect::init()`
sets the thresholds a standard is *named* by, so skipping it changes the
classification — is refuted. `resetModeDetect()` pulses `SFTRST_MODE_RSTZ`, and
pulsing it by hand on a settled unit leaves every threshold at the value
`ModeDetect::init()` wrote: `MD_HPERIOD_LOCK_VALUE` 22, `MD_NTSC_INT_CNTRL` 32,
`MD_PAL_INT_CNTRL` 38, `MD_VGA_CNTRL` 62, `MD_SVGA_60HZ_CNTRL` 177,
`MD_XGA_CNTRL` 99, `MD_SXGA_CNTRL` 133, `MD_HD720P_CNTRL` 93, and all 1536
registers unchanged. The block reset does not clear its configuration, so the
boot bring-up's thresholds stand for the life of the power cycle.

**Nor can the register diff say. It shows the destination, not the decision.**
Of the nineteen fields, eleven are written by `bypassModeSwitch_RGBHV()` itself
— `DAC_RGBS_ADC2DAC`, `OUT_SYNC_SEL`, the three pad enables, `MEM_PAD_CLK_INVERT`,
`PLLAD_FS`, `PLLAD_ICP`, `PLL_ADS`, `PLL_MS`, `SFTRST_HDBYPS_RSTZ`. Two more,
`SP_HS_POL_ATO` and `SP_VS_POL_ATO`, come from `SyncProcessor::applyForSyncType()`,
which that switch calls. The four `VDS_*` are the scaled raster left stale behind
a path that does not use the VDS. So the diff is the consequence of the choice
throughout, and reading a cause out of it — the ADC PLL was the tempting one,
since the bypass decision is a line count — is reading the destination as the
reason for the journey.

The decision is taken in `runSyncWatcher()`, against a 535-line threshold, long
before any dump. Two things can reach it and neither has been run: the serial
console on a guarded build, which names detection's reasoning, or `SP_VTOTAL`
sampled while the choice is being made.

## What that implies for the sequence

Deleting the second bring-up needs the fields it repairs to have an owner on the
scaling path first, and it needs `ModeDetect`'s thresholds to be established
somewhere the mode-change path can rely on. That is the per-subsystem work, and
it has to come first.

`SP_HS_POL_ATO` and `SP_VS_POL_ATO` are already out of this list: they moved to
`SyncProcessor::applyForSyncType()`, which the mode-change path calls whether or
not a bring-up runs. They still appear in the diff above because the run is a
bypass one, and that path sets them for separate sync.

## A note on the photographs

Every "no picture" reading in the first run was taken about a second after the
change, which is not long enough for the encoder to re-acquire and the set to
paint — a clean 800x600 bypass state photographed that way came out black, and
came out correct after a delay. **A photograph is evidence only once the encoder
has re-acquired.** Allow several seconds after anything that changes the output
timing, a register restore or a reflash included; `tv-snap -w 400` spends them in
the camera's warmup. The second run's wrong picture was confirmed by eye, not by
a hurried photograph.


## The DAC routing, found the same way

The guard was landed and reverted again on 2026-08-26 after passing boot, a
restart and entry into bypass, and failing the return.

Coming back from RGBHV bypass to a scalable source, the firmware reaches the
right conclusion and does not act on it: the sync watcher logs
`rgbhv-leave-bypass,311,15` then 770 consecutive `rgbhv-keep-scaling,311,14`,
so it believes it is scaling — while `DAC_RGBS_ADC2DAC` and `OUT_SYNC_SEL` are
still routed to the bypass channel. The engine solves a scaled raster
underneath, every register reads plausibly, and the picture is the bypass one.

Both fields are `Chip::init()`'s. `bypassModeSwitch_RGBHV()` sets them and only
the bring-up inside the load put them back, which is the same shape as
`PLL_VCORST` and wants the same answer: the class that owns the scaler's output
routing writes it when a non-bypass output mode is applied.

**The lesson about testing, which cost this a third round:** entering bypass and
returning are two transitions, not one. The entry was tested and the return was
not, and the commit went in on the strength of the half that passed.

## The full list, measured

Fourth attempt, 2026-08-26, with `DisplayClock::select()` owning the display PLL
and `Chip::routeToScaler()` owning the DAC routing. The guard now passes boot and
a restart with an EMPTY 1536-register diff, and the routing returns correctly
from bypass. What it still fails is the state a bypass excursion leaves behind:

| field | owner it wants |
|---|---|
| `PLLAD_FS`, `PLLAD_ICP` | `Adc` — the ADC PLL, and mode-dependent anyway |
| `PLL_ADS`, `PLL_MS` | `DisplayClock` / `Chip` |
| `PAD_SYNC1_IN_ENZ`, `PAD_SYNC2_IN_ENZ`, `PAD_TRI_ENZ` | `Chip` — the input pads |
| `MEM_PAD_CLK_INVERT` | `MemoryBus` |
| `SFTRST_HDBYPS_RSTZ` | `HdBypass` — held on the scaling path |
| `SP_HS_POL_ATO`, `SP_VS_POL_ATO` | `SyncProcessor` — it has one, but only the `scalingRgbhv()` branch calls it |

Every one is written by a bypass path and claimed back by nothing on the scaling
path. **That is the whole of what the bring-up inside the load was doing**: not
bringing the chip up, but undoing bypass.

**Which is the answer, and it is not eleven owners.** If the list is exactly
"what bypass changed", then the thing that invalidates a bring-up is entering
bypass, and the arming rule should say so. `BringUp::arm()` called from both
bypass switches leaves the whole list to the bring-up that already knows how to
write it, and leaves an ordinary mode change skipping a re-init it never needed.
Measured: EMPTY after boot, EMPTY after a restart, and one run-variable field
after a bypass excursion and back.

The two fields that did earn owners -- `PLL_VCORST` through
`DisplayClock::select()` and the DAC routing through `Chip::routeToScaler()` --
keep them. Neither is bypass-only: `setResetParameters()` asserts the first, and
the second states what solving a raster means. They are right independently of
this.

**A round trip is not tested by checking the two fields you changed.** The
routing came back and eleven others did not, and a narrow check reported PASS.
Diff all 1536 after the excursion.
