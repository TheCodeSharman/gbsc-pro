# A test bus selection has two halves, and the measurement set one

`DEBUG_IN_PIN` carries whatever the chip has selected onto its test bus, and
every rate the engine measures is a period timed on that pin. The selection is
a **two-level mux**: `TEST_BUS_SEL` picks which block drives the bus, and the
block that generates the signal has its own test-output enable and its own
signal select.

`Tv5725::TestBus` owned the top level. Nothing owned the bottom.

| level | registers | owner |
|---|---|---|
| which block drives the bus | `TEST_BUS_SEL`, `TEST_BUS_EN` (s0_4D), `PAD_BOUT_EN` | `TestBus`, complete |
| the input formatter's output | `IF_TEST_EN` s1_28[3], `IF_TEST_SEL` s1_28[7:4] | nobody |
| the VDS's output | `VDS_TEST_EN` s3_50[4], `VDS_TEST_BUS_SEL` s3_50[3:0] | nobody |
| the sync processor's stage | `SP_TEST_EN`, `SP_TEST_MODULE`, `SP_TEST_SIGNAL_SEL` (s5_63) | `SyncProcessor::driveTestBus()`, complete |

`IF_TEST_EN` and `VDS_TEST_EN` had **exactly one writer each in the whole
tree**, `resetDebugPort()`, reachable only from `setResetParameters()`,
`inputAndSyncDetect()`, `doPostPresetLoadSteps()` and `enterHdBypass()`.
`VDS_TEST_BUS_SEL` had none. So a measurement selected its signal and then
depended on a reset function having run earlier in the boot to have the block
driving it at all — and the chip keeps its registers across an ESP reset, so
what it depended on could be from before the reset.

The field rate was the closest to the hole: `sourceFieldRateHz()` wrote
`IF_TEST_SEL` raw and never wrote `IF_TEST_EN`. `outputFrameRateHz()` selected
the VDS and wrote nothing about it at all.

## The selector map cannot be inferred, so the fix is named signals

RD-5725-1.1 tabulates **no values** for `TEST_BUS_SEL` — the Bit/Name table
says only "Test bus selection" — and none for `IF_TEST_SEL` either, which reads
"Select which signal to the test bus". So which block drives a given selector
is not derivable from the datasheet, and `select()` cannot enable the right
block from the number alone.

The two signals the firmware names are complete calls instead:
`TestBus::selectInputVsync()` and `selectOutputVsync()` each enable the
generating block, ask it for the signal, then select it onto the bus and out of
the pad. `select()` stays raw for the sweep `/testbus` needs, which has to reach
all 32 selectors with the sub-selection under the caller's control.
`FormatterVertical` is 3 because that is what every rate measured on this board
has been timed on, not because the datasheet says so.

## What this does not establish

The enables read 1 on a settled unit — measured on the bench at 800x600@60 in
pass-through, `IF_TEST_EN` 1, `IF_TEST_SEL` 3, `VDS_TEST_EN` 1, with
`SP_HTOTAL` 2038 against a `PLLAD_MD` of 2038. So the hole is **latent**, and
its window is a boot or an acquisition that measures before any of the four
callers of `resetDebugPort()` has run. `VideoSourceAcquisition` now arms its own
first solve on a cold engine, which is a path into exactly that window.

**Nothing here shows this is what produces the bad readings** in
[`the-field-rate-measurement-is-unreliable.md`](the-field-rate-measurement-is-unreliable.md).
A half-selected bus would read a timeout and 0 Hz, which is not the line-rate
and twice-field-rate readings that page measures. What closed is an
incompleteness that no register dump can see, and a dependency on call order
between two blocks that had no stated relationship.
