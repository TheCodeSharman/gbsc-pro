# "Scaling RGBHV" is spelled two ways, and they disagree for the length of a load

`Tv5725::PresetLoad::scalingRgbhvInForce()` and `scalingRgbhv()` answer the same
question. On a settled unit they agree. Between a preset load and the middle of
`doPostPresetLoadSteps()` they do not, and four branches read the byte inside
that window.

## The two spellings

| | set by | true from |
|---|---|---|
| `PresetLoad::scalingRgbhvInForce()` | `rememberScalingRgbhv()` | the load |
| `scalingRgbhv()`, `videoStandardInput == 14` | `doPostPresetLoadSteps()`, and `loadScalingRgbhvPreset()` | part-way through the load |

`loadComputedPreset()` writes `videoStandardInputAfterLoad()`, which is
`PresetLoad::ScalingRgbhvStandard` -- **3**. `scalingRgbhv()` tests **14**. So the
byte holds 3 from the load until `doPostPresetLoadSteps()` reaches

```cpp
if (Tv5725::PresetLoad::scalingRgbhvInForce())
    rto->videoStandardInput = 14;
```

and repairs it.

**One number carrying two facts is the whole of it**: 3 is also the byte for
480p NTSC, so inside the window a scaling RGBHV source is indistinguishable from
a progressive SD one.

## What acts on the byte inside the window

`doPostPresetLoadSteps()` reads it four times before the repair. Three are
guarded or inert; the fourth is not.

```cpp
rto->osr = Tv5725::SourceStandard(rto->videoStandardInput, rto->inputIsYpBpR)
               .apply(GBS::PLLAD_KS::read());
```

`SourceStandard::isProgressive()` covers 3, 4, 8 and 9, so **3 takes the
progressive arm** and 14 takes none of them. The difference between the two:

| | byte 3, `applyProgressive()` | byte 14, no arm |
|---|---|---|
| `ADC_FLTR` | 40 MHz corner | untouched |
| `PLLAD_KS` | written 1, then oversampled against it | **read back** and oversampled against whatever it holds |
| SD vsync start/stop | 14/11, then 16/13 for standard 3 | untouched |
| the IF and VDS delays | seven registers written | untouched |
| `rto->osr` | `applyOversample(1, 2)` | `applyOversample(PLLAD_KS::read(), 2)` |

Most of those writes are overwritten later -- measured on RGBHV, `IF_SEL_WEN`
reads 0 where the arm writes 1, with no other writer -- so the arm is largely
dead on this path. **`rto->osr` is not**: the engine takes it through
`inputTimingsChanged(osr)`, and it is the one output of the call that survives.

## What would settle it

Make `videoStandardInputAfterLoad()` return 14 and delete the repair. The window
closes, `scalingRgbhv()` agrees with `scalingRgbhvInForce()` everywhere, and one
of the byte's meanings is gone.

The measurement that says whether it is safe is `rto->osr` and the sync
processor's SD vsync pair, before and after, on the bench RiscPC -- which is a
scaling RGBHV source, so it takes this path on every load. Equal `osr` means the
arm was already contributing nothing that outlived it.

**`PLLAD_KS::read()` as the argument is the reason the answer is not derivable
from the code.** What the divider holds at that instant is set by whichever path
reached the load, so the two arms can agree on one route and differ on another.

## The register spelling is gone

`GBS_OPTION_SCALING_RGBHV` was a third spelling, in s1_2c, an address
RD-5725-1.1 does not document. It has no readers and no declaration left:
`PresetLoad` holds the state instead.
