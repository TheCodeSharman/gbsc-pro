# RISC PC → GBSC Pro: ADFFS game modes

**Status: diagnosed, not fixed.** With the `RetroScale` monitor file loaded,
games under ADFFS arrive on a VGA-rate raster instead of the mode file's TV-rate
320x256. With stock AKF50 loaded they come through correctly, so the monitor file
is what decides it. Which entry produces the 422-line raster is not confirmed.

Companion to [riscpc-no-sync.md](riscpc-no-sync.md), which covers the desktop
no-sync fault. That one is fixed; this is the next problem along.

## The symptom

Zarch runs and is playable, but the picture sits small in a black field rather
than filling the screen the way a mode-file mode does. The TV reports the
scaler's output as 1920×1080 @ 75 Hz.

## What is established

Every claim here is a measurement, not an inference.

### Mode-file modes measure as predicted, and Zarch does not

Four modes measured with `tools/gbsc-pro-hwtest/mode_watch.py`, predicted with
`tools/gbsc-pro-hwtest/mdf_modes.py` from the `RetroScale` MDF — **hand-authored,
not an Acorn file**, so it predicts this machine and nothing more general:

| request | MDF predicts | measured | polarity | scaler preset |
|---|---|---|---|---|
| desktop 1280×1024 | 1062 | 1061 | H+ V+ ✓ | 0x22 (bypass) |
| a ×480 @60 mode | 525 | 524 | H− V− ✓ | 0x05 |
| MODE 13 (MakeModes test) | 324 | 323 | H+ V+ ✓ | 0x15 |
| MODE 141 (shadow MODE 13) | 324 | 323 | H+ V+ ✓ | 0x15 |
| **Zarch under ADFFS** | — | **422** | **H+ V−** | 0x05 |

Every mode-file mode lands within one line of prediction, with correct sync
polarity. Zarch matches none of the four. All predictions are from `RetroScale`,
which is the file that was loaded for every row.

**`VTOTAL` is the number to compare.** It counts lines, so it is independent of
anyone's clock. `HTOTAL` is not comparable — see the no-sync doc.

### Zarch asks for MODE 13, and MODE 13 works

The binary sets its mode with two adjacent `VDU 22` sequences: **MODE 141**, then
**MODE 13** as a fallback. 141 is 13 + 128, the shadow-bank convention — Zarch
wants double buffering.

It does not program VIDC to set its mode. There is one VIDC base constant and
one IOC base constant in the binary, single occurrences each, so it touches that
hardware for something else.

Both MODE 13 and MODE 141 measure 323 lines, H+ V+, preset 0x15 — the mode file's
320×256, displaying correctly. **Shadow mode is transparent**, so the divergence
is not there.

### The trigger is `ADFRemapVideoMemory`

(*Trigger*, not author — which of ADFFS or RISC OS actually chooses the raster is
open. See the next section.)

The `!Run` for this Zarch runs, on RISC OS ≥ 3.5:

```
ADFRemapVideoMemory 13 160
```

MODE 13, 160K — two 80K banks for 320×256 at 8bpp, i.e. the double buffering
MODE 141 asked for. It relocates video memory to `&2000000` to imitate RISC OS
3.1's layout.

**Run that command alone from the desktop — no game, no JIT — and it reproduces
Zarch's mode exactly: VTOTAL 422, H+ V−, preset 0x05.**

That is the whole reproduction. Everything downstream — the game, the JIT,
`ADFForceVSync`, the patch database — is irrelevant to the mode. Restore with
a bare `ADFRemapVideoMemory`.

This is worth using: it turns a multi-minute cycle with cleanup hazards into a
few seconds, and it cannot leave the machine wedged mid-game.

## The monitor file decides the raster, not ADFFS

**Measured 2026-08-01.** With `ADFRemapVideoMemory 13 160` applied,
`OS_ReadModeVariable` reports the mode as **320 × 256**. So RISC OS believes it
is in the right mode, at the right resolution — but the wire carries something
else entirely:

| | mode file's 320×256 | what is emitted |
|---|---|---|
| resolution | 320×256 | 320×256 (same) |
| VTOTAL | 324 | **422** |
| line rate | 16.25 kHz | **~31.6 kHz** |
| field rate | 50.15 Hz | **~75 Hz** |
| sync | H+ V+ | **H+ V−** |

Everything differs except the resolution, at roughly double the line rate.

**The monitor file decides it.** With `RetroScale` loaded,
`ADFRemapVideoMemory 13 160` gives VTOTAL 422. With stock AKF50 loaded, the same
command gives the mode file's 320x256. Nothing changes but the monitor file, so
RISC OS is selecting this raster the way it is designed to — ADFFS is not
synthesising one.

Two readings of the source say the same thing. ADFFS *"does not program VIDC to
set its mode. There is one VIDC base constant and one IOC base constant in the
binary, single occurrences each"*. And `ADFRemapVideoMemory` is on its face a
memory-layout operation — it relocates video memory to `&2000000` to imitate
RISC OS 3.1 — not a timing one.

**Why `RetroScale` and not AKF50: the file holds two populations.** It is stock
AKF50, plus eight higher-resolution modes carrying 31.47 kHz VGA heritage, plus
one invented 320x256 — see `tools/gbsc-pro-hwtest/mdf_modes.py`. AKF50 alone
offers no VGA-rate answer to a 320x256 request; `RetroScale` does, and that is
what gets picked. The same conclusion is reached from the other direction under
*Killed: the "ADFFS picked a bigger mode" hypothesis* below: the resolution
really does stay 320x256, so what comes from the VGA population is the
**timings**, not a different mode.

The line rate follows from the output side, independent of any mode-file
assumption: the scaler's `autoBestHtotal` tracks output frame rate to input, the
TV reports the output at 75 Hz, and 422 lines at 75 Hz is ~31.6 kHz.

**Still open: which entry yields exactly 422 lines.** No mode with VTOTAL
418-426 exists in the 13 stock Acorn monitor files or in MakeModes' `GenModes`,
so 422 is not read off a shelf — but `RetroScale` has never been scanned, and it
is the file that was loaded. Run `mdf_modes.py` over it looking for VTOTAL
418-426 with H+ V-. A hit settles it; a miss means the raster is computed.
Either way 31.47 kHz / 422 = 74.6 Hz puts the line rate in the VGA population.

### How the substitution actually happens

Read from `adffs/commands/remapvideomemory`. **ADFFS does not choose a different
mode.** It parses the mode number off the command line and asks for it plainly:

```
MOV R0, #0          ; OS_ScreenMode reason 0
MOV R1, R9          ; R9 = the parsed mode number, i.e. 13
SWI XOS_ScreenMode
```

`R1` is a mode *number*, not a pointer to a mode selector block — so no timings,
resolution or frame rate are specified. It is the same request `VDU 22,13` makes.

The substitution is in the **ordering**, and the source comments say so:

1. set mode 13 — before claiming anything
2. `Claim_DataAbort_Vector`
3. resize DA 2 to the requested size
4. **set mode 13 again**, under the comment *"now claim the Abort vector and
   change mode again so we can set IOMD vars"*

**What that second mode change is for is not established.** The tempting
reading — that ADFFS intercepts RISC OS's own VIDC programming and rewrites the
timings in flight — does not hold up:

- RISC OS on a RiscPC programs **VIDC20**, legitimately, in supervisor mode. For
  ADFFS to intercept those writes it would have to be trapping the OS itself,
  which was assumed, not verified.
- The source comment says *"so we can set **IOMD** vars"*. IOMD handles screen
  DMA and addressing — exactly what this command is documented to do, relocating
  video memory to `&2000000`. That reading needs no timing rewrite at all.

`aborts/vidc1_abort` exists to trap **games** writing to VIDC1's address space,
which does not exist on a RiscPC, so those writes fault and get translated. That
is a different thing from intercepting the OS.

### What the remap changes

What is measured, and not in doubt:

- the mode number requested is 13, by number, with no mode selector
- `OS_ReadModeVariable` afterwards reports 320×256
- a plain `MODE 13` gives 323 lines, H+ V+
- `ADFRemapVideoMemory 13 160` gives 422 lines, H+ V−

Same mode number, same reported resolution, different raster. What differs
between the two paths is the **DA 2 resize** and the **video memory
relocation** — not, as far as the source shows, anything about timings.

Two tests would split it, neither yet run:

1. **With ADFFS loaded but no remap applied, set `MODE 13` from BASIC.** 323 means
   the remap causes it; 422 means merely having ADFFS resident does.
2. **`ADFRemapVideoMemory WIMP`** — the single-parameter form, documented as *do
   not change mode*. If the raster still changes, the memory relocation alone is
   responsible and the mode set is irrelevant.

Both are unrun. They would say whether the remap itself, or merely having ADFFS
resident, is what puts the request onto the VGA population.

### Killed: the "ADFFS picked a bigger mode" hypothesis

Kept because the reasoning was sound on the evidence available, and to stop it
being re-derived.

The scaler reports **H+ V−**, which is `sync_pol:2` in the MDF. That rules out
the ×480 letterbox family (all `sync_pol:3`, H− V−) and leaves:

| mode | VTOTAL | line rate |
|---|---|---|
| 240×352 | 449 | 31.47 kHz |
| **384×288** | **449** | **31.47 kHz** |
| 480×352 | 449 | 31.47 kHz |
| 640×352 | 364 | 21.85 kHz |
| 896×352 | 364 | 21.85 kHz |

And the arithmetic closes on the 449 group:

```
31.47 kHz ÷ 422 lines = 74.6 Hz     ← the 75 Hz the TV reports
```

So: ADFFS selects a **31.47 kHz VGA-rate** mode — plausibly 384×288, the smallest
entry that contains a 320×256 game — and trims the vertical timing through its
VIDC shadow from 449 lines to 422, taking the refresh from 70 Hz to 75 Hz.

**Why those modes are VGA-rate matters.** This MDF was derived from an SVGA/XVGA
base set, so its 352- and 288-line families came in at 31.47 kHz from that
heritage. The TV-rate modes — 320×256, 640×256, 1056×256 — were hand-tuned for
the scaler afterwards. The file holds two populations with different assumptions,
and ADFFS is choosing from the wrong one.

**That paragraph outlives the hypothesis it was written for**, and is the
explanation above: what was killed is a *bigger mode* being selected, not a
VGA-rate timing being selected.

**To confirm or kill it**, read what RISC OS thinks it is in, straight after
running the command:

```basic
SYS &35,-1,11 TO ,,X%
SYS &35,-1,12 TO ,,Y%
PRINT X%+1,Y%+1
```

`&35` is `OS_ReadModeVariable`; using the number avoids SWI-name lookup. 384×288
confirms the hypothesis. 320×256 kills it, and means the resolution is right
while only the timings are overridden.

## Dead ends, so they are not re-derived

- **LCDGameModes and AutoVIDC.** Both ship with ADFFS, neither loads here. The
  loader gates them on `ADFFS$OSVersion < &35000` — before RISC OS 3.5, i.e.
  pre-RiscPC. LCDGameModes says so itself: "only suitable for RISC OS 3.00-3.49".
  AutoVIDC is VIDC1-only hardware. From 3.5 the mode file is the mechanism.
- **`GameModes`** (`!System/310/Modules/`) is unrelated — a 1993 Acorn module
  that answers Service_ModeExtension for **only modes 48 and 49**, on non-TV
  monitor types. Nothing to do with MODE 13.
- **The mode-detection short-circuit.** `getVideoMode()` returns early in mode
  15, but returns 0 when sync drops — and a mode change drops sync, which is what
  triggers re-detection. Not a blocker.
- **`preferScalingRgbhv`** is already `1` on this unit.
- **MonitorType / RISC OS.** MODE 13 from BASIC gives the correct mode, so
  neither is at fault.
- **Shadow mode.** MODE 141 measures identically to MODE 13.
- **`ADFForceVSync 4`** is frame pacing — it forces 25 FPS by delaying buffer
  swaps. It cannot change raster timing.
- **Preset preference.** `Output1080P` has no source-matching branch at all;
  `Output960P` takes the 240p preset unconditionally. Switching to 960P makes it
  *worse*: that preset cannot lock this source and the picture comes out
  horizontally doubled — each captured line spans two source lines. Neither stock
  preset suits a 422-line source.

## Making the scaler fill the screen

A better line of attack than fighting ADFFS: tune the scaler to whatever raster
ADFFS consistently emits. It needs no mode file, no ADFFS change, and not even an
explanation of how ADFFS builds the raster — only that it is stable, and 422 has
been unwavering across every reading, two preset changes and many mode
transitions.

### Registers that work, proven on hardware

Each confirmed by a deliberately large change producing a large, correct
movement on screen.

| register | address | effect |
|---|---|---|
| `IF_HB_SP2` | seg 1, `0x1a` | input capture window **left edge** |
| `IF_HB_ST2` | seg 1, `0x18` | input capture window **right edge** |
| `VDS_HSCALE` | seg 3, `0x16` | stretches the captured region, 256–1023 |
| `VDS_HSCALE_BYPS` | seg 3, `0x00` bit 4 | **1 = horizontal upscaler off** |

Stock values for this mode: capture `142 .. 1152`, `HSCALE 1023`, `BYPS 1`.

### The two coordinate spaces, and the bounds that go with them

Neither the capture window nor `HSCALE` can be set without knowing the space it
lives in. Getting this wrong destroys the picture rather than degrading it.

**Input side.** The capture window is measured in units of `IF_HSYNC_RST`
(seg 1, `0x0e`), which is `PLLAD_MD / 2` — 1279 units for the 422-line mode's
2558-sample line. An edge at or beyond `IF_HSYNC_RST` is out of bounds: blanking
never starts, the line buffer wraps, and every output line is built from
fragments of the wrong part of the source. The firmware states the bound
directly, in `shiftHorizontalLeftIF()`:

```c
if (IF_HB_ST2 < IF_HSYNC_RST) { write(IF_HB_ST2); } else { write(IF_HB_ST2 - IF_HSYNC_RST); }
```

The earlier "capture `400 .. 1280` with `HSCALE 893` produced a full-width
picture" claim is **withdrawn**: 1280 is past the 1279-unit end of the line. Set
that way on 2026-08-01 it produced heavy horizontal drag and periodic banding,
not a full-width image.

**Output side.** The display active window is `VDS_DIS_HB_SP .. VDS_DIS_HB_ST`
(seg 3, `0x11` and `0x10`), reading `348 .. 1356` — **1008 pixels of a 1601-pixel
output line** (`VDS_HSYNC_RST`, seg 3, `0x01`). The panel stretches that active
region to fill itself, so 1008 is the budget everything must fit inside.

That budget is what makes `HSCALE` useless at the stock capture width. With 1010
captured units going to a 1008-pixel window, the picture **already fills the
output window at 1:1**. `HSCALE` can only magnify, so any value below 1023
overflows and wraps. Measured: `HSCALE 540` wrapped the picture about twice.

**So `VDS_HSCALE_BYPS` being set is not the reason the picture is narrow.** The
upscaler is bypassed because at this capture width there is nothing for it to do.

### The measured firmware defect

`scaleHorizontal()` nominally moves `VDS_HSCALE` by 2 counts per call. Measured
over 100 invocations of the UI's own command (`/sc?z`):

```
HSCALE 1023 -> 1007      16 counts from 100 calls
```

Its internal guards absorb about **92%** of its own calls. Reaching a useful
1.7–2.4× needs `HSCALE` near 430–590, i.e. several hundred counts — thousands of
button presses. The control is not broken, it is unusably slow, which is exactly
why it felt useless.

**And the input capture window has no UI control at all.** `IF_HB_SP2`/`ST2` are
not exposed by any command; the geometry controls only reach output-side
`VDS_HSCALE`. That is why output scaling alone could never remove the borders —
it magnified the captured black along with the picture.

### Open: artifacts, and the likely real lever

Driving the picture edge to edge introduced **horizontal dashes and smearing**
through text and graphics, and they got *worse the wider the capture window
went*. That points away from the capture registers.

The scaler samples **2558 times per input line** (`STATUS_SYNC_PROC_HTOTAL`) for
a source with far fewer real pixels. Widening the capture pulls in more of that
oversampled region, where sampling is not aligned to the source's pixel grid.

So the lever is probably **`PLLAD_MD`** (seg 5, `0x12`) — the sample-clock
divider setting samples per line. Match it to the source's real pixel count and
the sampling aligns; the capture window then becomes a small trim rather than a
fight. This is what auto-best-htotal does for modes gbs-control recognises, and
it is evidently not doing it correctly here. **Untested.**

## Tuning, measured 2026-08-01

The picture is still not correctly framed. What is established is why.

### Photographs lag the register writes

**A measurement discipline, not a fact about the chip.** Register changes can be
made faster than photographs can be taken and read, so a photo arrives showing
the state *before* the change and gets attributed to it. That silently inverts
cause and effect.

**"The scaler carries state, and restoring registers does not recover it" is
exactly that error** and does not hold: it rested on seeing corruption after
reverting to values that had been clean earlier, where the photo almost certainly
still showed the previous, sheared configuration. Nothing here shows the scaler
failing to recover from a bad configuration. Testing it properly means: change,
*confirm the photo was taken after the change*, then compare.

`/sc?q` — `resetDigital()` + `ResetSDRAM()` + `togglePhaseAdjustUnits()` — is a
reset short of a power cycle. Worth using between experiments as cheap hygiene,
but not because the above was demonstrated.

### There are two distinct faults, not one

Separating them matters, because a single explanation was being stretched over
both.

| | signature | implicates |
|---|---|---|
| **Duplication** | content appears twice, same offset **every frame** | addressing / line length — deterministic |
| **Interference** | streaking that **changes frame to frame** | state, configuration extremes, marginal clocks |

A wrong register value produces identical corruption every frame, so the
time-varying component cannot be an addressing error. It appears when settings
are pushed toward extremes and is strongly source-mode dependent.

**And it is mode dependent.** AKF50 480×352 (31.47 kHz, 70 Hz) provoked heavy
artefacts; switching the source to 640×256 (15.62 kHz, 50 Hz) cleared them with
no scaler change at all. The 15 kHz modes are gbs-control's native domain.

### The detector cannot work, and here is the proof

Two stock AKF50 modes:

| mode | clk | htotal | VTOTAL | sync | **active px** |
|---|---|---|---|---|---|
| 640×256 | 16.000 MHz | 1024 | 312 | H+ V+ | `222 .. 862` |
| 768×288 | 16.000 MHz | 1024 | 312 | H+ V+ | `158 .. 926` |

**Identical rasters. Different active regions.** Every quantity gbs-control
measures — `VTOTAL`, `HTOTAL`, polarity, field rate — is the same, yet the correct
capture window differs by 64 px per side. No detector keyed on those measurements
can get both right. This settles the open question this file has been asking: the
active region **cannot** be derived from raster measurements. It must come from
the mode file, or from finding where video actually starts and stops.

Observed directly: for a `VTOTAL 422` source and a `VTOTAL 448` source, gbs-control
produced byte-identical configuration — preset `0x05`, `PLLAD_MD 2558`, capture
`142..1152`. It is not detecting; it is applying a fixed preset.

### What the mode file does and does not predict

- **`VTOTAL`: reliable.** Now five modes, all within one line.
- **Active *width*: reliable.** 480×352 predicted 80.0% of the line; setting that
  width matched. 640×256 predicts 62.5%.
- **Active *position*: not reliable.** Every window computed from mode-file pixel
  positions landed wrong, because the IF's coordinate origin is not the sync edge.

`RetroScale`'s 640×256 entry is **byte-identical to stock AKF50/AKF52**, so the
mode file is not the source of error. All thirteen stock Acorn MDFs are available
locally under `rpcemu/installs/*/hostfs/!Boot/Resources/Configure/Monitors/Acorn/`.

### Claims that do not survive measurement

Recorded so they are not re-derived:

- **"Capture `400 .. 1280` with `HSCALE 893` gives a full-width picture."** 1280
  exceeds `IF_HSYNC_RST` (1279); it wraps and corrupts.
- **"`PLLAD_MD` 2560 is 4 × 640 source pixels."** That rests on the TV's reported
  75 Hz being the *source* field rate. It is not — it is the 1080p preset's fixed
  output rate. The 1024P preset does track the source, 70 Hz for a 70 Hz source,
  which is why the two observations differ.
- **"Smearing is the output upscaler."** `VDS_TAP6_BYPS` is 0, so the filter is
  on. The artefacts are corruption, not filtering.

### The pixel-perfect target

`1280×1024 = (320×4) × (256×4)`. The classic RISC OS mode families land on it
exactly:

| mode | scale | fills 1280×1024 |
|---|---|---|
| 320×256 (MODE 13, the game modes) | ×4 / ×4 | exactly |
| 640×256 | ×2 / ×4 | exactly |
| 640×512 | ×2 / ×2 | exactly |
| 480×352 | 2.67 / 2.91 | **no** — the awkward one |

The TV accepts 1280×1024 @ 70 Hz and fills its 16:9 panel from it, so the TV does
the aspect stretch and the scaler can stay on integer ratios. That resolves the
"pixel-perfect versus full-screen" conflict: let the panel do the non-integer part.

**Blocked on:** `PLLAD_MD 2048` — which would make `IF_HSYNC_RST` 1024, i.e. one
capture unit per source pixel, making the mode file's `222..862` usable verbatim —
**does not lock** under the 1024P preset, where `PLLAD_KS` reads 2. It locked
under the 1080p preset. Moving `PLLAD_KS` is the obvious next step and is exactly
the kind of extreme excursion that triggers the corruption, so it wants a reset
either side of it.

### Method notes

- **Reset the scaler between experiments** (`/sc?q`), for the reason above.
- **The mouse test beats photographs.** "Can the pointer go off the edge" locates
  the active region far more precisely than reading edges off a photo — several
  wrong turns here came from photo estimates being 25% out.
- **Change one edge at a time.** Narrowing the capture window by 276 units in one
  step produced diagonal shear; the line lengths must stay consistent.
- **Verify `VTOTAL` before every write.** Enforced in code, and it
  caught the source moving mid-experiment at least once.

### The plan: tune several modes by hand, then write the detector

The real goal is not a set of manual controls, it is **better auto-detection** —
gbs-control already tries to work out capture window, scaling and sample clock,
and it gets this class of mode wrong. Manual controls are the means, not the end.

So the order matters: **tune a handful of modes by hand first**, recording what
the correct answer turns out to be for each, and only then design the detector
against real data. That converts the manual work from one-off fiddling into a
dataset, and it means the auto code is written against measured input→output
pairs instead of guesses about what it should be looking at.

For each mode, record:

| capture | what to record |
|---|---|
| what the source is | `VTOTAL`, `STATUS_SYNC_PROC_HTOTAL`, sync polarity, field rate if known |
| what the answer is | `IF_HB_SP2`/`ST2`, `VDS_HSCALE`, `VDS_HSCALE_BYPS`, `PLLAD_MD`, the vertical equivalents |
| how it was judged | full-screen with no wrap, no smearing, correct aspect |

Worth covering at least: Zarch's 422-line raster, the mode file's 320×256 (323
lines, which already displays correctly and so is a useful *control* — whatever
the detector does must not break it), and a couple of other game modes once
they've been looked at.

The interesting question the dataset should answer is **what the detector can
key off**. `VTOTAL` is reliable and clock-independent. Sync polarity is
reliable. Whether the active region can be found automatically — by looking for
where video actually starts and stops within the raster — is the crux, and is
exactly what gbs-control appears to get wrong here.

### The RISC PC is a mode generator, and that is worth exploiting

Most scaler debugging is done against whatever source happens to be on the desk.
Here the source is **programmable**: `!MakeModes` will define essentially any
timing set, and RISC OS will emit it. That makes the RISC PC a torture-test rig
for auto-detection rather than merely the thing being fixed.

The pieces already close the loop:

```
!MakeModes  →  define an arbitrary mode
mdf_modes.py →  predict VTOTAL, line rate, field rate, polarity
mode_watch.py → record what the scaler actually did with it
```

Predicted-versus-measured has already been validated on four modes, each landing
within one line, so the prediction half is trustworthy.

What that enables, which guessing cannot:

- **Sweep one variable at a time.** Hold resolution fixed and walk the line rate;
  hold the line rate and walk the blanking ratio; flip sync polarity alone. Each
  sweep says exactly where the detector's behaviour changes, and whether it
  changes at a sensible boundary.
- **Build the edge cases deliberately.** Modes just either side of the
  `VTOTAL ≤ 535` scaling threshold; very wide blanking; unusually short frames.
  These are the cases that break auto-detection in the field and are otherwise
  almost impossible to obtain.
- **Regression-test the detector.** A mode set with known-correct answers is a
  fixture. `tools/gbsc-pro-hwtest/` could cycle through them and assert the
  detector picks sane capture windows and scale factors — the same discipline as
  the sync tests, applied to geometry.
- **Keep the control honest.** The mode file's 320×256 already displays
  correctly. Any detector change must leave it that way.

Worth doing before writing detector code, not after: the sweeps define what the
detector can actually key off.

### Persisting a result

`/uc?4` writes the live registers as a custom preset for the current video mode
and switches the unit to `OutputCustomized`, so a tuned result would reload
whenever the game mode appears. That is the destructive path — it overwrites the
stored preset and the preference survives reboot. Take a flash backup first.
Tuning live needs no backup; only saving does.

### Method notes, learned the hard way

- **Verify the input reads 422 before every write.** The machine changes mode
  underneath you and the scaler reloads its preset each time. A tuning session
  once ended up stretching the *desktop* because the mode had moved between
  reading and writing.
- **Nothing persists** until `/uc?4`. A mode change or reset restores stock.
- **Tear down ADFFS before launching a game.** A bare `ADFRemapVideoMemory`
  first, every time. Leftover remap state from a manual test plus the game's own
  remap produced a black screen and then a wedged machine.
- **Ctrl-Shift-F12, not Ctrl-Break**, to exit ADFFS — the former runs its
  teardown, the latter leaves the remap in place.

## Scaler-side notes

- The scaler leaves RGBHV bypass for a scaling preset when `VTOTAL ≤ 535` and
  stable, gated on `preferScalingRgbhv`. The desktop's 1061 correctly stays in
  bypass; 422 correctly scales. **This works.**
- The handover fired while `VTOTAL` still read a transient 97, passing the
  `≤ 535` test on a number that was not the settled mode. It got away with it
  because 422 also qualifies. Worth remembering if a source ever settles above
  the threshold from below.
- The scaler already handles the mode file's 320×256 correctly, with preset
  `0x15`, pillarboxed properly for 5:4 on a 16:9 panel. **Nothing needs fixing
  downstream** — the whole problem is getting ADFFS to ask for that mode.

## Tools

| | |
|---|---|
| `mdf_modes.py` | MDF → VTOTAL, line rate, field rate, polarity. Makes a mode file a prediction. |
| `mode_watch.py` | Records what the scaler sees, logging every mode change with a `printVideoTimings()` dump. |

Known gap: `mode_watch.py`'s console capture comes back empty at some
transitions — the WebSocket appears to drop while the unit reconfigures. The
register half is reliable.

## 2026-08-05 afternoon: the horizontal geometry model, measured

The session that replaced *"the write origin is assumed to be `VDS_HB_SP`"* with
a measurement. Driven from the register panel by hand, one field at a time,
against `TestPat` on the RISC PC; photographs in
[photos/2026-08-05-horizontal-geometry/](../photos/2026-08-05-horizontal-geometry/).

### The model

```
produced   = (IF_HB_ST2 - IF_HB_SP2) x 1024 / VDS_HSCALE
origin     = VDS_HB_SP + 78                    <- measured, not assumed
picture    = [origin, origin + produced - 1]   <- INCLUSIVE, both ends

VDS_DIS_HB_SP  >= origin                       else unwritten memory shows left
VDS_DIS_HB_ST  <= origin + produced - 1        else one stale pixel, or worse
VDS_HB_ST - VDS_HB_SP >= produced + ~20        else the picture shreds
VDS_HS_ST, VDS_HS_SP  must be EVEN             odd = unstable picture
```

> **The `produced` line above is SUPERSEDED.** `capture x 1024 / scale` is 14 px
> long at 1:1 and 40 px short at x3.2, which shows as a band of unwritten memory
> past the picture. Measured over four magnifications per axis on 2026-08-05
> night; see [scaler-geometry-model.md](../scaler-geometry-model.md). The origin and
> the bounds below it still hold.


**The display bounds are inclusive pixel indices, and the `- 1` is real.**
Measured: `VDS_HB_SP` 49 gives origin 127, `produced` is 797, and
`VDS_DIS_HB_ST` = 924 leaves **one non-flashing pixel** on the right that 923
removes. 923 - 127 + 1 = 797 exactly. Non-flashing is the `TestPat` liveness
cue doing its job — that pixel was stale frame-buffer scratch, one past the end
of what the scaler wrote, and at a width of one pixel nothing else would have
identified it.

### Three blanking pairs, three different jobs

Every observation here fits this model:

| pair | seg | job |
|---|---|---|
| `IF_HB_SP2` / `IF_HB_ST2` | 1 | input capture window — which part of the source line is taken |
| `VDS_HB_SP` / `VDS_HB_ST` | 3 | frame-buffer bounds — what memory is valid and gets read |
| `VDS_DIS_HB_SP` / `VDS_DIS_HB_ST` | 3 | display clip — what part of that reaches the output |

### The write origin is fixed, and it is not `VDS_HB_SP`

- **Fixed with respect to the capture window.** The first written pixel stayed at
  output 248 across `IF_HB_SP2` of 284 and 260. Panning the capture moves the
  *content*, not the frame. A tracking origin would have moved it 24 px.
- **But it tracks `VDS_HB_SP` 1:1.** `VDS_HB_SP` −3 moved the picture 3 px left.
- **The offset is 78 px, and it is constant.** Confirmed at `VDS_HB_SP` of 49,
  50, 60 and 170 — a 121 px span. Predicted `VDS_HB_SP` = 49 would put the
  picture's left edge on the panel's left edge; it did, confirmed by eye.
- **It is not referenced to sync start.** `VDS_HS_ST` moved 8 → 10 during the
  session and the offset stayed 78.
- **Mechanism unknown.** Leading explanation is a fixed read/output pipeline
  latency. Untested discriminators: change `VDS_HSCALE` (an output-side delay
  stays 78 px, an input-side one scales), or change `VDS_HS_SP`.

`geometry.py` has been printing this all along as `display vs memory left edge` —
that figure *is* `VDS_DIS_HB_SP - VDS_HB_SP`, so when it reads exactly **78** the
display window starts precisely on the first written pixel. One-glance alignment
check.

### `VDS_HS_ST` and `VDS_HS_SP` must be even

Found by accident: odd values (11, 13) make the picture unstable, even ones (10,
12, 14) are rock solid. Corroborated across **55 archived snapshots**: 110 sync
position values recorded, **not one odd** — while `VDS_HB_ST` (15/55),
`VDS_HB_SP` (6/55) and `VDS_HSYNC_RST` (25/55) in those same files are odd freely.
So the constraint is specific to the sync pair, not VDS timing generally.
Consistent with the output stage moving two pixels per clock. **Nothing in the
tooling rounds these to even**, so a computed odd value would ship and present as
an intermittent tear rather than an obvious fault.

### The source's active width is measurable after all

CLAUDE.md says blanking cannot be auto-detected, because a border is black
*active* video and electrically identical to back porch. That stands for the
chip — but `TestPat` makes it visible to the eye, and the eye can be creeped one
unit at a time. Measured on the bench RISC PC at `PLLAD_MD` 2553:

| | |
|---|---|
| active picture | `IF_HB_SP2` 264 .. `IF_HB_ST2` 1061 = **797 units** |
| panel's left edge | output pixel **127** |

### Reading `TestPat` correctly

- **Magenta and cyan are the same border**, alternating twice a second
  (`VDU 19,0,24,...` flips `(255,0,255)` and `(0,255,255)`). Seeing cyan and
  concluding the magenta border was trimmed is a trap — it was hit this session.
- **Green is the signal.** `PROCframe` draws a 1-pixel green frame on the
  outermost active pixels and does *not* flip. Green outermost = correctly
  trimmed; magenta/cyan outermost = still capturing border; a missing green side
  = that edge is clipped.
- **Anything static is stale frame-buffer scratch**, not live captured video —
  which is how leftover junk beside a trimmed picture tells itself apart. A
  static cyan line was misread as live border before this was understood.

**Photographs 11 and 12 are the proof, and they only work as a pair.** Two
frames of the same scene half a flash apart: the wide top and bottom bands
change magenta → cyan, so they are live captured border and the capture window
is too big. The thin outermost strips are the same pale cyan in both, so they
are scratch outside the capture and the *display window* is too big. Identical
in any single still, opposite fixes. Film it.

### Method lessons, paid for

- **Change one field at a time.** Six registers written at once produced a
  corrupted picture attributable to nothing, and the whole observation was
  discarded (photo 03).
- **Freeze, or the firmware fights you.** The unit rebooted mid-session — the
  freeze is never persisted — and for some time afterwards `applyPresets()` and
  `runAutoBestHTotal()` were live, reverting hand-set capture values and
  latching `HPERIOD_IF` wrong. Symptom was a full green corruption (photo 07)
  after edits that should have been harmless.
- **Photographs cannot measure position.** Comparing absolute edge positions
  between two hand-held photos produced a confident and wrong conclusion that the
  picture had slid; the camera had moved. Only discrete events — garbage
  appearing, a colour changing — survive photographic evidence.

### `HPERIOD_IF` 177 is the 800x600 value

While the firmware was unfrozen, `HPERIOD_IF` latched at a stable **177** where
431 was due, and a full register restore did not clear it. Through the documented
formula, `(HPERIOD+1) x 4 / 27`, 177 is **26.37 us = 37.9 kHz = 800x600** — and
`tv5725-chip.md` records 176 as the healthy value for an 800x600 capture. So the
counter was returning a coherent measurement of the wrong mode, not noise and not
a rail. The picture was clean throughout, so it is cosmetic here — but the
"≥4 distinct values in a settled window" detector scores 177 ±0 as perfectly
healthy, and would miss it.

## 2026-08-05 evening: the vertical axis, and the mode file reconciled

Same method, same session. The vertical axis turned out to have the same shape
as the horizontal one with two different units, and it closes with the chip's
geometry and the RISC OS mode file predicting each other.

### The vertical model

```
produced_v = (IF_VB_ST - IF_VB_SP) x 1024 / VDS_VSCALE
origin_v   = VDS_VB_SP + 26
picture_v  = [origin_v, origin_v + produced_v - 1]

VDS_DIS_VB_SP >= origin_v                    else scratch above the picture
VDS_DIS_VB_ST <= origin_v + produced_v - 1   else scratch below it
VDS_VB_ST < VDS_VSYNC_RST                    else the frame rolls
```

> **The `produced` line above is SUPERSEDED.** `capture x 1024 / scale` is 14 px
> long at 1:1 and 40 px short at x3.2, which shows as a band of unwritten memory
> past the picture. Measured over four magnifications per axis on 2026-08-05
> night; see [scaler-geometry-model.md](../scaler-geometry-model.md). The origin and
> the bounds below it still hold.


Structurally identical to horizontal. The offset is **26 lines** where horizontal
is 78 px, confirmed by `VDS_VB_SP` 16 -> 76 producing exactly the 60-line black
bar the arithmetic predicts.

### `IF_VB` counts half-lines

**The unit is half-lines, not lines** on this source. Measured directly:
`IF_VB_ST` **624** rolls the picture, **623** is stable. 624 = 2 x 312, and 312
is the source's line count.

Two earlier readings of the `IF_VB_ST`/`IF_VB_SP` pair each worked for one state
and broke on the next; the direction was right and the unit was wrong.

> **This holds only while the line doubler is in the path**, which it is for
> every mode on this page. Bypassed, the counter runs at the source's own line
> rate and the wrap is `VTOTAL + 1`. See "What the IF counter counts" in
> [scaler-geometry-model.md](../scaler-geometry-model.md).

Half-line counting is how interlace is expressed — a field is a whole number of
lines *plus a half*, which is what displaces the second field between the
first's scan lines, so a chip with a deinterlacer counts vertical position at
twice line rate. This source is progressive, so the precision is unused; it just
halves the numbers. It does set the trim granularity: one `IF_VB` unit is half a
source line, which at `VSCALE` 660 is 1.55 output lines, where one horizontal
unit is about one pixel.

### Three wrap points, all at a total

| register | wraps at | is |
|---|---|---|
| `IF_HB_ST2` | 1277 | `IF_HSYNC_RST` + 1, and the firmware guards it |
| `IF_VB_ST` | **624** | 2 x the 312-line frame |
| `VDS_VB_ST` | **1125** | `VDS_VSYNC_RST`, the output frame total |

Nothing guards the last two. Crossing one makes the picture *jump* rather than
extend, and it was mistaken for "losing the capture window" before the totals
were known.

### The mode file and the chip predict each other

Stock AKF50, 320x256 (`mdf_modes.py` against
`!Boot.Resources.Configure.Monitors.Acorn.AKF50`):

```
v_timings: 3, 16, 17, 256, 17, 3        h_timings: 36, 30, 44, 320, 44, 38

  sync             3    lines   0 ..   3
  back porch      16    lines   3 ..  19
  top border      17    lines  19 ..  36
  display        256    lines  36 .. 292
  bottom border   17    lines 292 .. 309
  front porch      3    lines 309 .. 312
```

Probing the porch/border edge by eye against `TestPat`'s magenta screen border:

| | measured | AKF50 | difference |
|---|---|---|---|
| top border begins | line 11 | line 19 | **-8** |
| bottom border ends | line 301 | line 309 | **-8** |
| **span** | **290 lines** | **290 lines** | **0** |

**The span is exact and both edges are off by the same 8 lines** — the chip's
vertical counter zero sits 8 lines after the MDF's, which counts from the start
of sync. So the capture window is computable from the mode file:

```
IF_VB_SP = (MDF top-border start  - 8) x 2 = (19  - 8) x 2 = 22   measured 22
IF_VB_ST = (MDF bottom-border end - 8) x 2 = (309 - 8) x 2 = 602  measured 602
```

Both exact. Probing the *display* edges instead of the border edges gives
`IF_VB` 56..568 predicted against 56..569 measured — one half-line out.

The horizontal axis behaves the same way. At 1277 IF units per 512 MDF pixels
(2.4941 units/px):

```
MDF display span 320 px = 798.1 IF units;  measured 797  ->  -1.1 units (-0.45 px)
left  edge:  264 vs 274.4  ->  -10.4 units
right edge: 1061 vs 1072.5 ->  -11.5 units
```

Span exact, constant origin offset of about 11 IF units. Same structure, both
axes.

### The offsets are not sync width

`SP_PRE_COAST` and `SP_POST_COAST` both read **0**, so nothing programmed shifts
the vertical reference. MDF vertical sync is 3 lines against an 8-line offset;
horizontal sync is 36 MDF px = 89.8 IF units against an ~11 unit offset. Neither
matches, and they are not proportional. On this evidence the offsets are
**implicit** — a fixed latency between the chip seeing the sync pulse and its
counter reaching zero — but this is one mode, so "fixed" and "some function of
this mode's sync and porch" are not yet distinguishable.

The discriminator is a mode with very different vertical timings. AKF50's
384x288 (vsync 2, back porch 58, vtotal 449) predicts `IF_VB_SP` = 104 if the
offset is a fixed 8, 106 if it is sync + 5, and nowhere near either if it tracks
the back porch. Not run: a source mode change reloads a preset and moves
`PLLAD_MD`, so it costs the whole tuning.

### Panel calibration

The output raster is larger than what the panel shows, and the visible corner is
measurable:

| | |
|---|---|
| panel left edge | output pixel **127** |
| panel top edge | output line **63** |

Both found by creeping the display window until it met the bezel. Note the
vertical raster is standard — `VDS_VSYNC_RST` 1125 is 1080p's frame total — but
the horizontal one is **not**: the line is 1445, not 1080p's 2200, so VESA
numbers predict the vertical axis and not the horizontal one.

### Vertical headroom may not need padding

Horizontally, `memory window - produced` below about 13 px tears the picture,
because the scaler must finish reading the line before the line period ends.
Vertically there is no equivalent pressure — a frame is orders of magnitude
longer — and the bench bore that out: a settled state at **-1.9 lines** of
vertical headroom showed a clean picture, where the same margin horizontally
would shred. It appears to truncate the last line or two rather than corrupt.

**Treat as tentative.** One state, and the display window was clipping above the
shortfall, which may have masked it.

### The order that matters

Widening the picture before widening the window that holds it is the one move
that reliably wrecks a good state. Measured: at `VDS_VB` 37..845 the window is
808 lines against 795.9 produced, and every lower `VSCALE` from there is
negative — 600 gives -67, 500 gives -243. **Window up first, then scale down.**
This is the vertical case of the constraint `write_origin.py`'s `ordered_writes`
now enforces for the horizontal pair; nothing enforces it here.

### Engaging the scaler shifts the picture one pixel right

`VDS_HSCALE_BYPS` is not a no-op even at `HSCALE` 1023. Measured against a state
already known exact — under bypass the picture ends at 923 and 924 is stale
scratch, confirmed by eye:

| | picture | against display `127..923` |
|---|---|---|
| `BYPS` 1 (bypassed) | 127 .. 923 | exact fit |
| `BYPS` 0, `HSCALE` 1023 | **129 .. 926** | 2 px of scratch at the **left**, 2 px cut at the right |

Garbage appearing on the *left* is the whole point: a stretch can only extend
the right edge, so this is a **pure shift**, not a widening. Putting the scaler
in the path costs **two pixels** of pipeline delay:

```
origin = VDS_HB_SP + 78     scaler bypassed
origin = VDS_HB_SP + 80     scaler engaged
```

Re-measured after a first pass read it as one pixel. The number that settles it
is the *left* edge, because scratch-or-no-scratch is discrete: with the scaler
engaged, a clean left edge needs `VDS_DIS_HB_SP` = 129 against `VDS_HB_SP` = 49.

Anything computing `VDS_DIS_HB_SP` has to know which of the two it is in, or it
clips a pixel or shows one of scratch.

### Whether `VDS_HSCALE` carries a `+1` is still open

This chip encodes raster totals as N-1 — the datasheet says `VDS_HSYNC_RST`
"contains horizontal total value minus 1", and every total in this document has
had 1 added. So `HSCALE` plausibly means `HSCALE + 1`, which would make 1023
exactly unity rather than a register that misses 1:1 by 0.1% for no reason:

```
no +1 :  797 x 1024/1023 = 797.78 px
   +1 :  797 x 1024/1024 = 797.00 px
```

**The one-pixel shift above does not decide it**, though it was briefly written
up as if it did. With the picture starting at 128 and the display window ending
at 923, both widths look identical — the 0.78 px difference falls past 924,
where the window has already cut. A differential measurement only works if the
origin holds still, and here it moved.

Deciding it needs high magnification, where the two forms diverge by whole
pixels: at `HSCALE` 128 on a 150-sample capture they predict 1200.0 px against
1190.7, which is 9 px apart and well inside what creeping `VDS_DIS_HB_ST`
resolves. Not run — it needs a narrow capture and `VDS_HB_ST` near 1280, so it
tears down a tuned state.

`geometry_math` currently uses the no-`+1` form. At the scale factors used so far
the error is sub-pixel; it becomes real once magnifying 2x or more to fill a
screen, which is where this work is heading.

### `geometry.py` does not read the bypass bit

A real defect, and it misled this session. `geometry.py` decides
*"BYPASSED (1:1)"* from the `HSCALE` **value** and never reads
`VDS_HSCALE_BYPS`. In a state with the bypass bit set and a value of 1013 it
reports 805.7 produced pixels where the truth is 797 — a 9 px headroom error, in
the unsafe direction.

### Three horizontal unit systems, and the conversions between them

The single most confusing thing about this axis is that three different units are
in play and two of them look like "pixels". Getting them straight is what made
the capture window land on the mode file.

```
PLLAD_MD              ADC samples per source line
IF_HSYNC_RST + 1      IF units per source line   = PLLAD_MD / 2
                      so 1 IF unit = 2 ADC samples
MDF htotal            source pixels per line     (what the RISC PC thinks it drew)
```

`IF_HB_SP2`/`IF_HB_ST2` are in **IF units**. `VDS_*` are in **output pixels**.
Neither is a source pixel. At `PLLAD_MD` 2553 against AKF50's 512-pixel line:

| | |
|---|---|
| IF units per line | 1277 |
| ADC samples per IF unit | 1.9992 (i.e. 2) |
| **IF units per source pixel** | **2.4941** |
| ADC samples per source pixel | 4.9863 |

**`HSCALE` 1:1 is not 1:1 with the source.** It is one output pixel per ADC
sample, and there are ~5 samples per source pixel, so the picture is already
magnified about 2.5x by the sampling alone before the scaler does anything. This
is what CLAUDE.md means by "the horizontal axis has no native resolution" — how
many samples there are per source pixel is a choice, made by `PLLAD_MD`.

### The capture window, closed

Trimmed by eye against `TestPat`'s green 1-pixel frame until the magenta screen
border was gone on both sides and green was outermost:

```
IF_HB_SP2 264 .. IF_HB_ST2 1062  =  798 IF units
                                 = 1595 ADC samples
                                 = 319.95 source pixels
```

**Against the mode file's 320-pixel display that is an error of 0.05 pixels.**
Three independent things had to be right for it to land there: AKF50's
`h_timings`, the 2.4941 conversion derived from `PLLAD_MD`, and the green-frame
edge detection. Any one of them wrong and it does not come out at 320.

### The sample ratio is not an integer, and that is fixable

2.4941 IF units per source pixel means a capture edge cannot land on a source
pixel boundary, and a one-on/one-off grating falls differently on the sample
grid every pixel — the moiré the `TestPat` legend warns about. Whole IF units per
source pixel needs `PLLAD_MD` to be a multiple of 1024:

| `PLLAD_MD` | IF units/px | samples/px | prior work |
|---|---|---|---|
| 2048 | 2 | 4 | `snapshots/pllad-2048-integer-ratio-2026-08-02.json`, and `pllad-2048-latched` |
| 3072 | 3 | 6 | `snapshots/pllad-3072-integer-if-units-2026-08-02.json` |
| 4096 | 4 | 8 | — |
| **2553 (current)** | **2.494** | **4.986** | — |

Read those snapshots before re-deriving; the `latched` name suggests 2048 had a
problem. And the standing warning applies: **sync stability does not mean the
divider is right.** `getStatus16SpHsStable()` passed with `PLLAD_MD` halved from
2553 to 1276 while the display went solid green. The grating is the honest test.

### The horizontal chain, end to end

Every value below is measured or derived from a measurement, not chosen:

| | |
|---|---|
| capture | `IF_HB` 264..1062 = 798 IF units = 320.0 source px |
| sampling | `PLLAD_MD` 2553 = 4.986 samples per source pixel |
| scale | `HSCALE` 1023, `BYPS` 0 -> produced 798.8 output px |
| origin | `VDS_HB_SP` 49 + 80 = 129 |
| picture | 129 .. 927 |
| display | `VDS_DIS_HB` 129 .. 927 |
| memory | `VDS_HB` 49 .. 1074 = 1025, headroom +226 |
