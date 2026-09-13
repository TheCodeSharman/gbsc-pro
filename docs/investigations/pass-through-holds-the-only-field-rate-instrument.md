# Pass-through holds the only field-rate instrument

The engine measures the source's field rate by timing a pulse at `DEBUG_IN_PIN`,
routed there from one of the TV5725's internal test buses. Entering pass-through
held the block that generates that pulse in reset, so a passed-through source
could not be measured at all — and re-deciding pass-through from a measurement
is what `VideoPath::solveFromMeasurement()` exists to do.

## The vertical pulse is on two selectors and both are the input formatter's

`/testbus` sweeps `TEST_BUS_SEL` 0..31 and counts transitions over a window.
Taken on the bench RiscPC at 320x256@50 with a clean picture, 100 ms per
selector:

| `TEST_BUS_SEL` | transitions | what it is |
|---|---|---|
| 0 | 10 | 50 Hz — the vertical the engine times |
| 2 | 10 | 50 Hz |
| 5, 6, 7 | 11070, 5349, 14142 | line rate |
| 12 | 224 | ~2.2 kHz |
| 14, 15, 16, 18 | 14064, 17306, 16634, 8532 | line rate |
| everything else | 0 | |

Taken again with the output passed through, selectors 0..11 read **zero** and
12, 14, 15, 16, 18 are unchanged. Nothing anywhere in 0..31 shows the six to
twelve transitions a 50 or 60 Hz vertical would give. The two selectors that
carry it are the input formatter's, and they die with it.

`STATUS_SYNC_PROC_VTOTAL` reads the source's line count perfectly throughout —
627 on a 800x600 source, never wavering. **The count is available and the pulse
is not**, and those are different questions.

## The sync processor is not a second instrument

Reaching for `getSourceFieldRate(1)` is the obvious move and does nothing. On a
**separate-sync** source the two calls differ by one write:

| | `TEST_BUS_SEL` | `IF_TEST_SEL` | `s5_63` |
|---|---|---|---|
| `getSourceFieldRate(0)` | 0 | 3 | left as found |
| `getSourceFieldRate(1)` | 0 | 3 | `0x0f` — `SP_TEST_EN` 1, `SP_TEST_MODULE` 7, signal 0 |

Swept side by side, `if=3` and `sp=7` give byte-identical counts at all 32
selectors, in both the healthy and the failing state. And selector 10 — the one
`getSourceFieldRate(1)` uses on a composite-sync source — reads **0 transitions
even with a perfect picture**, which is the same result
[`field-rate-measured-downstream.md`](field-rate-measured-downstream.md) reached
by forcing it unconditionally.

## What held the block

`Chip::resetVideoBlocks()` held six blocks on the bypass branch — the input
formatter, both FIFOs, the memory, the deinterlacer and the VDS — on the
reasoning that nothing scaled is running. That is right for five of them. The
input formatter is the one that also **measures**, and measuring is needed
whether or not the scaler is running, so it now stays released: it counts the
source and drives its test bus, and the memory blocks being held is what stops
it writing anywhere.

The failure was a sequence rather than a state, and only the console shows it:

```
2.78  evt,rgbhv-enter-bypass,627,14
4.89  sampling: 627 lines x 54.58 Hz -> line rate 34276
5.0-6.2  thirty-five readings at 60.31 Hz, every one refused
6.52  evt,bypass-switch,627,15
8.79  sampling: 627 lines x 0.00 Hz -> line rate 0      indefinitely
```

The `0.00` begins at the instant the bypass switch runs, not at the source mode
change. Everything between 4.89 and 6.52 is a second fault visible in the same
capture: one bad first sample at 54.58 Hz against a real 60.31 became the held
good rate, and `rateFollowsCount()` then refused every correct reading after it.
[`two-instruments-decide-one-raster.md`](two-instruments-decide-one-raster.md).

## The deleted arm never used its own measurement

`runSyncWatcher()`'s leave-bypass arm called `getSourceFieldRate(1)` and passed
the answer to `PresetLoad::rgbhvStandardFor()`, which reads it only above
`TallSourceLines`:

```cpp
if (sourceLines < ShortSourceLines) return 1;   // 280
if (sourceLines < TallSourceLines)  return 2;   // 380
if (fieldRateHz > 44.0f && fieldRateHz < 53.8f) return 4;
return 3;
```

At 311 lines it returns 2 and the rate is never read. So the arm measured a rate
it discarded, and was load-bearing for a different reason: it went on to call
`applyPresets()` → `doPostPresetLoadSteps()` → `BringUp::init()` →
`Chip::init()`, which writes `SFTRST_IF_RSTZ` back to 1. **The preset load was
releasing the input formatter**, and deleting the arm removed the only route
that did.

That corrects this page's predecessor, which enumerated the five register writes
the arm made between the count and its field rate and concluded those were what
the leave path still needed. They are not: four of the five are claimed by
`BringUp::init()` and `Adc::applySampleRate()` already, and the fifth
(`SP_CLAMP_MANUAL`) has a live writer in `updateClampPosition()` on every pass.

## What leaving pass-through has to put back

Entering it configures the chip away from the scaling setup, which is what
`BringUp::arm()` in `enterHdBypass()` records. Nothing on the engine's path
claimed any of it back, and each piece was found by a different instrument:

| left behind | symptom | found by |
|---|---|---|
| the input formatter held | `0.00 Hz`, no solve ever | `/testbus` sweep |
| the VDS and memory blocks held | sink reports "No signal" | photograph |
| `DEC_MATRIX_BYPS` 1 | magenta whites over green blacks | photograph |

`Chip::init()` releases the deinterlacer, both FIFOs, the memory and the OSD but
**not** the VDS or the input formatter — only `resetVideoBlocks()` does, and only
on its scaling branch, which the route is what selects. So the order on the way
out is: route to the scaler, bring up, restart the blocks, restore the matrix,
and re-arm the measurement rather than solving from a rate that still names the
mode pass-through was entered on.

Which matrix the source wants is held by `Adc::inputIsComponent()`, so the engine
asks the peer that already knows rather than taking it as an injected action.

## Measured after the change

RISC PC on `vga`, `preferScalingRgbhv` off, the sketch's three RGBHV arms deleted:

- 320x256 → 800x600 enters pass-through, full-screen picture, correct colour
- 800x600 → 320x256 reaches `state: acquired` in under five seconds, with the
  correct rate on the first reading and **no `0.00 Hz` sample at all**
- three further round trips, all clean
- `HPERIOD_IF` reads the 431 the mode is due rather than the railed 511 that had
  been standing

Before it, the same round trip produced `0.00 Hz` for as long as it was watched,
and neither `/sampleclock` nor `/sc?~` cleared it — only a reflash did.

## The round trip is state-neutral, over every address

Taken with `snapdiff.py --save` at both ends -- all 1536 addresses, not the
608-address config range the earlier comparisons here used -- a full 320x256 ->
800x600 -> 320x256 excursion leaves **one byte** different:

```
1 bytes differ, resolving to 1 fields
    PA_SP_S    16 -> 18    (s5 0x19 b1 w5)
```

the sync processor's sampling phase, which the phase search re-tunes by design.
Every other address agrees, including the 928 that no comparison here had ever
covered.

**So a framing that is wrong after an excursion is not something the excursion
left behind.** `/geometry` reports the same origin and extent either side, and
the picture looks the same before and after. What differs from an earlier
session's framing is the output RASTER the solve landed on -- 1915 against 2022,
with `PLL648_CONTROL_01` unchanged, so the line is shorter at the same pixel
clock and the encoder is shown a different mode. That is
[`two-instruments-decide-one-raster.md`](two-instruments-decide-one-raster.md),
and it is where a framing question belongs rather than here.

The same measurement retires the standing suspicion that a bypass round trip
strands registers nobody has looked at. It does not; the set was simply never
looked at.
