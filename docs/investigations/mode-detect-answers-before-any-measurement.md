# Mode Detect answers in one burst, and nothing asks it

The acquisition path measures the source with a steadiness run over
`STATUS_SYNC_PROC_VTOTAL`. The chip's own Mode Detect block classifies the same
source from one six-byte read, and on a recognised mode that classification
determines the timings outright. What follows is what each route answers on the
two bench sources, and what stands in the way of using the cheap one.

`docs/video-source-acquisition.md` is the design this supports.

## The classification is one burst

`IF_STATUS_` is documented as a single 45-bit block at `s0_00..s0_05`, so the
whole answer -- family, specific mode, scan type, custom-mode flag -- arrives in
one read with no settling time.

| | Wii 480i on `ypbpr` | RiscPC 320x256@50 on `vga` |
|---|---|---|
| raw `s0_00..05` | `8f 00 00 00 40 00` | `00 00 00 00 00 1c` |
| bits set | `INP_SD`, `INP_NTSC_INT`, `INP_INT` | `INP_SW` only |
| `STATUS_IF_INP_NTSC_INT` | **1 in 755 of 755** | 0 |

So a recognised source is classified completely and instantly, and an
unrecognised one sets no mode bit at all -- which is the signal to escalate.

**`STATUS_IF_INP_INT` and `STATUS_IF_INP_PRG` are generic**, `s0_04[6]` and
`[7]`, and answer the scan type for any mode rather than for NTSC and PAL alone.
`ModeDetect::sourceIsInterlaced()` reads `STATUS_IF_INP_NTSC_INT` and
`STATUS_IF_INP_PAL_INT` only, so 1080i and any custom interlaced mode get no
answer from it. The generic bit is set on the Wii at 480i alongside the NTSC one.

## The custom-mode slot has never been programmed

`MD_USER_DEF_VCNTRL` (`s1_80`) and `MD_USER_DEF_HCNTRL` (`s1_81`) are a
user-defined mode, and `STATUS_IF_INP_USER` (`s0_05[0]`) fires when a source
matches them. Both registers read **255**, the reset value; `ModeDetect::init()`
writes neither. That is why the custom-mode flag never answers, and why the
RiscPC sets no bit at all rather than setting `INP_USER`.

Written from a completed measurement, a custom mode becomes a recognised one:
the first step answers it from then on, and the mode-change interrupt below
begins to fire for it.

## The mode-change interrupt is enabled, unread, and wiped on a timer

`STATUS_INT_INP_SW`, `s0_0F[3]`, is documented as "input source switch the
mode". `INT_ENABLE3` is written 1 by `Interrupts::init()`, and `INT_RST_3` is
its acknowledge.

- **Nothing reads it.** The only reference outside the register declaration is
  `INT_RST_3::write(0x0)` in `init()`.
- **`loop()` destroys it every 3 s.** `Interrupts::acknowledgeAllButSogBad()`
  writes `0xfe` to `s0_58`, which is bits 1 to 7 and includes bit 3.
- The sketch uses `STATUS_INT_SOG_SW`, `s0_0F[1]`, in its place. That reports
  the sync separator switching rather than the mode changing.

Polled at roughly 50 Hz for 62 s across two source mode changes on `vga`, with
no writes in the window:

| bit | result |
|---|---|
| `STATUS_INT_INP_SW` | **0 in 3079 of 3079** |
| `STATUS_INT_SOG_SW` | 1 in **1** of 3079 -- `takeSourceDisturbed()` consumes it in ~20 ms |
| `STATUS_IF_INP_SW` | 1 for 40 s, then clear; not a change latch |

A single catch on `SOG_SW` is what a consumed latch looks like. Bit 3 survives
up to 3 s and was never seen set, so on this source it does not fire at all.
**The leading explanation is that Mode Detect classifies nothing on it**: a
switch detector keyed on the classification has nothing to report while the
classification is nothing. That predicts the bit DOES fire on a source Mode
Detect recognises, which is untested -- it wants a Wii mode change between 480i
and 480p, where both sides classify.

**An acknowledge pulse of `INT_RST_3` makes the bit read 0/1 at random.**
Measured before the clean run above and mistaken for the bit firing; the
62-second poll with no writes is the one to believe.

## What it costs on the interlaced source

`countHeld()` requires four consecutive identical counts. An interlaced source's
field count cannot supply them: 480i is 262.5 lines per field, so the counter
alternates. Measured on the Wii over 1417 samples:

```
STATUS_SYNC_PROC_VTOTAL   260 x736, 259 x681   -- two values, nothing else
VPERIOD_IF                524 x1417            -- the frame count, steady
HPERIOD_IF                428                  -- exact for 15734 Hz
PLLAD_MD 2050 == STATUS_SYNC_PROC_HTOTAL 2050  -- latched and locked
```

Every register measures the source correctly and the steadiness run still never
completes, so `sourceIsPresent()` stays false, no solve runs for the mode, the
output clock is never seeded and the picture rolls. Mode Detect has the answer
throughout.

**The roll and the presence verdict are separable.** A later solve seeded the
clock and the roll stopped while `state` stayed `absent`.

**The sketch and the engine disagree in the opposite direction here.**
`printInfo()` reads `m:1` in 96 of 96 with `u:0` and `s:ff` -- `getVideoMode()`
classifies the source and the no-sync branch never runs -- while `/geometry`
reads `present: false`. On the scaling-RGBHV source of
`the-gate-runs-a-ladder-that-is-not-safe-yet.md` the two answers are the other
way round. Neither is the reliably better answer, which is the constraint on
step 4 of the plan.
