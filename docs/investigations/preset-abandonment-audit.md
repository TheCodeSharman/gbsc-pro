# What it takes to stop loading presets

**The end state is preset loading switched off completely**, because the engine
needs no preset. What follows is the verification that nothing needing set-up has
been missed.

This is that verification, done by diffing what a preset load writes against what
the firmware writes at runtime. **The answer is that it is far less work than 432
registers, and the reason is that most of a "preset" is not a preset.**

## The split

One `writeProgramArrayNew()` call writes **432 registers** — s0 `0x40-0x5f` and
`0x90-0x9f`, s1 `0x00-0x2f`, s3 `0x00-0x7f`, s4 `0x00-0x5f`, s5 `0x00-0x6f`.

| | count | what it is |
|---|---|---|
| already written at runtime | 114 whole + 89 partial | the engine and the sketch own these |
| **identical in all twelve tables** | **209** | static chip bring-up, not a preset |
| **differs between tables** | **20** | genuinely mode-dependent |

So twelve tables collapse to **one static bring-up block** plus **20 values**, of
which the engine already knows how to derive most of what matters.

Reproduce with the script in this file's history, or by hand: extract the
`// sN_XX` labels from the preset headers, map every `GBS::NAME::write` in the
sketch through `Tv5725.h` to (segment, register, offset, width), and diff.

## The twenty, and what each one needs

| registers | field | verdict |
|---|---|---|
| `s1_14`, `s1_16` | `IF_HB_ST1`, `IF_HB_SP1` | **inert — measured 2026-08-13. Do not write them.** See below; this row used to say the engine must compute them |
| `s1_2d` | `GBS_PRESET_DISPLAY_CLOCK` | **inert.** 0 in every scaling table; the firmware abandoned this stash for RAM |
| `s4_25/26` | `CAP_SAFE_GUARD_A` | **inert.** `CAP_SAFE_GUARD_EN` (s4_21 bit 5) reads **0** on the live unit, so the capture guard is off |
| `s4_27…` | `CAP_SAFE_GUARD_B` | **inert.** `0x1FFFFF`, all 21 bits, in all ten tables |
| `s4_45/46`, `s4_48/49` | `WFF_SAFE_GUARD_A/B` | enabled, but **measured not to matter** — see below. Bring-up |
| `s4_2c`, `s4_2d`, `s4_4e` | `PB_MAST_FLAG`, `PB_GENERAL_FLAG`, `RFF_MASTER_FLAG` | no evidence of line dependence. Bring-up |
| `s4_18`, `s4_1b` | `MEM_DATA_DLY`, `MEM_ADR/CLK_DLY` | 3-bit bus timing trim. Board property, not mode. Bring-up |
| `s4_4a` | `WFF_YUV_DEINTERLACE`, `WFF_LINE_FLIP` | mode-dependent, but only 240p differs — constant for progressive output |
| `s4_33`, `s4_36` | **not registers.** Top bytes of `PB_CAP_BUF_STA_ADDR_A`/`_B` | one map for both standards: write `0x100000`. See below |
| `s4_04` | `MEM_FK_RD_DLY`, `MEM_RD_LAT_PIP` | SDRAM read-latch trim. Board property like `s4_18`/`s4_1b`. Bring-up |
| `s4_23` | `CAP_FF_STATUS` | **a status register.** Writing it is almost certainly inert — see below |
| `s4_4f` | `RFF_GENERAL_FLAG` | same family as `PB_GENERAL_FLAG`/`RFF_MASTER_FLAG` above. Bring-up |

**So the engine needs to compute NOTHING it does not compute today.** Everything
else can be frozen at one mode's values, and the two that were going to need a
new rule turned out not to be consulted at all.

## `IF_HB_ST1`/`SP1` are inert

Set 1 is not used on this path, and the measurement is about as blunt as one
gets.

The IF module has **three** horizontal blanking sets — `IF_HB_ST`/`SP` (set 0) at
`s1_10`/`s1_12`, set 1 at `s1_14`/`s1_16`, set 2 at `s1_18`/`s1_1a`. The engine
owns set 2. Nothing in the firmware has ever written set 0 or set 1; only preset
tables do. `Tv5725.h` documents no selector between them.

Set 1 was written to a **zero-width window** — `IF_HB_ST1 = IF_HB_SP1 = 0` — from
a clean default framing, with set 2, `VDS_HSCALE` and the framing all confirmed
unchanged either side of the write. The screen stayed scaled normally. The full
hardware suite then ran against it: **318 passed, 0 failed**, three
minutes of pad presses, zooms, pans and framing changes, and set 1 read back
`0..0` afterwards — so nothing rewrote it and nothing missed it.

For contrast, in normal operation the two windows **disagree permanently** and
always have: set 1 sits at the preset table's 72..1096 (1024 wide) while set 2
tracks the framing, measured at 134..1143 (1009) at the default and 170..1106
(936) zoomed in. A stale 88-unit disagreement, with a perfect picture. That is
the same signature as `VDS_EXT_HB_*` (CLAUDE.md): a register that looks like it
must matter, held wrong for months, costing nothing.

**The bring-up block should not write `s1_14`/`s1_16` at all**, which is a
stronger answer than freezing them.

### What was NOT tested, so do not over-read it

One configuration: RGB in, progressive, scaling (not bypass), `IF_SEL_HSCALE` 1,
`IF_LD_WRST_SEL` 1, 1080p out, 50 Hz. Untested are interlaced sources, the
scale-down path, RGBHV bypass and the other output modes. If set 1 ever turns
out to matter, that is where it will be.

### The false positive on the way, because it nearly became the finding

The first attempt moved set 1 from 72..1096 to a narrow 600..700 and the picture
DID look different — and it was not the write. An earlier experiment had walked
the zoom pad 26 times and left the framing at `zh=73`, capture 936 instead of
1009, so the comparison was against a remembered un-zoomed picture. Restoring set
1 changed nothing, which is what exposed it. **Set the framing before a hand
experiment, exactly as the pad tests' `framed` fixture does** — inheriting bench
state is the same mistake in a shell as it is in a test.

## The FIFO guards do NOT cause the green bars

Tested on the unit 2026-08-12, at raster 2301 x 1126 / 129.6 MHz with the bars
visible. `WFF_SAFE_GUARD` (s4_42 bit 3) is the only guard actually enabled;
`CAP_SAFE_GUARD_EN` is 0. Clearing it changed nothing: *"Green bars look exactly
the same."*

The hypothesis was that these were sized for a 1445-pixel line and we were running
2301. **It was wrong, and the tables say so too**: across all ten,
`CAP_SAFE_GUARD_A` clusters by FIELD RATE — PAL ~1.4M against NTSC ~1.1M — with no
correlation to htotal at all. `value/htotal` spans 396..972, which is not a rule.

Worth recording because the reasoning was plausible and `PB_FETCH_NUM` is the
precedent: the same class of register, left at an upstream constant, tore the
picture across 80 of 493 framings. This family is not that.

## Progress

**2026-08-13 — the output raster is computed.** `Geometry::solveRaster()` derives
both totals, both sync pulses and the clock seed and writes them from
`applyPresets()`, before `externalClockGenResetClock()`. Measured
1436 x 1126 @ 80.85 MHz before, 1915 x 1126 @ 107.81 MHz after. The table's
raster bytes are now overwritten on every mode change, so the group that made
`pal_1920x1080` "throw away a quarter of every line" is no longer an authority.

**2026-08-13 — `IF_HB_ST1`/`SP1` are inert**, measured, so the engine needs to
learn no new register at all. See the section below.

**2026-08-15 — the last 22 fields acquired owners.** All of them were the same
mistake one level below the one `25e2d36` fixed; see the section below.

**2026-08-15 — the twelve scaling tables are deleted.** `Tv5725::BringUp` runs
each subsystem's `init()` in address order, which is what the 209 identical
registers were; `applyPresets()` calls `loadComputedPreset()` and a null
programArray is the normal case. With one memory map for both standards (below)
there is no per-standard branch left at all.

Not yet blob-free: `presetDeinterlacerSection` and `presetMdSection` are still
PROGMEM arrays, moved into `loadStaticSections()` rather than replaced. No preset
writes segment 2 at all, so they were never table data.

## A writer inside a branch is not an owner either

`25e2d36` established that a field written only by `setResetParameters()` or one
of the two bypass switches has **no owner on the scaling path**, because those
functions put the chip where a scaling preset has to bring it back from.
That is not the whole rule.

`doPostPresetLoadSteps()` unquestionably runs on the scaling path, and
attribution is per FUNCTION — so a field it writes only inside
`if (rto->outModeHdBypass)`, `if (rto->inputIsYpBpR)` or
`if (presetID == 0x06 || presetID == 0x16)` reads as owned and is not. **22
fields were in exactly that state on 2026-08-15**, and `--gap` reported zero the
whole time. `OUT_SYNC_SEL` is the sharpest: its one write in that function is
inside the HD-bypass branch, so scaling never selected the sync output at all,
and the symptom is CLAUDE.md's "no HDMI with every register perfect".

The full list, with who the apparent owner was:

| fields | apparent owner | branch that never runs here |
|---|---|---|
| `OUT_SYNC_SEL` | `doPostPresetLoadSteps` | `if (rto->outModeHdBypass)` |
| `SP_HS_POL_ATO`, `SP_VS_POL_ATO` | `doPostPresetLoadSteps` | `if (syncTypeCsync == false)` — and it writes 1 against the tables' 0 |
| `IF_HS_TAP11_BYPS`, `IF_HS_Y_PDELAY` | `doPostPresetLoadSteps` | `if (inputIsYpBpR)` / standard 3-9 |
| `IF_HS_DEC_FACTOR`, `IF_HB_ST`, `IF_HB_SP`, `IF_HBIN_SP` | `doPostPresetLoadSteps` | `if (presetID == 0x06 \|\| 0x16)` |
| `PLLAD_FS`, `PLLAD_ICP` | `doPostPresetLoadSteps` | standard-8 branch |
| `VDS_Y_DELAY` | `doPostPresetLoadSteps` | YPbPr and standards 3-9 |
| `VDS_W_LEV_BYPS`, `VDS_WLEV_GAIN` | `enableScanlines` / `disableScanlines` | only run when the user toggles scanlines |
| `RFF_ADR_ADD_2`, `RFF_REQ_SEL`, `RFF_FETCH_NUM`, `WFF_FF_STA_INV` | the deinterlacer's enable/disable pair | never runs on a progressive source |
| `OSD_COMMAND_FINISH` | `osd.h`, `OSDManager.h` | nothing draws the OSD until the user asks |
| `VDS_VSYN_SIZE1`/`2` | `doPostPresetLoadSteps` | written once, and the DEFERRED raster solve never re-enters that function |

`VDS_VSYN_SIZE1`/`2` is the odd one and the most instructive: nothing about it is
conditional. `Geometry::solveRaster()` refuses while the source field rate is still
settling, `runSyncWatcher()` retries it later, and the retry re-solves the
raster, the clock and the windows **without going back through
`doPostPresetLoadSteps()`**. So `VDS_VSYNC_RST` moved and its two derived
registers did not. It is `solveRaster()`'s now, beside the register it is derived
from — one quantity, one owner, the same rule `Tv5725::SourceMeasurement` carries for
`PLLAD_MD` / `IF_HSYNC_RST` / `SP_RT_HS_SP`.

### Automating this was TRIED and does not work

The obvious check is brace depth: flag any field whose every attributed write
sits deeper than the function body. Implemented and measured on 2026-08-15, it
reports **146 fields**, of which about fifteen matter. The reason is structural
rather than fixable — `doPostPresetLoadSteps()` wraps most of its body in
`if (!rto->isCustomPreset)` and then again in `if (videoStandardInput == 1 || == 2)`,
so `IF_HS_SEL_LPF` and `IF_HS_PSHIFT_BYPS` are nested two deep and are
nonetheless written for every mode that reaches them. Narrowing to fields the ten
scaling tables agree on still leaves 91. Reverted rather than shipped: a review
list with a 90% false-positive rate is not a check, and the next session would
have to re-derive that by hand anyway.

**So the check that works is the bench diff.** Dump a step-4 unit and a working
`dev` unit against the same source and `snapdiff.py` the two, subtracting the
divider-derived differences first (a `PLLAD_MD` of 2548 against 2553 moves
`IF_HSYNC_RST`, `SP_RT_HS_SP`, the capture window, `VDS_HSCALE` and the raster by
a unit or two, and none of those is a missing owner). That is how all 22 were
found, and it is the procedure to repeat rather than a tool to build.

## Ordering, which is not optional

`applyPresets()` establishes the sequence and anything replacing it must keep it:

1. the raster registers — totals before sync positions
2. the display clock for that raster (`externalClockGenResetClock`)
3. the windows, solved against the raster
4. the rate steer (`externalClockGenSyncInOutRate`) **last**

Step 4 measures `getOutputFrameRate()` off whatever raster is programmed at the
time. Running it early corrects the new clock against the OLD raster: on
2026-08-12 that set 129.6 MHz, read 79.7 Hz from the still-1445 raster, scaled the
clock to 81.3 MHz, and then the 2301 raster landed on it as a 31 Hz frame. Black
screen, and it looked like the raster model was wrong.

## The five unknowns, mostly resolved 2026-08-13

The header work of 2026-08-13 (`940774a`, `2c5af95`) named three of the five, and
grouping the twelve tables by value settled the other two without a name.

**`s4_33` and `s4_36` ARE NOT REGISTERS.** They are the top bytes of two 21-bit
address fields, and reading them as registers in their own right is the error to
avoid.

```
UReg<0x04, 0x31, 0, 21>  PB_CAP_BUF_STA_ADDR_A     21 bits: s4_31, s4_32, s4_33
UReg<0x04, 0x34, 0, 21>  PB_CAP_BUF_STA_ADDR_B     21 bits: s4_34, s4_35, s4_36
```

They are the **top bytes of two 21-bit SDRAM capture buffer start addresses**.
`Tv5725.h` declares only the low address of a multi-byte field, so every high
byte looks undocumented — the exact trap already recorded in this project's own
notes, and I walked into it again in the same session that wrote them down. **A
gap in the register map is a continuation byte until proven otherwise.**

Read as whole fields, the twelve tables say something much simpler than a
per-register rule. A and B are IDENTICAL in every table, and the address is a
property of the video standard:

```
PAL  x6    PB_CAP_BUF_STA_ADDR_A = B = 1,048,576   0x100000   1 MiB
NTSC x6    PB_CAP_BUF_STA_ADDR_A = B =   786,432   0x0C0000   768 KiB
```

So the bring-up needs `PAL ? 0x100000 : 0x0C0000` written to both, which is one
memory-layout decision rather than two mystery bytes. The firmware writes these
nowhere; only preset tables do. Live unit, PAL: A and B both `0x100000`, with
`_C` at 0 and `_D` at 3 — so only A and B are in use.

Why PAL starts higher is not established. It is not the frame-size ratio
(625/525 = 1.19 against 4/3), so it looks like a coarse split of the memory map
rather than a computed buffer size.

### One map for both standards, which is the simplification to take

Nothing in the firmware writes an address register, so the preset tables are the
only authority and the map is ours to choose. Keeping one map for both standards
is not merely harmless: it gives NTSC **more** room than it has now.

The whole SDRAM layout, read off the live unit:

```
0x000000          0   RFF_WFF_STA_ADDR_A     deinterlacer field store starts
0x000001          1   RFF_WFF_STA_ADDR_B
0x052000    335,872   WFF_SAFE_GUARD_A       its top — guard ENABLED, s4_42 b3 = 1
                      ~712 KiB unused
0x100000  1,048,576   PB_CAP_BUF_STA_ADDR_A  capture buffer starts (PAL)
0x157000  1,404,928   CAP_SAFE_GUARD_A       guard DISABLED, s4_21 b5 = 0 — inert
0x1FFFFF  2,097,151   CAP_SAFE_GUARD_B       top of the 21-bit space
```

Three facts make a single address safe:

- **Only buffer A is live.** `PB_DB_FIELD_EN`, `PB_DB_BUFFER_EN`,
  `PB_2FRAME_EXCHG`, `PB_DOUBLE_REFRESH_EN` and `CAP_DOUBLE_BUFFER` all read
  **0**, so playback is single-buffered. `_B` merely mirrors `_A` in every table;
  `_C` and `_D` hold 0 and 3, which are junk nothing reads. That is also why A
  and B being equal in all twelve tables is not a coincidence.
- **The only thing below the capture buffer is the field store**, and its guard
  caps it at 335,872. Both candidate starts — NTSC's 786,432 and PAL's 1,048,576
  — clear that by more than a factor of two.
- **The per-standard bound that would go stale is already inert.**
  `CAP_SAFE_GUARD_A` differs by table, but `CAP_SAFE_GUARD_EN` reads 0, so the
  capture guard is off. `WFF_SAFE_GUARD` is the one that IS enabled, and its
  bound sits below every capture start.

**So the bring-up writes `PB_CAP_BUF_STA_ADDR_A` = `_B` = `0x100000` for
everything.** PAL's value, which is what this bench already runs, so the tested
path is unchanged. NTSC moves from ~324 KiB of allocated capture space to the
~1 MiB above `0x100000`, and the direction of that error is the safe one.

**Untestable here**: the bench is a 50 Hz RiscPC, so nothing on it exercises the
NTSC address. The argument above is structural, not measured.

That removes the last non-constant, and the bring-up block becomes one static
table with no per-standard branch at all.

**`s4_04` and `s4_23` ARE genuine standalone registers, and they are NOT
field-rate dependent** — `pal_768x576` sits with the NTSC group in both, so no
rule fits and freezing them is the right call:

```
s4_04  0x30 x8, 0x32 x4    MEM_FK_RD_DLY / MEM_RD_LAT_PIP, SDRAM read-latch trim
s4_23  0x08 x6, 0x1f x5, 0x0d x1   CAP_FF_STATUS
```

`s4_23` deserves a note of its own: `CAP_FF_STATUS` is the capture FIFO's *status*
readout, valid only when `cap_cntrl_[17]` is set. A preset writing 0x08/0x1f/0x0d
into it is writing a status register, so the bring-up should simply **not write
it**. This does not contradict "no read-only register is written" — that claim is
about the 16 formally RO registers, all of which are `s0_00..s0_2e`. `s4_23` is a
status register outside that set, and the preset ranges do cover it.

So the count moves, and every one of the twenty is now accounted for: nothing
needs computing, `PB_CAP_BUF_STA_ADDR_A`/`_B` take one address for both
standards, `s4_23` and `s1_14`/`s1_16` should not be written at all, and the rest
freeze.

## Open questions

- **Why the PAL capture buffer started higher than the NTSC one.** `0x100000`
  against `0x0C0000` is not the frame-size ratio, so it reads as a coarse split
  of the memory map. Moot once one map is used for both, but unexplained.
- **~712 KiB of the address space is unused**, between the field store's guard at
  `0x052000` and the capture buffer at `0x100000`. Nothing needs it today; worth
  knowing it is there before anyone concludes memory is tight.
- **209 "identical" is measured across the twelve scaling tables only.** The two
  `ofw_*` tables are excluded; they differ in ways nobody has explained
  (`IF_LINE_ST` 0x18, and they are the only tables that *enable* HBOUT/VBOUT).
- The bring-up block is only proven for the modes we drive. Freezing 18 registers
  at `pal_1920x1080`'s values is safe for 1080p50 and untested elsewhere.
