# The ADC phase separates from the global shimmer

1280x720@60 on `vga` in pass-through, `PLLAD_MD` 2038 latched against
`STATUS_SYNC_PROC_HTOTAL` 2038, count 749, PM5544 with `ANIM OFF`, console
certified quiet for both captures. One acquisition, `PA_SP_S` held at 0
throughout, only `PA_ADC_S` moved. Registers for this state:
`tools/gbsc-pro-hwtest/snapshots/clean-720p-passthrough-sp0-adc12-2026-10-24.json`.

**THE ARTEFACT IS TEMPORAL AND A STILL CANNOT SHOW IT.** Each `*-shimmer.png`
is the per-pixel standard deviation over 32 captured frames, scaled so white is
the 99.9th percentile, which is what makes a shimmer visible as a picture.

| file | what it shows |
|---|---|
| `clean-sp0-adc12-frame.png` | the emitted frame at the pair judged clean at the panel |
| `clean-sp0-adc12-shimmer.png` | its temporal map: the wedge lit at its dense (left) end, fading right |
| `bad-sp0-adc28-frame.png` | the same acquisition at `PA_ADC_S` 28 |
| `bad-sp0-adc28-shimmer.png` | its temporal map: a broad beat across the whole wedge, and the disc interior textured |
| `clean-sp0-adc12-wedge4x.png` | the wedge band at 4x, where the finest block's chroma fringing is visible and its luma structure is not lost |

## The two measures disagree in scope, and that is the finding

| `PA_ADC_S` | wedge contrast | movement | ratio | whole-picture sd, mean | p99 |
|---|---|---|---|---|---|
| 12 | **52.3** | 7.626 | **0.146** | 1.176 | 8.507 |
| 28 | **22.7** | 9.425 | **0.416** | **1.189** | 8.839 |

**The phase moves the wedge 2.3x and does not move the whole-picture shimmer at
all.** The faint vertical banding covering the grey field is the same in both,
so it is not the sampling phase and no phase setting addresses it.

**`wedge.score`'s movement is nearly blind to that banding**, because it is
displacement multiplied by local contrast and the grey field has almost none --
so a shimmer over most of the picture contributes little while the wedge
dominates the number. The per-pixel map has no such bias and is the measure
that matches what a viewer reports across the whole image.

52.3 is the achievable peak rather than a good point on a poor acquisition:
`investigations/the-adc-sampling-phase-is-a-lottery.md` records it as
52.6 with sd 0.42.
