# The bench sources, and what each one can prove

What is available to test against, which input it arrives on, and what it is the
only source that exercises. A change judged only against the RISC PC has been
judged against one input, one sync type and one scan mode.

| source | `/input?src=` | connector | path to the ADC | exercises |
|---|---|---|---|---|
| RISC PC | `vga` | DE-15, sheet `VGA_IN` | direct analog | arbitrary rasters, both sync types, progressive |
| Wii | `ypbpr` | sheet `YPBPR_IN` | direct analog | sync on green, interlace, component colour |
| Wii, composite | `av` | pin header, sheet `AVSV2YPBPR` | ADV7280 to ADV7391, re-encoded | the decoder chain, 625i |

## Direct analog against the ADV chain

This distinction decides whether a source's timings are its own.

**Only composite and S-Video pass through the ADV chain.** They are the only two
inputs the AV module routes with `adv_sw` true, and sheet `AVSV2YPBPR` decodes
them through the ADV7280 and re-encodes them through the ADV7391 -- so they reach
the scaler as regenerated, standard-conformant YPbPr, and the scaler cannot tell
them apart. Their analog switch state is identical; only a register inside the
ADV7280 selects between them.

**VGA, RGBs, RGsB and YPbPr are direct analog.** Nothing reconstructs their
timings, so whatever the source emits is what arrives. An argument that rests on
a source being standard-conformant does not reach any of them.

The AV module's switch table, from `uart_dma.c`:

| input | `asw_01..04` | `adv_sw` |
|---|---|---|
| VGA | 1, x, 1, 1 | false -- the only input raising `asw_01` |
| RGBs | 0, x, 0, 1 | false |
| RGsB | 0, x, 1, 0 | false |
| YPbPr | 0, 0, 1, 0 | false |
| S-Video | 0, 0, 1, 0 | true, `adv_input = SV_INPUT` |
| composite | 0, 0, 1, 0 | true, `adv_input = AV_INPUT` |

Note that YPbPr, RGsB, S-Video and composite share one switch state. What
separates them is `adv_sw` and the ADV7280's own input register, neither of which
appears in a TV5725 register dump.

## The RISC PC

The everyday source. `vga`, 320x256@50, VTOTAL 311, separate sync. Driven over
ModeServ, TCP 6502, so a session can change it without anyone at the bench --
`CLAUDE.md` has the commands and `RiscPc/tools/video-source/README.md` the
detail.

What it is the only source for: **arbitrary rasters**. A monitor definition can
program modes no enumeration contains, which is the whole reason the input-side
concept of a video standard does not survive here.
`docs/retiring-mode-detect.md`.

`SYNC 0|1|3` switches the machine between separate and composite sync, which
makes it the source for sync-type work. It is one CMOS value re-applied to
VIDC20's external register, not a mode-file setting.

**It cannot produce an interlaced mode through a monitor definition** -- the MDF
format has ten keys and none is interlace. `*TV vert,interlace` with 0 meaning ON
does it, or `VDU 23,0,8,&81` immediately, neither of which ModeServ exposes yet.
Adding it needs a `MODE ... I1` and a readback through `OS_Byte 144`, which
returns the old values while setting the new.

**What that would buy, and it is now the only way to get it.** No register on
this board has been shown to establish interlace: the dedicated status bits call
the RISC PC's 311-line progressive mode PAL interlace, bit-identical to the Wii's
real 576i, and `VPERIOD_IF` counts half-lines so the two read 623 against 624.
`docs/investigations/vperiod-if-on-rgbhv.md`.

Two readings survive that, and **the two existing sources cannot separate them**,
because the Wii and the RISC PC differ in interlace *and* in input and sync path
at once:

1. the chip cannot resolve interlace at 15 kHz and 50 Hz at all, or
2. it can, and a 311-line progressive mode is genuinely ambiguous against 576i.

An interlaced RISC PC mode is the discriminator -- same machine, same cable, same
input, same sync arrangement, **only interlace changes**. If the status bits move,
reading 2 is right and interlace is detectable with care. If they do not, reading
1 is right and nothing downstream may key on interlace.

It is **not** needed to separate `IF_PRGRSV_CNTRL`'s two meanings, which the two
existing sources already do between them -- see below. That was the earlier case
for it and it is weaker than this one.

## The Wii

On the YPbPr input, and the only source here for three things:

- **Sync on green.** Component carries sync on Y, so this is the real test of
  `SyncType` and the SOG slicer against a source that genuinely has it, rather
  than against the RISC PC's composite-sync setting.
- **Interlace.** Interlaced SD is what `SourceStandard::isSd()` names, and the
  arm that asks for the higher oversample and the 40 MHz analog corner. Nothing
  on the RISC PC reaches it, because a monitor definition cannot ask for an
  interlaced mode.

  **Whether it is 480i or 576i does not need to be known in advance, and that is
  the point.** It is a line count and a field rate, and `VPERIOD_IF` answers it
  on connection -- `STATUS_SYNC_PROC_VTOTAL` does not, because it counts fields. The old code
  calls those two standards 1 and 2 and branches on which, which is exactly the
  branch this retirement deletes. A console whose region and video setting decide
  the answer is a good demonstration of why the byte cannot be trusted to carry
  it.

  If the console is set to 60 Hz on component it can also output 480p, which
  would add a **progressive component** case -- the one combination neither
  other source provides.
- **Component colour**, so the `inputIsYpBpR` branches -- the luma and chroma
  realignment delays -- have a source that needs them.

It is a direct analog path, so its timings are its own.

### Measured, on the component cable

```
STATUS_SYNC_PROC_VTOTAL   310      SP_SOG_MODE          1
VPERIOD_IF                624      SP_H_PULSE_IGNOR    97
HPERIOD_IF                431      PLLAD_KS             2
line rate              15625 Hz    ADC_FLTR             3
field rate            50.00 Hz     IF_PRGRSV_CNTRL      0
```

**576i.** `VPERIOD_IF` counts the frame at 624 and `STATUS_SYNC_PROC_VTOTAL`
counts 310, which is the field -- so **the sync processor counts FIELDS on an
interlaced source**, and a 625-line source reads as about 310.

`SP_SOG_MODE` 1 with `SP_H_PULSE_IGNOR` 97 is the csync configuration, chosen by
the probe without help: component carries sync on Y, and the probe finds it.

**A console's region and video setting decide 480i against 576i**, and this one
is PAL. Read `VPERIOD_IF` rather than assuming.

## Composite, and why it is last

Composite video comes from the **Wii**, on an RCA cable. **It replaces the
component cable rather than joining it**, so reaching the `av` input costs the
YPbPr source -- which is the only source for sync on green and interlace. Do
composite last, after everything the component cable is needed for.

The RISC PC has no composite video output here. ModeServ's `SYNC 1` selects
composite **sync** on the VGA connector -- one CMOS value re-applied to VIDC20's
external register -- which is a different thing and stays on the `vga` input.

Composite is the one path where a source arrives standard-conformant regardless
of what the machine emits, because the ADV chain regenerates it.

## What nothing here covers

- **RGBs and RGsB** have no source attached, so the SCART input and the
  sync-on-green RGB path are untested. Sheet `RGBS_IN` carries both
  `CON_SCART_F` and a CVBS net; where that net routes is not traced, so do not
  assume the SCART socket reaches the `av` input.
- **S-Video** shares everything with composite except one ADV7280 register, so
  testing composite tests all of it but that register.
- **Composite and component at the same time.** One Wii, one cable, so the
  interlaced-component and the decoder-chain cases cannot both be live.
- **HD component**, the standards 5, 6 and 7 branch, has no source.

## `IF_PRGRSV_CNTRL` is separable with what is already here

`InputFormatter::applyScanMode()` writes it from the line-doubling flag, while
RD-5725-1.1 defines the bit as whether the SOURCE is interlaced. The two sources
between them show those are different facts, on one build:

| source | genuinely interlaced | line doubler | `IF_PRGRSV_CNTRL` |
|---|---|---|---|
| RISC PC 320x256@50 | no | on | 0 |
| Wii 576i | yes | on | 0 |

The bit reads 0 for both, and it is right about one of them. Nothing needs an
interlaced RISC PC mode to establish that.

## Why this matters to a register argument

Two subsystems that never see the same input cannot be shown to agree by testing
one of them. `docs/retiring-mode-detect.md` deletes branches keyed to a video
standard, and which branch a change is judged against is decided entirely by
which source is plugged in.
