# PLL_MS has four writers, and coming back from bypass the wrong one wins

`Tv5725::MemoryBus` derives the memory clock rather than inheriting it:
`SdramTimings::fastestInSpec()` walks the eight `PLL_MS` codes, computes the
activate and precharge counts each one needs, and returns the fastest that stays
inside the EM638325TS-6's AC characteristics. That is **3**, 162 MHz.

Three functions in the sketch write **2**, which is not a frequency: it selects
the pad FEEDBACK clock. `setResetParameters()`, `setOutModeHdBypass()` and
`bypassModeSwitch_RGBHV()` each write it, and in bypass that is reasonable --
the frame buffer is out of the path, so what clocks it does not matter.

Coming back to the scaling path it does matter, and the write order does not
hold. Measured with the memory-bus registers poisoned, armed by an RGBHV bypass
excursion and read after the bring-up: **2 of 3 runs left `PLL_MS` at 2**, with
every other register the bring-up owns correct -- so `MemoryBus::init()` ran,
wrote 3, and something wrote 2 after it.

The frame buffer is then clocked from the pad feedback clock while the scaler is
using it, and the whole `SdramTimings` derivation is bypassed without anything
saying so. The picture is unaffected on this bench.

## Why it does not always reproduce

`bypassModeSwitch_RGBHV()` does not always take on a 311-line source. Sampled
across an excursion with nothing poisoned, `PLL_MS` read 3 throughout, including
while nominally in bypass -- the switch had not run. It is the runs where it does
run that leave 2 behind, which is what makes this look intermittent from outside.
`BringUp::arm()` is the first statement in that switch, so a run that repairs the
poisoned memory map is a run where the switch definitely ran.

## Where it belongs

The bypass functions are Phase 7 of the preset-load work, where their register
writes move onto `Tv5725::` classes. `PLL_MS` should end up owned by `MemoryBus`
with bypass asking for the feedback clock explicitly, rather than three functions
writing a literal and the last one winning.

`test_arming_the_chip_brings_the_subsystems_back` covers the other 21 fields the
bring-up owns and deliberately leaves `PLL_MS` out, with the reason at the table.
Putting it back is how the fix gets checked.
