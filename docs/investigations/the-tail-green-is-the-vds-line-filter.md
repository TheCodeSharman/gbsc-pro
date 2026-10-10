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

## The buffer delivers 1020 samples of the captured line

Past captured sample 1020 the tail comes back green. The bound is on the
CAPTURED width -- the data the VDS reads back per line -- and on nothing else.

Measured on `vga` at 800x600@60, undoubled, raster 1592x1125, read off the USB
HDMI capture at 1920x1080 as the run of columns at the picture's right edge
where `G - max(R, B)` stays above 4.

| capture, samples | 1024 | 1022 | 1020 | 1018 | 1016 | 1008 |
|---|---|---|---|---|---|---|
| fringe, photo columns | 6 | 2 | 0 | 0 | 0 | 0 |
| greenness of the last ten columns | +25.8 | +2.5 | +0.0 | +0.1 | +0.2 | +0.2 |

**A BAND IS OBVIOUS AND THE LAST FEW SAMPLES ARE NOT**, which is how the
boundary reads four samples late: a detector wide enough to ignore the test
card's own green needs a sustained run, and a six-column fringe never supplies
one. The first pass over this put the boundary at 1024 for that reason. Ask the
columns at the edge, not for a run.

## It is not output pixels, a line position, or the oversampling

Each of these moves something the boundary might have been expressed in, and
leaves it where it was.

| moved | from | to | onset, captured sample |
|---|---|---|---|
| `VDS_HSCALE` | 803 | 1023 | 1022.8 both |
| the display window | 1387 px | 1042 px | 1022.8 both |
| the capture window's head | 293 | 172 | 1022.8 and 1022.3, at line positions 1316 and 1194 |
| the oversampling | 2x | 1x | 1019.7 both |

The scale and the display window settle that it is not output pixels. The
window's head settles that it is counted from the first captured sample rather
than from hsync. The oversampling settles that it counts KEPT samples: the
whole group moved -- `PLLAD_CKOS` 0 to 1, `ADC_CLK_ICLK1X` 1 to 0, `DEC2_BYPS`
0 to 1 -- with `PLLAD_MD` held at 1348, and the fringe stayed six columns wide.
Nothing upstream of the decimator reaches it.

## There is no width register

RD-5725-1.1 gives this buffer one register and it is a position, not a length.
`VDS_D_SP` at 3, 100, 500 and 1023 leaves the onset at 1022.8; at 0 the whole
line goes green from column 4, which is what says the delay carries the whole
line rather than a filter tap. Every other line buffer in the part has a read
reset beside its write reset -- `MADPT_PD_SP`/`_ST`,
`MADPT_NRD_VIIR_PD_SP`/`_ST`, `MADPT_UVDLY_PD_SP`/`_ST` -- and this one has
none.

`VDS_HALF_EN` is the only candidate the datasheet leaves open, its Function cell
being empty with the neighbouring text duplicated from `VDS_HSCALE_BYPS`. Set to
1 it changes the onset, the lit extent and the mean luma by nothing at all.

## What it costs, and which sources reach it

The captured width is the source's active area in IF units, so the divider
decides whether the line fits:

| | `PLLAD_MD` | doubled | capture window | samples | band |
|---|---|---|---|---|---|
| 800x600@60, unbounded | 1436 | no | 293..1384 | 1091 | 71 samples, 6.5% of the width |
| 800x600@60, bounded | 1342 | no | 275..1294 | 1019 | none |
| 320x256@50 | 2200 | yes | 236..926 | 690 | none |

`VideoPath::dividerCeilingForLineFilter()` is the bound: with the delay wanted,
the divider comes down until the captured line fits, which costs 6% of the
sampling density here and leaves 1.27 ADC samples per source pixel. It declines
where the published raster states more active pixels than the delay delivers --
1024x768 and up -- because capping there would store fewer samples than the
source has pixels, and `applyLineFilter()` leaves the delay out instead.

Nothing bounds the undoubled divider by this buffer.
`VideoPath::dividerCeilingForOutput()` caps it by what the raster can show,
which is 1436 here, and the capture reaches 1091 unopposed.

**`SamplingClock::DoubledLineSampleLimit` is this buffer.** Its onset of
2236..2256 ADC samples is 1118..1128 IF units on a doubled line, which is 1024
captured samples once the window's head is off. Two things follow. Its stated
premise -- that the bound is on the line rather than the window -- is refuted by
the pan above, so a doubled source whose active fraction is wider than the
bench's bands at any divider the cap allows. And it is only needed while the
delay is in circuit, which is not the default, so on a doubled source it is
otherwise holding the kept count at 1100 against the counter's wall of 2006.

## What is still bounded

At divider 3200 with the delay bypassed the tail of the line repeats
horizontally. The playback fetch is held at the value the engine solved for a
shorter line, so that reading is about `PB_FETCH_NUM` and not about a capture
bound.

`docs/capture-limits.md` and
[`the-tail-band-is-not-a-capture-width.md`](the-tail-band-is-not-a-capture-width.md)
carry what that bound was believed to be.
