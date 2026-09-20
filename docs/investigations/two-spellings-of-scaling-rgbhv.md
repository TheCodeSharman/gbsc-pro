# "Scaling RGBHV" is spelled two ways, and they disagree for the length of a load

`Tv5725::PresetLoad::scalingRgbhvInForce()` and `scalingRgbhv()` answer the same
question. On a settled unit they agree. Between a preset load and the middle of
`doPostPresetLoadSteps()` they do not, and four branches read the byte inside
that window.

## The two spellings

| | set by | true from |
|---|---|---|
| `PresetLoad::scalingRgbhvInForce()` | `rememberScalingRgbhv()` | the load |
| `scalingRgbhv()`, the byte plus `Tv5725::RgbhvOutput` | `loadComputedPreset()`, and `loadScalingRgbhvPreset()` | the load |

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

## The window is closed

A load leaves the byte alone. It holds `PresetLoad::Rgbhv` for the whole of a
scaling RGBHV load -- the source is RGBHV before, during and after it -- and
what the load establishes is `Tv5725::RgbhvOutput`, so there is no interval in
which the byte names a standard the source is not. The mid-load repair is gone
with the value it repaired, and 3 no longer means two things.

Measured on the bench RiscPC at 320x256@50 on `vga`, a scaling RGBHV source that
takes this path on every load: all 608 config and 48 status registers
byte-identical across the change, and the photographed test card unchanged.

**`osr` comes out 2 on both arms whenever `PLLAD_KS` reads 1 or more**, which is
why the arm was contributing nothing that outlived it. `applyProgressive()` asks
`applyOversample(1, 2)` and the no-arm route asks `applyOversample(KS, 2)`; both
give 2, and they part only where `KS` reads 0 -- after an HD standard or a tall
source, neither of which this bench can produce.

**Two branches that could not run while the byte held 3 are now live.** Neither
moves a register on this source: the `scalingRgbhv()` arm at the sync processor
branches only on composite sync, and the `>= 5` sub-coast write lands on a value
already in force. RGBHV with composite sync does not acquire on either build --
`m:0`, `u:96`, `own V sync: no` -- so that arm has no behaviour to compare.
[scaling-rgbhv-standard-latches-the-no-sync-branch.md](scaling-rgbhv-standard-latches-the-no-sync-branch.md)

## The register spelling is gone

`GBS_OPTION_SCALING_RGBHV` was a third spelling, in s1_2c, an address
RD-5725-1.1 does not document. It has no readers and no declaration left:
`PresetLoad` holds the state instead.
