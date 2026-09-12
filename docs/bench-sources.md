# The bench sources, and what each one can prove

What is available to test against, which input it arrives on, and what it is the
only source that exercises. A change judged only against the RISC PC has been
judged against one input, one sync type and one scan mode.

| source | `/input?src=` | connector | path to the ADC | exercises |
|---|---|---|---|---|
| RISC PC | `vga` | DE-15, sheet `VGA_IN` | direct analog | arbitrary rasters, both sync types, progressive |
| Wii | `ypbpr` | sheet `YPBPR_IN` | direct analog | sync on green, interlace, component colour |
| Wii, composite | `av` | pin header, sheet `AVSV2YPBPR` | ADV7280 to ADV7391, re-encoded | the decoder chain, 625i |

## Switching between them

Both are connected at once. `/input?src=vga` and `/input?src=ypbpr` move the
analog routing and the ADC input together, so a change can be judged against
both sync types from a session with nobody at the bench.

## Acquisition is seconds, and a wait of minutes is a fault

Measured on the RISC PC, timed from the input switch:

```
 0.0  /input?src=vga queued
 1.6  own V sync: yes after 137ms  ->  separate H/V
 6.4  own V sync: yes after 2ms
 7.2  sampling: 311 lines x 50.08 Hz -> line rate 15625
 8.1  present: true, state: acquired
10.4  running frame sync, clock gen enabled = 1
```

Steady at 311 thereafter. The sync-type probe answers in 2-3 ms once the source
is up, and the solve costs about ten milliseconds, so **nothing about acquiring
a source takes minutes**.

**The Wii's output mode decides whether it acquires at all.** At 480p it
acquires and holds -- 524 lines x 59.80 Hz in 15.2 s, `PLLAD_MD` 1096 against
`STATUS_SYNC_PROC_HTOTAL` 1096, `HPERIOD_IF` 214, a clean picture. At 480i it
never reaches `acquired`: the field count alternates 259/260 by construction and
the steadiness run needs four identical samples, so the solve never completes and
the picture rolls while every register reads correct.
`docs/investigations/mode-detect-answers-before-any-measurement.md`.

The Wii on `ypbpr` acquires 9.3 s after the input switch and then holds.
Sampled from `loop()` at 35 Hz for 30 s: `STATUS_SYNC_PROC_VTOTAL` 310 in
1050/1050, `VPERIOD_IF` 624 in 1050/1050, `HPERIOD_IF` 431/430, no interrupts
latched at any point, and a clean full-screen picture. The RISC PC reads 311 in
1043/1043 the same way.

**Both sources are therefore seconds, and a wait of minutes is a fault to
diagnose rather than a budget to allow.** Until the coast pair had one owner
this source never converged at all: the count came back in five families and
`PLLAD_MD` halved and doubled under it, for as long as anyone watched.
`docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md`.


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
`docs/video-source-acquisition.md`.

### The line rate the bench reaches is the MONITOR DEFINITION's, not the machine's

**The machine now runs `RetroScaler-Acorn.mdf`, not a stock file** -- 63 modes,
15.6 kHz to 1080p, Acorn's own timings verbatim plus a CEA-861 block, built by
`RiscPc/tools/video-source/make_acorn_mdf.py`. `MODES` lists 1280x720 and
1920x1080, so the ceiling below is history for this bench and is kept because it
is what a stock file reaches.

Two things about that file are worth knowing before reading a measurement taken
on it.

**Thirteen modes carry a `mode_name:` and the rest do not.** That field is what
puts a mode on the Display Manager menu -- AKF50's own version history says so
-- and a nameless mode is still reachable by `MODE` and by ModeServ. So the
desktop's monitor icon listing nothing is not a fault.

**AKF50 wins any dedup tie.** Seven 15.6 kHz PAL modes are defined by both AKF11
and AKF50 with the same line rate, the same 312 lines and the same 50.08 Hz
field, and different sync widths behind them: 320x256 is 36 units of hsync in
512 under AKF50 against AKF11's 38. Every measurement recorded against the bench
is against AKF50's timings, and while AKF11 was winning the key the divider
settled on 2216 instead of 2208.

The stock AKF50 definition has **28 modes, none above 37.9 kHz**, and that
ceiling is the file rather than VIDC20. Surveyed across the thirteen Acorn
definitions RISC OS ships:

| definition | modes | fastest line | at |
|---|---|---|---|
| AKF11-40 | 7 | 15.6 kHz | 8 MHz |
| AKF50 / 52 / 53 | 28 | 37.9 kHz | 40 MHz |
| AKF60 / 65 / 72 / 74 | 28 | 48.4 kHz | 65 MHz |
| AKF80 / 85 | 31 | 63.7 kHz | **110 MHz** |
| AKF91 / 92 | 34 | 74.8 kHz | 136 MHz |

**The machine's own ceiling is 110 MHz**, and what says so is the round number
appearing as the top of two independent files where every other pixel rate in
them is an awkward one. 100.00 MHz caps the AKF60 group the same way.

What the faster files reach:

| | AKF50 | AKF60 | AKF80 |
|---|---|---|---|
| slowest line | **15.6 kHz** | 31.5 kHz | 31.5 kHz |
| 1600x600 | 37.9 kHz | **46.9 kHz** | 46.9 kHz |
| 1024x768 | -- | 48.4 kHz | 60.0 kHz |
| 1280x1024 | -- | -- | **63.7 kHz** |

**NO STOCK FILE HAS BOTH ENDS.** AKF60 and AKF80 clear the cliff and have no
15 kHz mode at all -- no 311-line mode of any width -- so switching the machine
to one costs the everyday 320x256@50 source, which is the whole low-line-rate
and line-doubler case. That is what a custom definition is for, and
`RiscPc/tools/video-source/make_test_mdf.py` builds one: it now carries the 15
kHz modes alongside four timings lifted from AKF80, the pair at ONE line count
with only the rate moving.

**`docs/investigations/the-decimators-filter.md`'s open trade needs exactly
that** -- above a 39.2 kHz line rate the pass-through divider's own cap pushes
CKO past 80 MHz, `PLLAD_KS` lands on the top crossover row and there is no
faster tap to oversample from.

It also retires *the AKF50 has no 1280x1024* as a statement about the bench:
it is a statement about one file, and AKF80 has it at 63.7 kHz, which is 1.25
MB at 8 bpp against the machine's 2 MB of VRAM. The depth matters -- the PM5544
card's palette comes out wrong below 256 colours, measured.

### It is the only source that can put ONE divider under two line rates

The engine halves `PLLAD_MD` when the line rate doubles, and at 1096 the ADC PLL
does not lock -- measured 0 in 667 of 667 samples at 720x576@50, against 1 in
499 of 552 at 320x256@50 where the divider is 2208. Because this source reaches
both from one cable, it is what separates the divider from the source: the Wii
reads the same 0 in 564 of 564 at the same 1096 on a different connector.
`docs/investigations/the-adc-pll-does-not-lock-at-the-half-divider.md`.

**The PM5544 card is what makes that visible**, and what the card shows says
which of two faults it is. Beating confined to the finest grating is a sampling
ratio, which can only alias where the detail is fine enough; beating that
reaches flat area is a clock that is moving. The card captions itself with the
mode, sync type and scan mode, so a photograph carries its own conditions.

`SYNC 0|1|3` switches the machine between separate and composite sync, which
makes it the source for sync-type work. It is one CMOS value re-applied to
VIDC20's external register, not a mode-file setting.

**So a composite-sync RGBHV source is on the bench**, and `SYNC 1` is all it
takes to reach it. A branch that wants csync *and* RGBHV together -- which
neither the separate-sync default nor the Wii's sync on green can reach -- has a
live source, so it is bench testable rather than host-test-only. It is also the
only way to move the sync type with nothing else changing: same machine, same
cable, same input, same raster.

**ModeServ exposes it: `INTERLACE ON|OFF`.** The MDF format has ten keys and
none is interlace, so it goes through `*TV vert,interlace` -- 0 meaning ON, the
sense inverted -- and the mode is re-applied so the change reaches the wire.
There is no OS call that reads it back, so the reported state is what the server
last set and a fresh server reports OFF whatever `*TV` was.

**What that would buy, and it is now the only way to get it.** No register on
this board has been shown to establish interlace: the dedicated status bits call
the RISC PC's 311-line progressive mode PAL interlace, bit-identical to the Wii's
real 576i, and `VPERIOD_IF` counts half-lines so the two read 623 against 624
**with the RISC PC on composite sync**. On separate sync it reads debris and
`STATUS_IF_VT_OK` 0 -- the measurement needs the sync separator in the path, so
a reading taken on `SYNC 0` is not a contradiction of the 623.
`docs/investigations/vperiod-if-on-rgbhv.md`.

The full classification burst, `s0_00..s0_05` in one read, with the RISC PC
progressive on composite sync beside the Wii genuinely interlaced:

| | raw | bits set |
|---|---|---|
| Wii 480i | `8f 00 00 00 40 00` | `SD`, `NTSC_INT`, `INT` |
| RISC PC 311-line progressive | `a7 00 00 00 40 10` | `SD`, `PAL_INT`, `INT`, `SW` |
| RISC PC, separate sync | `00 00 00 00 00 00` | none |

**SETTLED, AND IT IS THE SECOND READING.** With `INTERLACE ON|OFF` the
discriminator runs: same machine, same cable, same input, same mode, same sync
type, only interlace moving. The chip resolves it and the CLASSIFICATION does
not.

| composite sync, `INTERLACE` | line rate | doubled | `VPERIOD_IF` | `VTOTAL` | `s0_00..05` |
|---|---|---|---|---|---|
| 320x256@50 `OFF` | 15625 | 1 | 623 odd x519 | 308 | `a7 00 00 00 40 10` |
| 320x256@50 `ON` | 15625 | 1 | 624 even x519 | 309 | `a7 00 00 00 40 10` |
| 640x200@60 `OFF` | 15697 | 1 | 523 odd x534 | 258 | -- |
| 640x200@60 `ON` | 15697 | 1 | 524 even x536 | 259 | -- |
| 640x480@60 `OFF` | 31690 | **0** | **524 even x526** | 522 | -- |
| 640x480@60 `ON` | 31690 | **0** | **525 odd x529** | 523 | -- |

3163 samples, every state unanimous. **The classification is byte-identical
across a real interlace change**, so `STATUS_IF_INP_INT` and
`STATUS_IF_INP_PAL_INT` carry no interlace information at this line rate -- they
report a vertical-period family and nothing else.

**The half line lands in `VPERIOD_IF`, and which parity carries it INVERTS with
line doubling**, because doubling is what puts the count in half lines. So the
RISC PC gives both halves of that rule on its own, and a mode sweep across the
doubling boundary is what it is for.

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
  `SyncType` and the SOG sync separator against a source that genuinely has it, rather
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
- **HD component**, the standards 5, 6 and 7 branch, has no source, and getting
  one is not worth doing. Reaching those classifications needs the source off
  `vga` -- `getVideoMode()` opens with `sourceIsRgbhv()` and returns the held
  byte, so no timing produces a standard from 3 to 7 on that input -- which
  means a cable to the RGBs port and a bench trip. The branches it would reach
  are deleted by step 12 of `video-source-acquisition.md`.

  **What is worth having is a higher LINE RATE, and that needs neither.** The
  RISC PC reaches 40.7 kHz at 800x600@60, which is what established that a
  composite-sync source at that rate wants the narrow pulse-ignore and does not
  lock above 0x33 -- `investigations/the-pulse-ignore-value-is-measured-not-chosen.md`.
  720p is 45 kHz, so a monitor definition reaches it with no new cable and no
  change of input. VIDC20 cannot do 1080i through an MDF, which has no interlace
  key.

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
one of them. `docs/video-source-acquisition.md` deletes branches keyed to a video
standard, and which branch a change is judged against is decided entirely by
which source is plugged in.
