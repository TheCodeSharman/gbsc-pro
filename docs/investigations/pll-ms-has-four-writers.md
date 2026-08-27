# PLL_MS has four writers, and returning from bypass is not where it goes wrong

`Tv5725::MemoryBus` derives the memory clock rather than inheriting it:
`SdramTimings::fastestInSpec()` walks the eight `PLL_MS` codes, computes the
activate and precharge counts each one needs, and returns the fastest that stays
inside the EM638325TS-6's AC characteristics. That is **3**, 162 MHz.

Three functions in the sketch write **2**, which is not a frequency: it selects
the pad FEEDBACK clock. `setResetParameters()`, `setOutModeHdBypass()` and
`bypassModeSwitch_RGBHV()` each write it, and in bypass that is reasonable --
the frame buffer is out of the path, so what clocks it does not matter.

## Coming back from bypass is fine, and that is measured

Driven from the SOURCE rather than over HTTP -- 800x600@60 crosses the 535-line
threshold and forces RGBHV bypass, 320x256@50 comes back -- the clock is right at
both ends, three excursions out of three:

| | `PLL_MS` | `DAC_RGBS_ADC2DAC` | `OUT_SYNC_SEL` |
|---|---|---|---|
| in bypass | 2 | 1 | 1 |
| back on the scaled path | **3** | 0 | 0 |

So the ordering fault this page used to describe does not exist: `MemoryBus::init()`
runs and nothing overwrites it. **The frame buffer is never clocked from the pad
while the scaler is using it.**

## Where 2 does survive, and why it reaches nothing

`PLL_MS` at 2 on the scaling path is a **no-sync** state, not a scaled one. It
comes from `setResetParameters()`, which the RGBHV no-sync watchdog and
`goLowPowerWithInputDetection()` both call. That function arms the bring-up, and
the bring-up runs on the next preset load -- which needs sync. No sync, no preset
load, so 2 stays until the source comes back.

**The witness is `PLLAD_MD` 1792.** `setResetParameters()` writes `0x700` and the
literal appears nowhere else in the sketch, so a dump holding it names its writer
with no ambiguity. Measured beside it: `STATUS_SYNC_PROC_VTOTAL` 0, `HTOTAL` 0,
`DAC_RGBS_PWDNZ` 0 and `GBS_OPTION_SCALING_RGBHV` 0. Nothing is captured, nothing
is played back and the DACs are down, so the memory clock reaches no video.

An earlier reading of "2 of 3 runs left `PLL_MS` at 2" was this state, taken
without the fields that distinguish it from a working scaled path.

## Where it belongs

The move stands on its own terms and is Phase 7 of the preset-load work: three
functions writing a literal is one field with no owner, and the last writer wins
by position rather than by decision. `MemoryBus` should own it, with bypass
asking for the feedback clock by name.

What the move does **not** need to claim is a live defect. Asserting `PLL_MS` in
`test_arming_the_chip_brings_the_subsystems_back` would be asserting a value the
bring-up already writes correctly; what makes that test intermittent is the
excursion it uses, not the clock. See the note there.
