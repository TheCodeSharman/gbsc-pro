# Leaving pass-through stranded the sync reference, and one bit held the ADC PLL unlocked

A source handed to the encoder and then taken back came out with an unlocked ADC
PLL: jittery edges, a disturbed band across the top of the frame, and every
configuration register on the machine reading correct. `STATUS_MISC_PLLAD_LOCK`
sampled at 25 ms from `loop()` read **0.0..0.5% of ~210 samples** against
**82..87% healthy**, and `STATUS_SYNC_PROC_HTOTAL` wandered 1211..1213 at sd
0.33 against a steady 1212.

The cause is `SP_CS_P_SWAP`. It selects which edge of the source's sync the
sync processor regenerates from, and it reaches the ADC PLL -- so on the wrong
edge the loop has no reference it can lock to.

## The provocation, and it is two requests

Measured 2026-10-23 on `ypbpr`, the Wii at 480p into 1920x1080, which carries
sync on green and so is a composite-sync source:

```sh
curl 'http://<ip>/uc?x'    # preference -> Pass Through, the source is handed over
curl 'http://<ip>/uc?x'    # preference -> Scale, and the picture comes back broken
```

The earlier reading that it needed a `vga` leg first, and a pass-through state
entered from another input, is superseded: the round trip on one input is the
whole of it. `/uc?x` is the route that works from a script -- it toggles
`preferScalingRgbhv` ungated and resolves the source in force, where `/uc?K`
is gated by `bypassCanBeDisplayed()`.

## What differs is five fields, and four of them are one held value

A full 1536-address `snapdiff.py --save` either side:

```
PA_ADC_S          0 -> 16
SP_CS_CLP_SP     82 -> 23
SP_CS_CLP_ST     78 -> 19
SP_CS_HS_SP      84 -> 0
SP_CS_P_SWAP      1 -> 0
```

The last four all follow `SyncProcessor::leadingEdge_`.
`referenceLeadingEdge()` writes `SP_CS_P_SWAP` from it;
`referenceMovedByPulse()` gates the clamp's pulse offset on it, which is the
whole of 78/82 against 19/23; and `placeSeparatorPulseEnd()` writes 0 without
it. So the machine differs in one held fact and one phase.

## Which of them matters, measured one at a time

Written by hand into the live fault, each addition kept:

| written | lock | `sp_htotal` |
|---|---|---|
| the fault as provoked | 0.0..0.5% of ~210 | sd 0.25..0.34, 1211..1213 |
| `PA_ADC_S` = 0, the oversample-4 phase | **0.5% of 190** | sd 0.34, 1211..1213 |
| `SP_CS_P_SWAP` = 1 | **87.0% of 216** | sd 0.00, 1212 |
| + the clamp pair 78/82 | 85.3% of 217 | sd 0.00, 1212 |
| + `SP_CS_HS_SP` = 84 | 86.0% of 215 | sd 0.00, 1212 |

**`PA_ADC_S` is refuted**, and it is the obvious suspect: pass-through runs at
oversample 1 where the scaling path runs at 4, `Adc::acquirePhase()` picks
`halfSampleOn(MidField)` for one and `MidField` for the other, and the refusal
path returns before it chooses -- so the phase really is left on pass-through's
value. It changes nothing about the lock.

**`SP_CS_P_SWAP` is the whole of it**, A/B/A/B on one acquisition: 0.5%, 86.6%,
(missed window), 84.8%. The clamp pair and the separator pulse end add nothing
to the lock -- they are worth writing for the picture, not for the clock.

`frame_bands.py` over 120 full-resolution frames reads median band spread
**0.151, worst 0.993** with the bit put back by hand, against the fault's rows
0..319 at sd 2.6..7.5.

## Why nothing in the firmware put it back

`HdBypass::enterFor()` applied the pass-through reference -- the trailing edge
-- through a setup of its own. That was a **second owner** of registers
`applySeparation()` owns, and it moved them without moving `VideoPath`'s held
type. `VideoPath::applySyncType()` then skips:

```cpp
if (syncTypeApplied_ && csync == syncTypeInForce_)
    return;
```

`syncTypeInForce_` still says composite, so the application that would write the
registers again is refused as redundant. `SyncProcessor::reset()`, which
`configureScalingPath()` runs on the way out, then zeroes the clamp pair and
`SP_HS2PLL_INV_REG` as well, and nothing re-applies any of it.

That is also why the two recoveries measured behave as they do. A re-detection
(`/sc?~`) forgets the sync type, so the next application is not skipped and the
arrangement is written. `Adc::reapplySampleRate()` -- the whole PLL group plus
the restart, which `/sampleclock?md=1212` performs -- leaves the lock at 0.5%
and the frame's top rows at sd 7.70 against the fault's 7.91, because
`applySampleRate()` touches no sync-processor register at all.

## The fix

`VideoPath::configureScalingPath()` calls `reapplySyncTypeInForce()`, which
writes the arrangement for the type already held and asks no probe. It belongs
there beside the route, the blocks and the colour path -- the function's job is
putting back what pass-through took.

Three round trips on the fixed build, each landing scaled on `ypbpr`:

| | lock | `sp_htotal` | `SP_CS_P_SWAP` | phase scan |
|---|---|---|---|---|
| baseline | 83.4% of 211 | sd 0.00, 1212 | 1 | `...........................22...` |
| leg 1 after leaving | 86.9% of 213 | sd 0.00, 1212 | 1 | `...........................45...` |
| leg 2 after leaving | 82.4% of 210 | sd 0.00, 1212 | 1 | `...........................11...` |
| leg 3 after leaving | 84.4% of 224 | sd 0.00, 1212 | 1 | `...........................45...` |

No `sampling clock unlocked` line in the three round trips, against seven
consecutive refusals per excursion before. `frame_bands.py` reads median 0.223,
worst 1.262.

## And then the seam itself, rather than the symptom

Re-applying on the way out makes the overlay reversible and leaves the shape
that caused it: a reference written from whichever path ran last, with nothing
recording which. `SP_HS2PLL_INV_REG` had three writers on top of that, each
answering a different question -- the edge the sync type chose, the source's
measured polarity, and 0 for the ADC offset calibration.

**Both answers are right for their route**, which is why neither is deleted.
Scaled, `normaliseHsyncPolarity()` makes the polarity one shape at the boundary,
so the inversion is the chosen edge and a second one on top is displacement --
94 output px, measured in
[leaving-bypass-leaves-the-sync-path-behind.md](leaving-bypass-leaves-the-sync-path-behind.md).
Passed through, the channel replays the source's pulses the way round they
arrive, so the PLL needs the inversion matched to the SOURCE.

So the route became a fact the one owner takes.
`SyncProcessor::applyReference()` is the only writer of `SP_SOG_P_INV`,
`SP_CS_P_SWAP` and `SP_HS2PLL_INV_REG`, and takes the route, the sync type,
whether sync arrives on green, and the source's polarity.

**THE TWO WRITERS LOST THE POLARITY, which is a defect of its own and is what
pins the change.** The channel matches the inversion to a negative-going source;
a re-entry then wrote the trailing-edge answer over it, and the ADC PLL
referenced the wrong edge until the channel sync service next ran. A host test
in `test/test_hd_bypass.cpp` fails on that and passes after.

Scored across every sync arrangement the bench has, scaled after leaving
pass-through:

| source | arrangement | lock | `SP_CS_P_SWAP` | `SP_HS2PLL_INV_REG` |
|---|---|---|---|---|
| Wii 480p, `ypbpr` | sync on green | 83.1%, 87.9% | 1 | 1 |
| RISC PC 720p, `vga` | separate | 89.6%, 96.1% | 0 | 0 |
| RISC PC 800x600, `vga` | composite | 88.8% | 1 | 1 |

all at `sp_htotal` sd 0.00 on the divider, with pass-through itself 82.9..100%
in every leg.

**THE TWO FORMULAS AGREE ON ALL THREE, AND THAT IS WHY THE COLLISION NEVER
BIT.** A composite-sync source here reads negative hsync and a separate-sync one
positive, so `csync` and `!sourcePositive` come out the same on every source the
bench has. The divergent case -- composite sync with positive hsync, or separate
sync with negative -- is not reachable from here, so what is measured above is
that single ownership costs nothing, not that the two answers differ in practice.

## What the fault made look broken, and was not

Everything the unlocked state showed is downstream of the clock, and each was
filed as a fault of its own:

- **the phase scan returning noise.** `STATUS_SYNC_PROC_HTOTAL` counts ADC
  clocks, so an unlocked PLL makes the count wander at *every* phase and the
  scan's own metric is that wander. No setting of the phase adjuster can steady
  an unlocked clock.
- **the field-rate probe reading 47888 and 53280 mHz** against a source at
  59939. Those are `8/9` and `8/10` of the true rate against
  `UnconfirmedRatePulses` of 8 -- a miscounted edge, not a rate.
- **a re-solve every ~25 s armed by `TIMINGS change (rate)`**, with the output
  black for 4.6 s each time.

**The discriminator on the scan is the clean RUN, not the clean count**, and
that stands whatever else changes. A healthy pass-through scan scores 17..19 of
32 against a gate of 17, and an unlocked clock scores about the same -- what
separates them is that the clean phases are SCATTERED, widest run of one to
three, where a real band is a contiguous window of a dozen or more.
