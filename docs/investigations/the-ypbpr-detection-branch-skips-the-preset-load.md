# The YPbPr detection branch skips the preset load

A boot that lands on `ypbpr` emits a dark, flat or banded green field while the
sync path, the divider and the solved geometry are all correct. The cause is on
the TV5725 and in this firmware: **detection's YPbPr branch claims the source
and returns without a preset load**, so `doPostPresetLoadSteps()` never runs and
the chip is left half configured.

Anything that reaches `applyPresets()` cures it, with no input change and no
register written by hand.

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
