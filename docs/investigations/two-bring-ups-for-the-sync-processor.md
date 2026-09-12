# Two bring-ups for the sync processor, and the one ordering that mattered

`prepareSyncProcessor()` wrote twenty-eight raw bytes into segment 5 through
`writeOneByte()`. `Tv5725::SyncProcessor::init()` writes the same block by
field name. Unnamed, the duplication was invisible: nothing connects
`writeOneByte(0x2d, 0x03)` to `SP_VSYNC_TGL_THD::write(0x3)`.

## How the two were compared

A throwaway host binary against `FakeTwoWire`, since the fake records which
bytes a run touched and what it left in them:

```cpp
Wire.reset();
Tv5725::SyncProcessor::init();
for (int r = 0x20; r <= 0x60; ++r)
    if (Wire.touched[5][r])
        printf("s5_%02x = 0x%02x\n", r, Wire.bank[5][r]);
```

**Every address the two share carries the same value.** Twenty-three of the
twenty-eight, byte for byte. `init()` additionally writes s5_20, 32, 3e, 55, 56,
57 and 63, which the raw block never reached.

That is the comparison to reach for whenever a raw byte sequence is suspected of
duplicating a class — it is exact, it needs no bench, and it costs a minute.

## The raw block restored nothing

The obvious objection is cadence: `init()` runs once per bring-up,
`prepareSyncProcessor()` on every preset load, so the raw block might have been
re-establishing values something else had moved.

It was not. Every named field at those addresses — `SP_SYNC_TGL_THD`,
`SP_L_DLT_REG`, `SP_T_DLT_REG`, `SP_SYNC_PD_THD`, `SP_PRD_EQ_THD`,
`SP_VSYNC_TGL_THD`, `SP_SYNC_WIDTH_DTHD`, `SP_V_PRD_EQ_THD`, `SP_VT_DLT_REG`,
`SP_V_TIMER_VAL`, `SP_RT_HS_ST`, `SP_RT_VS_ST`, `SP_RT_VS_SP`,
`SP_HT_DIFF_REG`, `SP_VT_DIFF_REG`, `SP_STBLE_CNT_REG` — is written **only** in
`init()`. They are static thresholds. Nothing moves them, so nothing needed
putting back.

## The one value that was wrong

`s5_4b`/`s5_4c` is `SP_RT_HS_SP`, and the block set it to **68**.

It is 93% of `PLLAD_MD` — one quantity in three registers, all three written by
`SourceMeasurement` off one held value — and 68 is what the preset tables
shipped, which is wrong for every divider on this bench. `SyncProcessor.h`
carries a standing warning about poisoning this field: set 1110 against a
2553-sample line, and `SP_VTOTAL` fell to a steady 97/98 through the load and for
minutes afterwards.

**It was never a live fault, and the bench said so.** `SP_RT_HS_SP` read 2053
against a `PLLAD_MD` of 2208, exactly the 93% due, because `applySampling()`
writes it after `prepareSyncProcessor()` on every path that matters. A second
writer putting a wrong value in a window that is later corrected is still a
second writer, and it is deleted rather than moved.

## THE ORDERING: detection runs BEFORE the bring-up at boot

This is the part that bites, and deleting the block alone broke the unit.

`setup()` runs, in order:

```
zeroAll()
setResetParameters()
prepareSyncProcessor()      <- the only sync-processor bring-up this early
calibrateAdcOffset()
setResetParameters()
applySavedInputSource()     <- DETECTION, seconds of it
BringUp::init()             <- after the last setResetParameters(), deliberately
```

`BringUp::init()` is last for a good reason of its own: the `setResetParameters()`
above it holds six blocks in reset, and a bring-up that ran before it would be
discarded. But that puts the whole of it **after** detection.

So the raw block was the thing keeping the sync processor configured across the
detection that runs at boot. Removed, detection measured through a sync processor
left at zeros — and with the coast and delta registers clear,
`STATUS_SYNC_PROC_HTOTAL` reads a number that does not move when the divider is
written and latched by hand.

Measured: the unit came up `m:0 s:0 u:97` with the ADC PLL unlocked, `vt: 0`, and
`own V sync: yes` every seven seconds while nothing changed — the documented
post-flash signature, and `/sc?~` recovered it. One failure out of one attempt,
against three clean flashes before the change and three after the fix, so the
count is suggestive rather than conclusive; the argument that settles it is that
detection must not measure through an unconfigured block.

`SyncProcessor::init()` now runs at that point instead. It can, and the later
`setResetParameters()` does not undo it: **`SFTRST_SYNC_RSTZ` is not one of the
six blocks that call holds.**

**The general shape:** a block's bring-up has to precede the first thing that
measures through it, which is not the same question as where the bring-up of the
chip as a whole belongs.
