# The TV5725, in general

gbs-control drives a Tvia TrueView 5725. Its register names carry no explanation,
and the firmware's own comments are sparse, so this is the orientation that the
two PDFs in this directory do not give you on their own.

Everything here is either cited to a datasheet page or marked as measured on this
unit. Where the two disagree, that is called out rather than smoothed over.

## The two PDFs, and which one you want

| file | what it is | use it for |
|---|---|---|
| `Tvia TrueView 5725 Data Sheet (DS-5725-3.2).pdf` | 46 pages: block diagram, pinout, functional description, application circuits | **what a block does** |
| `Tvia TrueView 5725 Registers Definition (RD-5725-1.1).pdf` | 236 pages: every register, bit by bit | **what a register means** |

The register definitions are organised in chapters that do *not* map one-to-one
onto the I²C segments — see the segment map below, which is the translation you
actually need at the keyboard.

Reading them needs `pdftotext`/`pdftoppm` (poppler). Grep the text for a register
name to find the page, then render that page and read it as an image — the tables
lose their structure in text extraction, and the bit ranges are the whole point.

## The signal path

```
analog RGB ──► ADC ──► Input Formatter ──► De-interlacer ──► memory
                                                                │
     panel ◄── DAC ◄── Video Processor (scaler + output timing) ◄┘
```

Two things about this shape matter more than any individual register:

**The input and output halves are independent timing domains.** The input side
samples whatever the source emits; the output side generates its own raster from
its own PLL. They meet in memory. So an input-side register can never be "too big
for the screen" — it can only be wrong about the source.

**Each half has its own horizontal coordinate space, and they share no origin.**
This is the single most expensive thing to learn the hard way. See below.

## The analog inputs, and which one this board uses

The chip has **three RGB channels but only two external sync positions**, and the
mismatched numbering is what makes the pinout look wrong. From DS Table 3, "Analog
Video input Pins":

| group | pins | numbering |
|---|---|---|
| RGB triples | R0/G0/B0, R1/G1/B1, R2/G2/B2 | 0–2, three of them |
| sync-on-green | SOG0 (pin 61), SOG1 (64) — **no SOG2** | follows the channel |
| external H/V | HSIN1/VSIN1 (44/45), HSIN2/VSIN2 (46/47) | 1–2, a shared pair |

There is no HSIN0 and nothing is missing. External sync is not a per-channel
resource, so it does not inherit the channel numbering — the chip provides two
pairs of sync pins and numbers them from 1. SOG *is* per-channel: it is a separate
pin carrying the same green signal for sync-tip clamping, sitting next to its
channel's G pin (SOG0 at 61, G0 at 62), so it is numbered with the channel. The
numbering tells you which kind of resource you are looking at.

Two independent selectors:

| register | picks |
|---|---|
| `ADC_INPUT_SEL` (S5_02 b7-6) | the RGB triple — `00` R0/G0/B0/SOG0, `01` R1/G1/B1/SOG1, `10` R2/G2/B2, `11` reserved |
| `SP_EXT_SYNC_SEL` (S5_20 b3) | the external sync pair — one bit, two positions |

Any channel can run with either sync pair. **Sync-on-green is not a third position
on that axis**: it arrives with the channel selection, which is why the enum spells
it "R1/G1/B1/**SOG1**", and why channel 2 with SOG is the one combination that
cannot exist. `ADC_SOGEN` only gates whether that channel's SOG is used.

**This board uses channel 1.** Measured 2026-08-02: `ADC_INPUT_SEL` reads 1. The
firmware hardcodes it — `OLEDMenuImplementation.cpp` defines `RGB1 0x01` and
`YUV0 0x00`, and every RGB-family mode (`InputVGA`, `InputRGBs`, `InputRGsB`)
writes `RGB1` while component writes `YUV0`. **Nothing writes `0x02`.** Channel 2
is unused, and with no SOG pin it could only ever work with external H/V sync.

The firmware names the `SP_EXT_SYNC_SEL` values `HV_Enable 0x00` and
`HV_Disable 0x01`, but RD calls the bit "Ext 2 set Hs_Hs select" — a selector, not
an enable. If RD is right, the RGBs/RGsB modes writing `HV_Disable` are pointing
the mux at HSIN2/VSIN2 rather than disabling anything, and sync falls back to SOG
because that pair is unconnected. Same outcome, misleading names. **Not
confirmed** — RD is too terse to settle it without a bench test.

## Register segments

gbs-control addresses the chip as six segments. This is the map from segment to
functional block, which the register PDF's chapter numbering obscures:

| segment | block | register prefix | typical contents |
|---|---|---|---|
| **0** | status (read-only) | `STATUS_`, `HPERIOD_IF` | live measurements of the input |
| **1** | Input Formatter | `IF_` | capture window, input line length |
| **2** | De-interlacer | `MADPT_` | motion adaptation, noise reduction |
| **3** | Video Processor / display | `VDS_` | output raster, scalers, output filters |
| **4** | memory / capture-playback | `SDRAM_`, `MEM_`, `PB_`, `RFF_`, `CAP_` | frame buffer layout — see below |
| **5** | ADC, sync processor, PLLs | `PLLAD_`, `SP_`, `PA_`, `ADC_` | sample clock, sync retiming, phase |

Segment 0 registers `0x00`–`0x2F` are **live measurements, not configuration**.
They change on every read even when nothing is wrong, which is why
`dump_registers.py` captures them separately and excludes them from diffs.

## The two horizontal coordinate spaces

Confusing these produces pictures that are wrapped, clipped, or built from
fragments of the wrong part of the line — and it looks like a hardware fault
rather than an arithmetic error.

### Input side — `IF_` units

The ADC samples each input line `PLLAD_MD` times. The Input Formatter stores at
**half** that rate, so one input line is `IF_HSYNC_RST` = `PLLAD_MD / 2` units.

The capture window (`IF_HB_SP2`, `IF_HB_ST2`) is measured in those units. They are
*blanking* start and stop, so the active region is `SP2 .. ST2`:

- `IF_HB_SP2` — where blanking **stops**, i.e. the left edge of captured video
- `IF_HB_ST2` — where blanking **starts**, i.e. the right edge

An edge at or beyond `IF_HSYNC_RST` is out of bounds. Blanking then never starts,
the line buffer wraps, and the picture disintegrates into horizontal drag with
periodic banding. The firmware states the bound in `shiftHorizontalLeftIF()`:

```c
if (IF_HB_ST2 < IF_HSYNC_RST) { write(IF_HB_ST2); } else { write(IF_HB_ST2 - IF_HSYNC_RST); }
```

Those two helpers, incidentally, are **defined but never called** anywhere in the
firmware, and neither is `setIfHblankParameters()`. Nothing drives the capture
window at runtime; it comes from the preset.

`IF_HSYNC_RST` goes further: **no live code path writes it at all.** It is at
`s1_0E`, outside the range the preset arrays cover (`s1_10`–`s1_3F`), and the
three functions that compute it are all dead code. So it holds whatever it was
last set to, and **`/uc?h` will not repair it** — a stale value there survives the
reset that fixes everything else, and breaks the `PLLAD_MD / 2` invariant while
every other register reads correct. Check it explicitly after any reset.

### Output side — `VDS_` pixels

`VDS_HSYNC_RST` is the output line length **in real output pixel-clock periods**,
and `VDS_VSYNC_RST` is real lines. There is no hidden scale factor between them.
The check: decode every preset header and compute
`(htotal + 1) × (vtotal + 1) × the preset's display clock`, and all fourteen land
on 59.8x Hz or 49.7x Hz.

| preset | htotal | vtotal | active | clock | → |
|---|---|---|---|---|---|
| `ntsc_1920x1080` | 1602 | 1126 | 1008×1085 | 108 MHz | 59.87 Hz |
| `pal_768x576` | 2600 | 626 | 1928×578 | 81 MHz | 49.77 Hz |
| `pal_downscale` | 2579 | 314 | 1944×286 | 40.5 MHz | 50.01 Hz |

The *active* window inside the line is `VDS_DIS_HB_SP .. VDS_DIS_HB_ST`, and the
panel stretches that active region to fill itself. So the on-screen width of the
picture is governed by the ratio of active window to output line, not by the
capture window.

**A preset's name is not its active pixel count.** `pal_768x576` emits a
1928-pixel active region, not 768. The vertical name is real (578 ≈ 576) because a
mode's vertical dimension is lines either way; the horizontal name means "pixels
at the standard clock for that mode", and these rasters do not run at it —
`pal_768x576` is a 768×576@50 raster oversampled ×2.66 (32.1 µs per line against a
standard 31.9 µs). A TV identifies a mode by line rate and line count, which are
correct, so it reports the standard name and stretches whatever active region it
receives. Comparing `VDS_HSYNC_RST` against the resolution the TV reports compares
a pixel count against a label, and will not reconcile.

`VDS_HSCALE` maps captured units onto output pixels, and it can only **magnify**.
If the captured region already fills the output active window at 1:1
(`VDS_HSCALE_BYPS` set), then every `HSCALE` below 1023 overflows and wraps.

### They do not share an origin

The Input Formatter takes its line origin from a *retimed* HSync, not the sync
edge. The firmware computes the retime point as a fixed fraction:

```c
GBS::SP_RT_HS_SP::write(GBS::PLLAD_MD::read() * 0.93f);
```

A hardcoded 0.93 standing in for a measurement. So a position expressed in source
pixels cannot be converted into `IF_` units without an offset — and on this unit
the offset is large enough that the source's active region **straddles the IF line
boundary**, wrapping around the end of the line. Measured on a RISC PC AKF50
480×352 source, 2026-08-01.

That is the likely root of the whole class of geometry failures on sources
gbs-control has no tuned preset for.

## The frame buffer

**8 MB, one chip, 32 bits wide.** Three independent lines of evidence agree, which
is worth recording because segment 4 is otherwise the least-explored part of the
chip:

- The board schematic (`GBSC-AV-IR-v1.1-20240923.pdf`, SDRAM sheet) fits a single
  **EM638325TS-6/-6G** in an 86-pin package — an x32 part.
- DS Figure 17, "ONE 512Kx32x4BANK MEMORY", labels the same arrangement
  "2M x 32 SDRAM": 2M words × 32 bits = 64 Mbit = 8 MB, one chip select, four banks.
- The buffer start address is 21 bits (`S4_51` = `[7:0]`, `S4_52` = `[15:8]`,
  `S4_53` = `[20:16]`) and RD adds "Mapping to **32bits** width data bus field".
  2²¹ words × 4 bytes = 8 MB exactly — the address space fits the part with
  nothing spare and nothing short.

So **buffer addresses are in 32-bit words, not bytes**. The chip could take other
arrangements — MD[31:0], MCS0#/MCS1#, and FBCLK doubles as "Chip Selection 2 for
6MByte external memory"; DS Figures 14–18 cover three 1Mx16x2bank, one or two
1Mx16x4bank, and this one. This board took the single-chip x32 option.

### How it is laid out, measured on this unit

| register | value | meaning |
|---|---|---|
| `RFF_WFF_STA_ADDR_A` | 0 | frame buffer starts at address 0 |
| `RFF_WFF_STA_ADDR_B` | 1 | buffer B — unused, see below |
| `PB_DB_BUFFER_EN` | 0 | **double buffering off** |
| `PB_DB_FIELD_EN` | 0 | double field display off |
| `CAP_SAFE_GUARD_B` | `0x1FFFFF` | guard at the top of the space — wide open |
| `PB_FETCH_NUM` | 256 | pixels fetched per playback burst |
| `PB_CUT_REFRESH` | 1 | explicit refresh request generation disabled |

**It runs single-buffered from address 0.** Capture writes and display reads share
one buffer with no page flip — the arrangement in which a display window outrunning
the write pointer shows up as a repeat, with no second buffer to hide it. Buffer
B's start address is meaningless while `PB_DB_BUFFER_EN` is 0.

The sizing may be why it is single-buffered. `pal_768x576` is 2600 × 626 =
1,627,600 pixels against 2,097,152 addressable words: at 16 bpp two buffers fit,
at 32 bpp one buffer alone takes 1.63M words and a second could not fit at all.
The buffer's bytes-per-pixel is **not established**, so treat that as a constraint
to check rather than a conclusion.

`PB_CUT_REFRESH` = 1 disabling refresh looks alarming and is probably deliberate: a
buffer swept completely every 20 ms activates every row far inside the SDRAM's
64 ms retention window, so explicit refresh is redundant bandwidth. **That
rationale is inferred**, not stated by either PDF.

This is not the memory behind the capture-width limit. That bound is the internal
line-double FIFO, and 8 MB is nowhere near what constrains how wide a line can be
captured.

## Clocks

| PLL | what it clocks | range |
|---|---|---|
| **PLLAD** | ADC sample clock, locked to HSync | below 10 MHz to 162 MHz (DS, "Functional Description → ADC") |
| **PLL648** | memory and output, a 648 MHz VCO | `PLL_MS` selects 648/N: 81, 108, 129.6, 144, 162, 185, 216 MHz |

**`PLL_MS` = `010` is documented, and it is not a frequency.** An earlier version
of this section said RD did not cover the code and guessed it was "presumably 81
or 108 MHz". **Both halves are wrong.** RD-5725-1.1's PLL648 CONTROL 00 table
lists all eight codes — `000` = 108 MHz and `001` = 81 MHz as well as the five
above — and `010` reads *"memory clock from FBCLK (pin110)"*. It selects an
external clock input, so there is no frequency to pin down from the datasheet;
the answer is a board fact, and the schematic's text layer gives pin names
rather than nets, so this repo cannot read it.

**The clock is no longer a preset table's to choose.** The twelve tables split
it six/six between `111` (129.6 MHz) and `010` (FBCLK), following nothing —
`ntsc_240p` took the fast internal clock while `pal_1280x1024` took the pin.
`Tv5725::MemoryBus` derives one value instead, and `Tv5725::SdramTiming` is the
derivation: fastest code that keeps the EM638325TS-6's tCK, tRCD and tRP, which
is **`011` = 162 MHz**. Measured clean on the bench 2026-08-13 at 1920x1080, as
were 129.6 MHz and FBCLK. `docs/EM638325-Industrial_Rev-3.2.pdf`.

The three writes of `PLL_MS` = `010` in `gbs-control.ino` are the low-power and
bypass paths, where the memory is out of the picture; they are untouched.

`PLLAD_MD` (seg 5 `0x12`, 12 bits) sets samples per input line. It **cannot be
moved alone** — `IF_HSYNC_RST`, `IF_LINE_SP` and `SP_RT_HS_SP` are all derived
from it, and the PLL needs latching afterwards. The firmware's own `/sc?n` handler
is the reference sequence; `tools/gbsc-pro-hwtest/pllad.py` reproduces it in both
directions, since `/sc?n` only increments.

Being inside the 10–162 MHz range is not sufficient for lock. `PLLAD_KS` selects a
VCO band and is set from `VTOTAL` at preset time, so a large `PLLAD_MD` change can
fall outside the current band. Measured: at a 15.6 kHz source, `PLLAD_MD 2553`
(39.8 MHz) locked and `3072` (47.9 MHz) did not, with `PLLAD_KS` unchanged.

### `PLL648_CONTROL_01 = 0x75` selects the external clock — it is not a sentinel

An earlier version of this section called `0x75` "a sentinel, not a divider".
**That is wrong.** Seg 0 `0x41` is the datasheet's display clock tuning register
(RD-5725-1.1, "PLL648 CONTROL 01"), and every value the firmware writes is a
real hardware selection:

| bit | field |
|---|---|
| 7 | `PLL_4XV` |
| 6 | `PLL_2XV` |
| 5-4 | `PLL_VS4` |
| 3-2 | `PLL_VS2` |
| 1-0 | `PLL_VS` |

`PLL_VS4 = 11` selects an **external** clock source instead of the internal PLL,
and `PLL_4XV`/`PLL_2XV` pick which. `0x75` is `4XV=0, 2XV=1, VS4=11` — the
datasheet's **PCLKIN** row. So the firmware writes `0x75` because that is
genuinely how you tell the chip to take its display clock from the Si5351, and
reads it back to learn which source is live. That is legitimate inference, not a
magic marker.

The set `Tv5725::DisplayClock::hzFor()` maps — `0x25 0x35 0x45 0x55 0x65 0x85
0x95 0xa5` — decodes to real display clocks (40.5, 54, 64.8, 81, 108, 129.6,
162 MHz and one more external row). `0x35` is `4XV=0, 2XV=0, VS4=11`, so it is
*also* an external-source selection, not an internal divider.

Where the firmware **is** fragile: three sites test `!= 0x35 && != 0x75` to mean
"this must be a preset's own display clock, save it"
(`gbs-control.ino:6737`, `:6842`, `:8306`). That is a heuristic on two magic
values, and a preset legitimately wanting either would be misclassified.

Do not use `0x41` as a health check. A **healthy** 800x600 capture
(`HPERIOD_IF` 176) reads `0x35`, and so does a broken one — see
[the two signatures](#hperiod_if-railing-is-two-different-faults-under-one-name).

The failure this shape invites is worth knowing, because its symptom points
nowhere near its cause: an unmapped value leaves the Si5351 programmed with a
frequency from an earlier preset, the TV cannot lock to the resulting timing and
goes blank, and **every scaler register still reads exactly right**. If the
picture is gone and the input side is locked with sane geometry, suspect the
output clock before the scaler.

## What the chip measures, and what it only appears to

Two different pairs of registers report "totals", and they are not the same thing.

| register | segment | units | used by firmware for |
|---|---|---|---|
| `STATUS_SYNC_PROC_HTOTAL` | 0 `0x17` | **sample clocks** | gating `runAutoBestHTotal`, `runAutoGain`, frame sync |
| `STATUS_SYNC_PROC_VTOTAL` | 0 `0x1B` | lines | preset selection, scaling threshold |
| `HPERIOD_IF` | 0 `0x06` | **source H total / 4** | mode-change stability — a real source measurement |
| `VPERIOD_IF` | 0 `0x07` | lines | deinterlace cadence, mode detect |

`STATUS_SYNC_PROC_HTOTAL` is measured in the scaler's own sample clocks, and a
locked PLLAD produces exactly `PLLAD_MD` samples per line — so it reads back equal
to `PLLAD_MD` and says nothing about the source's pixel count. Measured 2558/2558
on this unit and 2345/2345 at a different divider. The firmware's three `±3`
guards comparing the two are self-satisfying.

`STATUS_SYNC_PROC_VTOTAL` is **not** the same trap. It counts in *lines*, so
nothing in the PLLAD path can feed a setting back into it. It is a genuine
measurement, and it is the one to trust when the IF disagrees.

## `HPERIOD_IF`, the one measurement of the source

The datasheet defines it as *"source H total measurement result. The value =
input source H total pixels / 4"*. Unlike `STATUS_SYNC_PROC_HTOTAL` it does not
move when the sample clock changes:

| `PLLAD_MD` | `HPERIOD_IF` |
|---|---|
| 2345 | 428 |
| 2000 | 428 |
| 2600 | 428 |

120 reads at each setting, zero spread. So the source line is `428 x 4 = 1712`
units, and the `x 4` in the firmware's averaging undoes the datasheet's `/ 4`.
The units are ITU-R BT.601 double-rate — the value the firmware substitutes on
saturation, `1716`, is exactly an NTSC line at 27 MHz.

### The unit is 27 MHz, and the counter is zero-based

```
H total (27 MHz ticks) = (HPERIOD_IF + 1) * 4
line period (us)       = (HPERIOD_IF + 1) * 4 / 27
```

Measured across ten stock AKF50 modes, 15.6-37.9 kHz, frozen. Four have a line
that is a whole multiple of 4 ticks, and on those it is exact:

| mode | exact 27 MHz ticks | `HPERIOD_IF` | `(H+1)x4` | error |
|---|---|---|---|---|
| 320x250 @50 | 1728.00 | 431 | 1728 | **0.0** |
| 640x512 @50 | 1008.00 | 251 | 1008 | **0.0** |
| 800x600 @56 | 768.00 | 191 | 768 | **0.0** |
| 320x480 @75 | 720.00 | 179 | 720 | **0.0** |

Mean error over all ten is **0.77 ticks (29 ns)** for `(H+1)*4` against 4.28 for
`H*4`. A least-squares fit over the same data gives 26.997 MHz and +1.04 counts —
27 MHz and one count, arrived at independently.

The residual is the `/4`: a line of 858.0 ticks divides to 214.5, so the register
reports 214 or 213 and the reading is ±2 ticks (74 ns) on any line that is not a
multiple of four. That is the floor of what this register can tell you, not a
fault.

### The exact modes give a free validity test

| mode | 27 MHz ticks | ÷4 | exact? | observed |
|---|---|---|---|---|
| 320x250 F50 | 1728.0 | **432.00** | yes | `431` — one value, 152 samples |
| 640x512 F50 | 1008.0 | **252.00** | yes | `251` — one value |
| 320x480 F75 | 720.0 | **180.00** | yes | `179` — one value |
| 640x200 F60 | 1721.2 | 430.31 | no | `429/428` |
| 640x352 F60 | 1235.5 | 308.88 | no | `308/310/311` |
| 320x480 F60 | 858.0 | 214.51 | no | `212/213` |
| 320x480 F73 | 713.1 | 178.29 | no | `176/177` |

Every exact mode reads a single steady value; every inexact one reads two or
three. So on an exact mode — 320x250, 320x256, 640x512@50, 320x480@75 — a healthy
reading is provably single-valued, and *any* spread is a real fault rather than
quantisation. Use one of those when you need to know whether the counter itself
is working.

### Validating a reading

**It fails in three ways, and two of them look healthy.**

| reads | meaning |
|---|---|
| a plausible value, dead steady | healthy |
| **511** (9-bit full scale) | railed — a PLL latch knocks Mode Detect out |
| **0** | railed the other way, after an `SFTRST_MODE_RSTZ` pulse |
| four or more distinct values in a settled window | the counter is returning a different wrong answer per read |
| **a single stable value that is simply wrong** | the dangerous one — see below |

The third signature is the one every health check misses. `VTOTAL 524` expecting
`212` has read `191` on 19 of 19 settled samples, and `19` on 23 of 23 — stable,
nowhere near a rail, and 21 and 193 counts out where quantisation is worth ±0.5.
`19` counts is 2.96 µs, impossible as a line period at these rates but close to a
plausible hsync pulse width, so the counter can apparently latch the wrong edge
and time the sync pulse instead of the line.

**Validate against the expected value for the mode, not against the rails:**

```python
expected = {311: 431, 261: 429, 363: 308, 448: 214,
            499: 179, 519: 176, 524: 212, 533: 250}   # frozen-unit measurements
bad = abs(median(samples) - expected[vtotal]) > 8
```

Judge by the fraction of far-from-expected samples, not by spread: one bad read
in fourteen is a dropped sample, and a raw spread test flags it as a fault.

**`STATUS_IF_HT_OK` is not a validity signal.** It reads 1 both when railed at
511 and when healthy, because Mode Detect locks on 22 consecutive *stable*
readings and a railed value is perfectly stable. Check the value, not the flag.

**Bypass does not predict the value.** In RGBHV bypass the IF is out of the video
path, but not out of the measurement path: bypass has produced `0`, `511` and a
correct `177` against an 800x600 source in the same configuration. Do not read
"we are in bypass" as "`HPERIOD_IF` is meaningless" — check it against the mode.

**Judge only settled samples.** Discard ~6 s after any mode change; a transient
`97`/`98` for one or two samples is normal, and scoring those as failures gave 15
false positives in a single run.

### Read it once, just after the preset apply

The instability is a runtime phenomenon. The measurement you want is taken at
mode-change time, before the display is configured, once per mode — and there it
behaves. Four consecutive `/uc?h` cycles:

| trial | settled after | value | held for 30 s |
|---|---|---|---|
| 1 | 5.6 s | 428 (425 while settling) | yes |
| 2 | 2.2 s | 428 | yes |
| 3 | 3.2 s | 428 | yes |
| 4 | 2.2 s | 428 | yes |

So: apply the preset, wait for the value to hold steady — allow up to ~6 s —
range-check it, take it once, and keep it. That it drifts or rails later does not
matter, because nothing needs to read it again until the next mode change.

It does rail during normal running, not only when provoked: 428 to a railed 511
with the unit left alone between test runs, cleared by `/uc?h`. The ADC PLL blips
recorded in the soak — `HTOTAL` wobbling ±1, seen live as 2344/2345/2346 — are the
likely trigger, since a PLL latch is known to knock the block out. So it is not
continuously available, it fails to a stable-looking value, and it needs a preset
reload to recover.

Why it rails is not established. See
[investigations/hperiod-if-railing.md](investigations/hperiod-if-railing.md).

## `VPERIOD_IF` is invalid on RGBHV, and the chip says so twice

Measured on the bench RISC PC at 320×256 (VTOTAL 311), RGBHV **scaling**, firmware
unfrozen, picture correct:

| register | reads | verdict |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` s0 `0x1B` | **311** | correct |
| `VPERIOD_IF` s0 `0x07`+`0x08` | **129**, and **0** after a Mode Detect reset | never a valid measurement |
| `HPERIOD_IF` s0 `0x06`+`0x07` | **431** | correct |
| `STATUS_IF_VT_OK` s0 `0x00` bit 0 | **0** | honest |
| `STATUS_IF_VT_BAD` s0 `0x05` bit 3 | **1** | honest |
| `STATUS_IF_HT_BAD` s0 `0x05` bit 2 | **0** | horizontal genuinely fine |

Only the vertical half is bad, and the chip flags it on two separate bits.

**It has never worked here, and the archive proves it.** Across 22 snapshots
spanning 2026-08-01 to 08-03, including `SOLVED-*` states captured with a
confirmed clean full-screen picture: `VT_OK` = 0 and `VT_BAD` = 1 in **22 of 22**,
`VPERIOD_IF` scattered across 13, 21, 25, 26, 45, 72, 75, 84, 195, 249, 361, 545
with no relation to the source, and `SP_VTOTAL` correct every time.

**A non-zero `VPERIOD_IF` on RGBHV is debris, not data.** Pulsing Mode Detect
(`SFTRST_MODE_RSTZ`, s0 `0x47` bit 1) takes it from 129 to 0, where it stays,
while `HPERIOD_IF` holds at 431. The counter is not returning a bad measurement —
it is never completing one, and 129 was latched from an earlier state.

**The header definition is correct — do not "fix" it.** `VPERIOD_IF` is
`UReg<0x00, 0x07, 1, 11>`, where the last two parameters are *bit offset and
width*, not a bit range — a natural misreading, since the datasheet names the
field `IF_VPERIOD_[10:0]`. `byteSize = (1 + 11 + 7) / 8 = 2`, so it reads `0x07`
and `0x08` in one auto-incremented transaction and decodes `IF_VPERIOD[6:0]` from
`0x07[7:1]` plus `IF_VPERIOD[10:7]` from `0x08[3:0]` — the datasheet's split,
7 + 4 = 11 bits. Register `0x07` is tiled with no gap: bit 0 is `IF_HPERIOD[8]`,
which is why the offset is 1. Encoding a healthy 311 gives `0x07 = 0x6F,
0x08 = 0x02`, which decodes back to 311; an off-by-one in either parameter
returns 155, 310 or 622.

**The firmware already routes around it**, and every consumer needing a line count
on RGBHV is guarded:

| `gbs-control.ino` | consumer | protection on RGBHV |
|---|---|---|
| `shiftVerticalUpIF` / `DownIF` | vertical shift | explicit `STATUS_SYNC_PROC_VTOTAL` substitution |
| SD blanking nudge | | gated `videoStandardInputIsPalNtscSd()` |
| deinterlace / field parity | | gated `videoStandardInput == 1 \|\| == 2` |
| `getVideoMode` HD branch | | gated on `STATUS_04` bits |
| console print | | prints `v:----` when `STATUS_IF_VT_BAD` is set |

**The axis is the input standard, not scaling.** `VPERIOD_IF` is trustworthy in
the SD and HD modes that traverse the IF — the deinterlace code keys *exact
equality* on 522/524/526/622/624/626 for field parity, which only works if it is
accurate to the line — and untrustworthy on RGBHV whether bypassed or scaled.

Why is hypothesised but not established. See
[investigations/vperiod-if-on-rgbhv.md](investigations/vperiod-if-on-rgbhv.md).

## Mode Detect classifies, it does not measure

`presetMdSection.h` sets `s1_62`–`s1_7F`, and every one is of the form *"if the
horizontal period number is equal to the defined value, it's XGA 75 Hz"* — VGA,
SVGA, XGA and SXGA at 60/70/75/85 Hz, plus one HD1250P slot. It is a VESA lookup
table. There is no entry for a 15 kHz 50 Hz mode, and the two custom-mode slots
(`s1_80`, `s1_81`) are parked at `0xff`.

So a locked Mode Detect answers "which VESA mode is this", not "what is the H
period". On a RISC OS source the vertical side never matches anything and
`STATUS_IF_VT_BAD` stays 1. That is expected, and it does not stop `HPERIOD_IF`
being valid.

## Bounds worth knowing before calculating

| register | width | ceiling |
|---|---|---|
| `VDS_HSYNC_RST` | 12 bits | output line ≤ 4096 px |
| `VDS_VSYNC_RST` | 11 bits | output frame ≤ 2048 lines |
| `IF_HSYNC_RST` | 11 bits | input line ≤ 2048 units, i.e. `PLLAD_MD` ≤ 4095 |
| `VDS_HSCALE` | 10 bits | 1023 ≈ 1:1, lower magnifies |
| `PLLAD_MD` | 12 bits | 4095 |
| `HPERIOD_IF` | 9 bits | source line ≤ 2044 units, i.e. line rates down to ~13 kHz |

The chip has **no fixed set of output modes**. gbs-control's eight presets
(`Output960P` … `OutputBypass`, see `options.h`) are a firmware convenience, not a
hardware capability list — any raster inside those bounds can be generated.

## Resetting

`/sc?q` runs `resetDigital()` + `ResetSDRAM()` + `togglePhaseAdjustUnits()` — a
digital and SDRAM reset that leaves the register values alone. Useful as hygiene
between experiments, and as a recovery short of a power cycle.

Whether the chip *needs* it — i.e. whether it can be left in a state that
restoring the registers does not undo — is **not established**. It was claimed on
2026-08-01 and retracted the same evening: the evidence was a photograph that
turned out to predate the register write it was being compared against. If the
question matters, test it with confirmation that each observation postdates the
change it is attributed to.

## The rest of the board

From `BOM/GBSC-AV-IR-v1.1-20240923.xlsx`. Only the TV5725's documentation is in
this directory; the others are worth collecting, particularly the clock generator
and the HDMI transmitter, since both sit in the path of any time-varying artefact.

| chip | role |
|---|---|
| **TV5725** | the scaler — documented here |
| **MS9288A** | HDMI transmitter (the output) |
| **Si5351A / MS5351M** | programmable clock generator |
| **EM638325TS-6** | SDRAM — the scaler's 8 MB frame buffer, see above |
| **ESP8266MOD** | WiFi MCU, runs gbs-control |
| **HC32F460JEUA** | HDSC MCU — the AV module |
| **ADV7280** | video decoder (composite / S-video in) |
| **ADV7391** | video encoder (analogue out) |
| **LM1881** | sync separator |
| **CH340T** | USB-serial |

The **Si5351A** is not passive: the firmware detects it (`rto->extClockGenDetected`)
and `FrameSync::runFrequency()` retunes it at runtime to frame-lock the output,
instead of `FrameSync::runVsync()`. A clock generator being adjusted while you
watch is a candidate for interference that varies in time rather than sitting
still. `/uc?X` toggles `disableExternalClockGenerator`, but it **only takes effect
at boot** — the runtime branch tests `extClockGenDetected`, which is set during
startup detection.

## Related

- [investigations/hperiod-if-railing.md](investigations/hperiod-if-railing.md) —
  why `HPERIOD_IF` goes bad, and the hypotheses already refuted
- [investigations/vperiod-if-on-rgbhv.md](investigations/vperiod-if-on-rgbhv.md) —
  why the vertical counter never completes, and the experiment that would settle it
- [riscpc-game-modes.md](investigations/riscpc-game-modes.md) — where these facts were established, and what is still open
- [gbs-control-debug-interface.md](gbs-control-debug-interface.md) — the `/sc?`, `/getreg` and `/setreg` surface
