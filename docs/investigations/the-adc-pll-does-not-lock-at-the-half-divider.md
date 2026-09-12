# The ADC PLL does not lock at the half divider, and the whole picture beats

On a 31 kHz source the engine settles on `PLLAD_MD` 1096 and
`STATUS_MISC_PLLAD_LOCK` reads **0 for as long as anyone watches**. The sampling
clock is then not phase-locked to hsync, its sample positions drift across the
line, and the beating that produces covers the entire image rather than the
fine detail.

## Measured

`/samplinglog?ms=25&for=15000`, which samples inside `loop()`. HTTP point reads
cannot settle this: a single `STATUS_MISC_PLLAD_LOCK` read during settling says
0 on a unit that is locked, so only a dense run counts.

| | RiscPC 320x256@50 | RiscPC 720x576@50 | Wii 480p, `ypbpr` |
|---|---|---|---|
| `PLLAD_MD` | 2208 | 1096 | 1096 |
| `pllad_lock` | **1 in 499 of 552** | **0 in 667 of 667** | **0 in 564 of 564** |
| `sp_htotal` | 2208, no dither | 1095 / 1096 / 1097 | 1095 / 1096 / 1097 |
| `hperiod_if` | 431, the value due | 215, the value due | 214, the value due |
| picture | clean | beats, whole image | beats, whole image |

**The dither IS the drift.** `STATUS_SYNC_PROC_HTOTAL` counts real ADC clocks
per line, so it reports the latched divider; at 2208 it is the divider in every
one of 552 samples, and at 1096 it moves by one in 78 of 667.

**Two different sources, two different connectors, one divider, the same
reading.** The RiscPC arrives on `vga` as separate-sync RGBHV and the Wii on
`ypbpr` as sync-on-green component, so nothing about the input path is shared.
What they share is 1096.

## What the group says

Every other member of the ADC PLL group is **identical** between the locked and
the unlocked state:

    PLLAD_KS 2   PLLAD_ICP 5   PLLAD_FS 1   PLLAD_CKOS 0
    ADC_CLK_ICLK1X 1   ADC_CLK_ICLK2X 1   DEC1_BYPS 0   DEC2_BYPS 0

So nothing adapts the loop filter or the VCO range to the divider. The VCO
frequency is not what differs either -- it is line rate x divider, which is
34.50 MHz at 320x256, 34.25 MHz at 720x576 and 34.50 MHz on the Wii, the same
frequency in all three. **What differs is the phase-detector reference**: 15.6
kHz against 31.3 kHz into an unchanged loop filter.

That makes the divider a proxy rather than the cause. The engine halves the
divider when the line rate doubles, holding the VCO still, and the loop is then
run at twice the reference frequency with the same `PLLAD_ICP`.

## What was refuted on the way

**"The gratings beat, so it is the sampling ratio."** 1096 samples across a line
of 864 source pixels is 1.268 samples per pixel, and a PM5544 grating is built
to expose exactly that -- but a ratio artefact can only appear where there is
detail fine enough to alias. The beating covers flat areas too, which a ratio
cannot do and a moving clock can.

**"`ifbits` 9 flags the fault."** `STATUS_IF_VT_BAD` is set at 720x576, and it is
equally set on the clean 320x256 control, in all 552 samples. It is the normal
reading for this bench and says nothing about the fault. The Wii reads 3 instead,
which tracks the connector rather than the picture.

**"The Wii is clean at 1096, so the lock bit is only tracking the divider."**
The Wii's menu text is legible at 1096, which is what earlier notes recorded,
and it beats as badly as anything else once there is a picture with area in it.
Legible is not clean.

## Open

Why `PLLAD_ICP` 5 suits a 15.6 kHz reference and not a 31.3 kHz one is the
question to put to the bench, and `/sampleclock` is the instrument -- it writes
the whole group together and restarts the PLL afterwards, which is required.
Walking `PLLAD_ICP` at a fixed 1096 divider, with `pllad_lock` read from the
sampling log rather than over HTTP, is the measurement. Do not bisect the group
by hand over `/setreg`: the members load together on `PLLAD_LAT` and writing two
or three of them leaves the PLL unlocked for a reason that has nothing to do
with this.

`docs/investigations/the-vco-gain-follows-the-vco.md` is the neighbouring work.
