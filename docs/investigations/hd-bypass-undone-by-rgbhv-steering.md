# HD bypass on an RGBHV source is undone by the RGBHV steering

`/sc?K` puts the output in HD bypass. On an RGBHV source the switch runs, claims
the HD bypass DAC route, and the main loop takes it away one to three seconds
later — with a scaled preset reloaded underneath it.

**A register dump taken at the switch reads correct**, because the switch does
claim the route. Only a reader that keeps looking sees it go.

## What the loop does with it

`runSyncWatcher()` steers an RGBHV source between scaling and RGBHV bypass, and
the only thing it asks is `rto->videoStandardInput`: 14 is scaling RGBHV, 15 is
RGBHV bypass. **Those two numbers name the OUTPUT as much as the source**, and
nothing in them says which bypass — so a source switched to HD bypass reads as
one of them.

Two sites conspire, and neither is about HD bypass:

| site | what it does |
|---|---|
| the `CheckInputFrequency()` block | `if (scalingRgbhv()) rto->videoStandardInput = 15;` — parks the standard at RGBHV bypass so the arm below rebuilds the scaling setup |
| the `sourceIsRgbhv()` arm | `if (rgbhvBypass())` → an RGBHV source is stranded in bypass with `preferScalingRgbhv` on, so pull it out: `applyPresets()` |

`applyPresets()` clears `rto->outModeHdBypass`, loads a scaled preset, and the
solve that follows ends in `Chip::routeToScaler()`. It repeats for as long as
bypass is asked for.

**It also destroys the preference that asked for bypass.** The arm opens with
`if (uopt->presetPreference == 10) uopt->presetPreference = Output1080P;`, in
RAM only — so the file still reads `OutputBypass` while the running firmware has
long since replaced it. Reading the file is what makes the detection block look
innocent: `wantPassThroughMode` is computed from RAM.

`steerableRgbhv()` — `sourceIsRgbhv() && !rto->outModeHdBypass` — is what stands
the steering down, and the `CheckInputFrequency()` block is skipped whole for
the same reason: every branch of it re-decides the output mode. A source that
changes mode under HD bypass is the detection block's, which asks
`presetPreference`.

## The trace that names it

`GBS_SAMPLING_LOG=1` prints the steering's own decisions. On the bench RiscPC at
800x600@60 over VGA, `/sc?K` at t=0:

```
1.1  setOutModeHdBypass  std=14              route claimed, PLLAD_MD 2345
3.2  evt,rgbhv-leave-bypass,627,15           the arm fires
3.2  applyPresets(3) pref=5 hdByp=1          preference already overwritten
3.5  Chip::routeToScaler()                   DAC_RGBS_BYPS2DAC back to 0
3.5  presetID 8 -> 5                         a scaled preset is loaded
```

The `presetID` byte of the websocket status frame is the cheap version of that
same reading, and needs no rebuild: `8` is either bypass preset, `1`..`5` a
scaled one.

## What the two register signatures cannot tell you

`Chip::routeToScaler()` and `Chip::init()` leave the same three bits —
`DAC_RGBS_BYPS2DAC` 0, `DAC_RGBS_ADC2DAC` 0, `OUT_SYNC_SEL` 0 — so the route
alone does not say which ran. Two pads separate them without a rebuild:
`setOutModeHdBypass()`'s tail writes `PAD_TRI_ENZ` 1 and `PAD_OSC_CNTRL` 1 after
`doPostPresetLoadSteps()`, and `Chip::init()` writes both 0.

- route 0, pads 0 — a bring-up ran and no bypass tail followed it
- route 0, pads 1 — the bypass tail completed and something took the route after

## Not the engine, and not the claim's position

`doPostPresetLoadSteps()` already ends its HD bypass branch with
`geometry.enterBypass()` and returns, so by the time `setOutModeHdBypass()`
continues, `choice_` is `OutputBypass` and `Geometry::solveRaster()` refuses
before it reaches `Chip::routeToScaler()`. Moving the sketch's own
`enterBypass()` call to sit beside `Chip::routeToHdBypass()` changes nothing:
there is no path between the two that re-arms a mode change.

## Still open: the route holds, and there is no picture

With the steering stood down, `DAC_RGBS_BYPS2DAC` holds for as long as it is
asked to — measured 35 s, and the acceptance test asserts ten. The television
reports **no signal** throughout.

HD bypass writes a hardcoded `PLLAD_MD` of 2345 and `HD_HSYNC_RST` of 0x3ff,
and hands `rto->videoStandardInput` to `HdBypass::applyForStandard()` — which is
14 here, not one of the HD standards it knows. Whether HD bypass can carry an
RGBHV source at all is the next question, and it is now askable: the route stays
where it is put.
