# 640x512@50 composite emits a picture

With the line filter off the mode acquires on `vga` composite and fills the
screen. Measured off the USB HDMI capture at 1920x1080, three frames, the card
legible and the source's green border present:

```
mean luma 77.81   black margins  left 2  right 1  top 0  bottom 11
picture 1917x1069
```

The engine solves 534 lines x 50.16 Hz, line rate 26786, divider **1746**,
oversample 2, and `STATUS_SYNC_PROC_VTOTAL` reads 530 in 57 to 60 of every 57
to 60 samples taken at 25 ms. So neither the mode nor its composite sync is
what stands between this source and a picture.

## With the line filter on the divider is capped, and there the count fails

`VideoPath::dividerCeilingForLineFilter()` sizes the divider so the source's
active pixels fit `VideoProcessor::LineBufferSamples`, and for this mode that
is **1424**. The engine installs 1746 from the measured rate, takes the duty,
and re-installs 1424 against the ceiling -- and from the next pass the count is
unusable.

Walked with `/sampleclock` in that state, automation frozen, 1.2 s of samples
per step:

| `PLLAD_MD` | `STATUS_SYNC_PROC_VTOTAL` |
|---|---|
| 1400, 1420, 1425 | garbage |
| 1430, 1435, 1440, 1445, 1450 | **530 in every sample** |
| 1475 .. 1800 | 530 in every sample |

The PLL row changes at 1500, not at the boundary -- `PLLAD_KS` 2 -> 1 and the
decimators with it -- so the row is not what decides it.

## The same divider counts perfectly once the source has settled

After one acquisition that holds at 1746, `/sampleclock?md=1424` reads 530 in
53 to 59 of 53 to 59 samples, repeated three times alternating with 1430. Put
the line filter back on and the engine solves 1424 by itself and holds it
**acquired for 87 s with the count steady**. So 1424 is not a divider this
source cannot be counted at; it is one it cannot be counted at in the state a
mode change leaves behind.

## What the state is NOT

Each of these was A/B'd inside one frozen acquisition, the count scored on
`Tv5725::SamplingLog` at 25 ms:

- **The sync pad.** `PAD_SYNC_OUT_ENZ` driven, away, driven: 530 throughout.
- **The coast window.** `SP_H_CST_SP` walked 100, 256, 512, 974, 1008 -- every
  value garbage in the bad state, including the 256 the good state holds.
- **The oversampling.** `os=2` and `os=4` give the same verdict at each
  divider.
- **The sampling phase.** All 32 values of `PA_SP_S` applied through the
  adjuster's own latch and bypass sequence: 28 to 36 samples each, every one
  530.
- **The pattern animating.** `ANIM ON`/`ANIM OFF` five times: 530 throughout.
- **The register state.** A 496-register configuration dump either side differs
  in ten fields -- `IF_HB_SP2`, `IF_HB_ST2`, `IF_LINE_SP`, `PA_ADC_S`,
  `PA_SP_S`, `PB_CAP_OFFSET`, `PB_FETCH_NUM`, `VDS_D_RAM_BYPS`, `VDS_HB_SP`,
  `VDS_HSCALE` -- and writing all ten to the bad state's values, singly and
  then together, leaves the count at 530 in 35 of 35.

## The clue that is left

Every bad reading is a multiple of about **75.9 lines**, which is one seventh
of the frame: 152, 228, 304, 380, 531, 683, 759, 835, 911, 1062, 1138, 1214,
1290, 1443, 1669, 1745, 1821, 1897, 1973, with the correct 531 as the seventh
multiple. `SP_PRE_COAST` is 7 and is the obvious suspect; walked 1, 2, 3, 4, 5,
7, 9, 12 in a settled state it changes nothing, and the walk has not been taken
in the state that produces the quantum.
