# An acquisition without a preset load emits a flat field

A boot that lands on `ypbpr` emits a dark, flat or banded green field while the
sync path, the divider and the solved geometry are all correct. So does `vga` on
composite sync. The cause is on the TV5725 and in this firmware: **an
acquisition that completes without a preset load leaves the chip half
configured**, and detection's YPbPr branch is one way to reach it -- it claims
the source and returns where the RGB branch calls `applyPresets()`.

Anything that reaches `applyPresets()` cures it, with no input change and no
register written by hand.

**The cure is a transition, not a value.** Writing every differing register back
does not work, in either direction, and neither does establishing them at
bring-up: nine of the fifteen are correct at boot since that change and the
picture is unchanged. What `applyPresets()` supplies beyond them is still open.

## The chain

`detectAndSwitchToActiveInput()` has two claiming branches. The RGB branch
(`currentInput == 1`) probes the sync type and calls `applyPresets()` before it
returns. The YPbPr branch (`currentInput == 0`) chooses the separator level and
returns 2.

Nothing else on the boot path loads a preset. `VideoSourceAcquisition` then
acquires the source and `Tv5725::VideoPath` solves every geometry register from
the measurement, which is why the sync front end, `PLLAD_MD`,
`STATUS_SYNC_PROC_HTOTAL` and the framing all read healthy — and why a register
dump taken on a faulted boot looks like a working unit.

What never runs is the three phases `doPostPresetLoadSteps()` owns:
`applyClockGroup()`, `applyFrameBufferRequests()` and `applyPictureFilters()`.

**`vga` boots clean because the RGB branch is the one that loads a preset.**
Same cable, same source, same settings file but for one key.

## What cures it, and what does not

One boot per row, scored off the USB HDMI capture inside the picture's own
borders with `picstate.py`. A clean `ypbpr` acquisition is `spread 92`, a clean
`vga` one `spread 110`; a flat field is under 6.

| act | reaches | cured |
|---|---|---|
| `/sc?#`, a bare `applyPresets()` | scaler only | **yes**, spread 2.7 -> 80.0 |
| output resolution 1080p -> 720p | scaler only | **yes**, spread 1.3 -> 82.6 |
| `/input?src=vga` then `/input?src=ypbpr` | HC32 and scaler | **yes**, 3 of 3 |
| `/input?src=rgbs` then `/input?src=ypbpr` | HC32 and scaler | no |
| `/input?src=sv` then `/input?src=ypbpr` | HC32 and scaler | no |
| `/avframe?src=vga` then `/avframe?src=ypbpr` | HC32 only | no |
| `/avframe?src=vga` then `/input?src=ypbpr` | HC32 and scaler | no |
| the sixteen differing fields written to their cured values | scaler only | no |

**`/sc?#` is the one that settles it.** It force-calls `applyPresets()` and
touches nothing else: no input, no output, no AV module frame, no hand-written
register. The picture returns.

## This excludes the HC32, and `rgbs` is what excludes it

`rgbs` and `vga` share the RGB connector, so selecting `rgbs` moves
`ADC_INPUT_SEL` 0 -> 1 and back exactly as `vga` does. It carries no sync the
RISC PC's separate-sync source can be claimed on, so it never acquires — and it
does not cure. `sv` shares `ADC_INPUT_SEL` 0 with `ypbpr`, so it transits the
HC32 without moving the ADC input, and it does not cure either.

So neither the AV module frame, nor the switch state it sets, nor the
`ADC_INPUT_SEL` transition is the cure. **What `vga` has and the other two lack
is an acquisition that fails detection's first pass**, which is what reaches the
RGB branch's `applyPresets()`.

## The sixteen fields are the symptom, and writing them back is not the cure

A full `snapdiff.py --save` either side of one cure, same boot:

    CAP_STATUS_SEL     0 -> 1        PB_CUT_REFRESH     0 -> 1
    DEC_IDREG_EN       0 -> 1        PB_REQ_SEL         0 -> 3
    DEC_TEST_ENABLE    1 -> 0        PLL_R              0 -> 1
    DEC_WEN_MODE       0 -> 1        PLL_S              0 -> 2
    PA_SP_S           14 -> 9        VDS_D_RAM_BYPS     0 -> 1
    VDS_FRAME_NO       0 -> 1        VDS_FR_SELECT      0 -> 1
    VDS_FRAME_RST      0 -> 4        VDS_PK_LB_GAIN     0 -> 22
    VDS_UV_STEP_BYPS   0 -> 1        VDS_PK_LH_GAIN     0 -> 10

Every one is written by the three missing phases, and every one is at a reset
default in the faulted state. `DEC_TEST_ENABLE` is the exception that names its
own source: `calibrateAdcOffset()` sets it and never clears it, and
`applyStoredAdcGain()` — inside `doPostPresetLoadSteps()` — is what does.

Writing all sixteen to their cured values on a faulted unit leaves the picture
where it was, spread 1.2 against 1.3. **A write-back tests a state and the setup
is a sequence**: the phases run block resets and re-seed the display clock, and
a final value cannot carry an edge. That negative is what kept this filed as a
separate defect.

## What this retires

- **That the fault is off the TV5725.** `/sc?#` cures it without reaching the
  HC32 at all.
- **That the HC32's switch state is in the causal path.** The frames are
  enumerable and none of them cures; `rgbs` bounces the ADC input and does not.
- **That `TEST_BUS_SEL` 14 and 16 read 0 whenever the picture is wrong.** On the
  dim and flat variants measured here they carry 4049 and 3594 transitions in
  25 ms with the picture wrong, so that reading does not generalise across the
  family.
- **That the sixteen fields are a defect of their own.** They are what the
  missing preset load would have written.

## IT IS NOT A YPbPr FAULT. IT IS AN ACQUISITION WITHOUT A PRESET LOAD

`vga` on **composite sync** does the same thing. `SYNC 1` on the RISC PC, which
re-applies the mode so the sync type reaches VIDC20, leaves the output black with
`STATUS_SYNC_PROC_VTOTAL` 308, `HTOTAL` 2200 against `PLLAD_MD` 2200 and the
engine reporting `acquired` -- and `/sc?#` restores it to `spread 106.1 luma
77.2`. `SYNC 0` recovers on its own, because separate-sync `vga` is what
detection's RGB branch claims and that branch loads a preset.

So the condition is not the connector. It is **an acquisition that completes
without a preset load**, and the YPbPr branch is one way to reach it.

## The boot spends its whole early life with the ADC PLL free-running

With `BOOTLOG_BYTES=8192` the boot log carries the state rather than running out
inside it. From the first measurement to the end of the log, every pass:

    sampling: 270 lines x 60.0 Hz -> line rate 16270
    duty: 242 pulse / 2200 divider, htotal 3258, negative, UNLOCKED

The source is 259/260 lines at 59.93 Hz. `htotal` 3258 against a divider of
2200 is the ADC PLL running free: 3258 x 15644 Hz is 51.0 MHz. The count reads
270 rather than 260 because the sync processor counts in ADC clocks and the ADC
is not locked to the line.

This is the same free-running VCO state
`a-ypbpr-detection-that-succeeds-first-pass-skips-the-preparation.md` measured at
1704 samples against a 1448 divider, 53.6 MHz. There it was cleared by
`SyncRecovery::FullReset` after 25..32 s. Here it is not: the count is *steady*
at 270, so the steadiness run agrees, the engine reaches `acquired`, and the
escalation ladder it would need stops at the first rung.

**A steady count is not a locked one**, and the engine's own state machine says
so -- `SourceUnlocked` exists for exactly this and `Adc::dividerLatched()` is
what decides it. By the time the state is polled over HTTP the count has come
back to 2200 and the state reads `acquired`, so the window in which the two
disagree is only visible from the boot log.

## What the bring-up fix reached, and what it did not

Establishing the decimator modes and the frame buffer's request modes at
bring-up, and stating the user's picture options whenever a mode change
completes, puts **nine of the fifteen** fields right at boot -- `DEC_IDREG_EN`,
`DEC_WEN_MODE`, `CAP_STATUS_SEL`, `PB_REQ_SEL`, `PB_CUT_REFRESH`,
`VDS_D_RAM_BYPS`, `VDS_PK_LB_GAIN`, `VDS_PK_LH_GAIN`, `VDS_UV_STEP_BYPS`.
**The picture is still a flat field.**

### The display PLL's skew must NOT be put in the bring-up

`PLL_R` and `PLL_S` are two of the sixteen, and establishing them there
**blanks the output on a `vga` boot**: nothing emitted, with every register
correct, the sync pad driven, the DACs powered and the raster solved at
1917x1124, where the same boot without it is clean at spread 109.6. `/sc?#`
restores it, so it reads exactly like the fault above and is a different one.

The display PLL is where "black with every register correct" lives, and its
skew belongs with the display clock's own setup rather than with a bring-up
that runs before a clock has been chosen. Nothing a block reset takes away
includes it. A test asserts the bring-up leaves both alone.

Four further candidates were tried on a faulted boot and none cures it:

| tried | result |
|---|---|
| `DEC_TEST_ENABLE` 1 -> 0, which `calibrateAdcOffset()` leaves on | no change |
| `VDS_FRAME_RST`/`VDS_FRAME_NO`/`VDS_FR_SELECT`, the frame sequencing | no change |
| the memory blocks pulsed through `resetVideoBlocks()`'s own sequence by hand | no change |
| waiting several minutes | gets worse -- flat at luma 23 becomes fully black |

Every block reset reads released, both pads enabled, and both scales solved. So
what `applyPresets()` supplies is still a transition and not a value, and it is
not the block reset.

## The family is one fault

A faulted boot is a dark green banded field, a dim one, a flat level or a flat
white one, with the margins varying between boots. All of them carry the
sixteen fields at their reset defaults and all of them are cured by a preset
load. What varies is which partial state the solve reached before the picture
was presented, not which fault occurred.

Four boots this session, all faulted, all cured.

## Method

`picstate.py` scores the emitted frame inside the borders `hdmi_capture` finds,
as `spread` (luma standard deviation) and `cast` (how far the channel means sit
from each other). **Score inside the borders**: a flat white field scores spread
102 over the whole frame because the pillarbox is in the sample, and 5.3 inside
them.

Fields are read by name through `gbs_unit.read_fields()` in one request, so the
set is simultaneous. The boot log carries the determinant directly —
`DETECT: 24ms, syncFound 2` is the first-pass claim — and needs
`BOOTLOG_BYTES=2048` on the flash line.

`docs/investigations/a-ypbpr-detection-that-succeeds-first-pass-skips-the-preparation.md`
is the same first-pass claim seen through its other consequence, the reference
sampling clock, which is repaired on the selection path.
