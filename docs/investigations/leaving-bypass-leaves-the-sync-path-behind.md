# Leaving bypass leaves the sync path behind

A bypass round trip -- `/uc?x` out of `preferScalingRgbhv` and back -- returns a
scaled picture displaced about 250 px to the right on the panel, with a black
band down the left and the right-hand edge off the screen. The framing registers
are unchanged across it: `/geometry` reads the same origin and extent, and
`VDS_HSYNC_RST`, `VDS_DIS_HB_ST`, `VDS_DIS_HB_SP`, `VDS_HSCALE`, `PLLAD_MD`,
`IF_HB_ST2` and `IF_HB_SP2` all read what they read before.

## It is not "every register identical"

A config dump leaves 928 addresses out of the comparison, which is why the
displacement has read as an encoder behaviour nothing on this board can test.
Taken as a FULL snapshot either side -- `snapdiff.py --save`, 1536 registers --
**23 bytes differ, resolving to 21 fields**, displaced against recovered:

```
HD_DYN_BYPS         1 -> 0      HD_HS_SP         40 -> 0     IF_HB_SP2      207 -> 208
HD_EXT_HB_SP        6 -> 0      HD_HS_ST        164 -> 0     IF_HB_ST2     1701 -> 1702
HD_EXT_VB_SP        6 -> 0      HD_MATRIX_BYPS    1 -> 0     PA_ADC_BYPSZ     1 -> 0
HD_HB_SP          144 -> 0      HD_U_GAIN       128 -> 0     PA_SP_BYPSZ      1 -> 0
HD_HB_ST         2039 -> 0      HD_VB_SP         35 -> 0     SP_HS2PLL_INV_REG 1 -> 0
HD_HSYNC_RST     2047 -> 0      HD_VS_SP          2 -> 0     SP_H_CST_SP    822 -> 256
                                HD_VS_ST          7 -> 0
                                HD_V_GAIN       128 -> 0
                                HD_Y_GAIN       128 -> 0
```

Two of those are in the path the scaled picture actually runs through, and each
is a mechanism for one of the two symptoms:

- **`SP_HS2PLL_INV_REG` 1** feeds the ADC PLL an inverted hsync, which moves
  where the sync processor starts counting the line -- a horizontal
  displacement.
- **`SP_H_CST_SP` 822** puts the clamp stop far into active video, where the
  black level is taken off picture content rather than off the back porch --
  a colour shift that follows what is on screen.

The rest is the `HD_*` pass-through channel left loaded with the raster bypass
was driving, and the two phase-adjuster bypasses.

## `SP_HS2PLL_INV_REG` carries 94 px of it, measured

The single-field test has been run. RiscPC at 640x480@60 on `vga`, forced 100%
framing, six round trips out of and back into `preferScalingRgbhv`: every one
displaced the picture, and the only watched register that changed was
`SP_HS2PLL_INV_REG` 0 -> 1.

Writing it back to 0 **alone**, with nothing else touched:

| | content, photo columns | against the good reference |
|---|---|---|
| after the round trip | 290..1557 | -200.3 px |
| `SP_HS2PLL_INV_REG` 0 | 92..1549 | -106.0 px |

So it is 94 px of the 200, and the residual 106 px is not a bypass artefact at
all -- 92..1549 is the ordinary `+41` landing of
[the-picture-position-is-latched-not-re-rolled.md](the-picture-position-is-latched-not-re-rolled.md).
A bypass round trip is therefore that jump plus a register-visible 94 px, which
makes it a **6-of-6 provoker** for a jump a source mode change only fires 2 in 8.

`SP_H_CST_SP` at 256 is separately shown NOT to black the left-hand band: moved
to 100 with automation frozen, the picture is unchanged to within a pixel.

## Why the scaling path clears it rather than following the polarity

`SyncProcessor::normaliseHsyncPolarity()` writes `SP_HS_INV_REG` from the
measured polarity so that everything downstream sees one shape -- the polarity
stops being an input to the solve. A second inversion into the ADC PLL on top of
that is displacement and nothing else, so the same call clears
`SP_HS2PLL_INV_REG`.

The bypass channel is the opposite case and keeps setting it from the source:
`HdBypass::applyChannelSyncEdges()` deliberately plays the source's pulses out
the way round the source sends them, so the PLL needs the matching inversion.

Six round trips after the change: `SP_HS2PLL_INV_REG` 0 in all six, and the
displacement is only the landing set -- five at the left landing, one at the
reference.

## The recoveries are not interchangeable

Measured in this order, each against the fault standing:

| tried | result |
|---|---|
| source mode round trip (320x256 and back) | no change |
| `/sc?U`, re-deriving every register from held state | no change |
| `/restart` | clears it; the autosaved framing survives |

So it is not the framing, and it is not anything `resolveFromSource()` rewrites.
What `/restart` does that those do not is run the bring-up, which is where every
field above is written to its boot value.

## Why this is a fault and not an encoder behaviour

`docs/investigations/encoder-stale-timing.md` is a different thing: the sink
drops signal and a `PAD_SYNC_OUT_ENZ` toggle brings it back. Here the sink is
locked and painting throughout, and the state that survives is on the TV5725,
in registers, on the chip's own bus.
