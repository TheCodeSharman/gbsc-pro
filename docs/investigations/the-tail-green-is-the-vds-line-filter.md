# The tail green is the VDS line filter, and it is a user option

The green band at the end of the line is produced by `VDS_D_RAM_BYPS` (s3_26
bit 6), which RD-5725-1.1 names *"line buffer one line delay data bypass"*. With
the delay in circuit the tail of every line past a fixed position is destroyed;
with it bypassed the whole line arrives.

It is not an unconfigured block. It is the picture-quality option
`uopt->wantVdsLineFilter` -- the OLED's Menu->Color->Line filter, `/uc?m`,
`VideoProcessor::setLineFilter()` -- and it was defaulted **on**.

Measured on the bench RiscPC at 320x256@50, line doubled, `PATTERN PM5544`. At
each divider the capture window is `129 .. divider/2 - 2` and `VDS_HSCALE` is
refitted to `(stop - 129) x 1024 / 1789`, so the produced picture fills the
display window and the artefact cannot be confused with the playback overhang.
The band is read as the first sustained run of columns where `G - max(R, B)`
clears 25.

| divider | 2600 | 2800 | 3000 | 3200 |
|---|---|---|---|---|
| filter in circuit -- band edge, photo columns | 1048 | 960 | 880 | 814 |
| filter bypassed | none | none | none | none |

Peak greenness is 81..85 with the delay in and 2 or less with it out. Toggled
back and forth twice at one divider: edge 1048 / peak 89 both times in, no edge
both times out.

## What the delay achieves here: nothing measurable

Every stage in the VDS that needs a previous line is already bypassed --
`VDS_PK_Y_V_BYPS`, `VDS_C_VPK_BYPS`, `VDS_BLEV_BYPS`, `VDS_W_LEV_BYPS`,
`VDS_NS_BYPS`, `VDS_SK_BYPS` and the SVM pair are all 1. The data passes
through a one-line delay nothing reads.

Photographed A/B/A/B at one framing, vertical detail in a fixed crop of the
card's grille:

| | filter off | filter off | filter on | filter on |
|---|---|---|---|---|
| mean vertical difference | 3.84 | 3.85 | 3.85 | 3.83 |
| mean horizontal difference | 20.37 | 20.61 | 20.64 | 20.65 |

The repeat-to-repeat spread is the same size as the toggle's, so the difference
is below the noise floor of the measurement.

**Whether it earns its place with scanlines is untested.** The sketch's
scanlines handler recommends it, and `Deinterlacer::enableScanlines()` clears
`VDS_W_LEV_BYPS`, which is a plausible consumer. Enabling scanlines on this
progressive source does not reach that code -- `VDS_W_LEV_BYPS` stays 1 and
`RFF_LINE_FLIP` stays 0 -- so the pairing cannot be judged from this bench
without an interlaced source.

## What the boundary is not

Each of these was written on a settled unit with the band in view, one field at
a time, restored before the next, and left the edge at 1048:

`MADPT_UVDLY_PD_BYPS`, `MADPT_UV_MI_DET_BYPS`, `MADPT_BIT_STILL_EN`,
`MAPDT_VT_SEL_PRGV`, `VDS_TAP6_BYPS`, `VDS_PK_Y_H_BYPS`, `IF_TAP6_BYPS`,
`IF_HS_TAP11_BYPS`, `IF_HS_INT_LPF_BYPS`, `CAP_SAFE_GUARD_EN`,
`WFF_SAFE_GUARD`, `CAP_VRST_FFRST_EN`.

`PB_FETCH_NUM` at 180, 210, 239, 270 and 300 gives edges of 1050, 1046, 1048,
1018 and 1018, which re-confirms `tail-green.md`'s refutation with the display
window filled.

`VDS_BLK_BF_EN` 0 reads as a second hit and is not one: it turns the whole
picture magenta, so a greenness profile stops seeing a band that is still there.
A metric keyed on one colour cannot survive a global colour change.

## The write reset position does not carry the boundary

`VDS_D_SP` is the line buffer's write reset, *"also the write start position"*.
Walked to 100, 300, 600 and 900 the band's onset does not move -- 1048, 1046,
1046, 1046. At 0 the **whole screen** goes green from column 4, which is what
establishes that the delay carries the entire line rather than a filter tap.

So the depth that bounds the line is not measured from the write reset, and this
page does not supply what it is measured from.

## What is still bounded

At divider 3200 with the delay bypassed the tail of the line repeats
horizontally. The playback fetch is held at the value the engine solved for a
shorter line, so that reading is about `PB_FETCH_NUM` and not about a capture
bound. **How far the divider goes once the engine solves the whole framing is
not measured**, and it cannot be from outside the firmware: `framableIfLine()`
caps the divider below the band's onset, so the engine never reaches it.

`docs/capture-limits.md` and
[`the-tail-band-is-not-a-capture-width.md`](the-tail-band-is-not-a-capture-width.md)
carry what that bound was believed to be.
