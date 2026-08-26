# `GBS_OPTION_SCALING_RGBHV` and `scalingRgbhv()` are not the same fact

Both answer "is this source being scaled as RGBHV", and on a settled unit they
agree exactly. Measured across a full bypass round trip on the bench RiscPC:

| state | `GBS_OPTION_SCALING_RGBHV` | `DAC_RGBS_ADC2DAC` | `SP_VTOTAL` |
|---|---|---|---|
| scaled | 1 | 0 | 311 |
| RGBHV bypass, 800x600@60 | 0 | 1 | 627 |
| back to scaled | 1 | 0 | 311 |

**Substituting one for the other is nevertheless a behaviour change**, and the
window where they disagree is the one that matters.

`loadComputedPreset()` sets the standard twice around the register write:

```cpp
rto->videoStandardInput = load.videoStandardInput();        // the sentinel, normalised
...
GBS::GBS_OPTION_SCALING_RGBHV::write(1);
rto->videoStandardInput = load.videoStandardInputAfterLoad();   // ScalingRgbhvStandard = 3
```

`scalingRgbhv()` is `rto->videoStandardInput == 14`, and nothing sets 14 until
`runSyncWatcher()` does, later. So from the load until then the register reads 1
and `scalingRgbhv()` is **false**.

Four of the register's six readers sit inside `doPostPresetLoadSteps()` — in
exactly that window. Replacing them with `scalingRgbhv()` inverts those branches
on every load, and the settled state afterwards looks correct either way, which
is what makes it expensive rather than merely wrong.

## What has to happen first

The two representations can only be merged once the standard on the input side
stops carrying more than one fact. `ScalingRgbhvStandard` being 3 while
`scalingRgbhv()` tests 14 is the same one-number-two-meanings shape as
`videoStandardInput == 14/15`, and retiring the input-side standard is what
resolves it.

Until then, leave the register. It is reserved space and costs nothing but a
name.
