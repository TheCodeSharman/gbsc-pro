# The first solve of a boot cannot be corroborated

The rate the first solve after a boot holds is not the rate the source is
running. Every later acquisition of the same source, on the same bench, with
nothing touched between them, is exact and repeatable.

**What makes a boot different is not established.** The error's direction is not
fixed and its size is not bounded, which rules out a bias in the instrument and
leaves a transient whose cause is unmeasured.

## What was measured

One source state throughout -- the RISC PC on `vga`, 311 lines, running 50.474
Hz -- across boots and re-acquisitions.

| | held field rate |
|---|---|
| boot | 50.5321, 50.451, 50.451 |
| `/sc?~`, four cycles | 50.4744 every time |

and on other occasions 1.14 per thousand high and 0.45 per thousand low, at
`lineRateHz` 15741 against 15748 and 15618 against 15625.

**The worst reading seen is twice the source.** One boot log, with the correct
rate refused afterwards because the doubled one had become what
`rateFollowsCount()` judges against:

```
sampling: 311 lines x 100.90 Hz -> line rate 31481
sampling: 311 lines x 100.89 Hz -> line rate 31479
sampling: rate 31479 doubled 1 -> divider 1652
sampling: 311 lines x 101.10 Hz -> line rate 31544
sampling: 311 lines x 100.37 Hz -> line rate 31317
sampling: 311 lines x  50.46 Hz -> line rate 0
sampling: 311 lines x  50.46 Hz -> line rate 0
```

so this is the same population as `a-doubled-field-rate-agrees-with-itself.md`,
seen at a smaller size on the input that usually escapes it.

## What it costs, with the frame time lock on

The lock converges on whatever the held rate says, so a held rate that is not
the source's is integrated into a tear that crawls down the screen. Measured on
one boot, either side of the correction, so no cross-boot comparison is involved:

| | held rate | pin in / out | clock |
|---|---|---|---|
| the boot's solve | 50.451 | 3169874 / **3169434** | parked at 108014752, no correction applied |
| after the re-solve | 50.474 | 3169874 / **3169874** | converging, span closing to 0 Hz |

The output frame comes out 440 ticks short of the input's and the phase walks
until the error wraps a frame, at which point the loop yanks the clock. The lock
is not at fault and neither is its tuning.

Two consequences beyond the tear. **The output raster is boot-dependent**,
because the source key is sticky and carries the boot's rate for the life of the
boot -- 1900 against 1902 at 320x256@50 into 1080p, 0.15 per thousand, so
absolute geometry is not comparable across boots. And the stored framing is
filed under a key the source does not have.

## Why nothing already on the board reaches it

**Agreement cannot reject it.** `SourceMeasurement::rateSettled()` promotes a
pair agreeing to `RateAgreementPerThousand`, and the readings behind the first
solve carry a common error, so they agree with each other. No run length changes
that; three was tried and refuted in
`a-doubled-field-rate-agrees-with-itself.md`.

**Corroboration cannot reject it either.** `rateMoved()`'s periodic recheck is
the one thing that asks whether the HELD rate is still the source's, and it asks
by comparing a fresh reading against it at `RateCorroborationPerThousand`. That
tolerance is wider than the error, so the recheck confirms the wrong value for
as long as the source stands still.

**Narrowing the tolerance is not available.** The instrument quantises by one
source line -- 3.2 per thousand at 311 lines -- so a tolerance tight enough to
see a 1 per thousand error fires on a single missed edge instead, and every such
firing is a solve nobody asked for.

## What the engine does now

The first recheck of a boot **re-solves instead of corroborating**.
`VideoSourceAcquisition::rateMoved()` holds `firstRateConfirmed_`, forgets the
held rate and arms a move the first time the recheck falls due. One extra solve
per boot, `RateRecheckPasses` after the first -- ten seconds, by which time the
source has settled.

It is the RECHECK'S arm and not whatever reaches the rate arm first. A line
period that twitches early would spend the confirmation inside the window it
exists to outlast, and the solve taken there holds the same wrong rate.

Measured on two consecutive boots: `source moved: rate (311 lines, solved 311)`
at 18.7 s and 18.9 s from the unit dropping, three consecutive readings of 50.47
Hz after it, and the lock converging from `err 893791` to a steady 13650 instead
of walking.

## What it does not do

The first ten seconds of a boot still run on the boot's rate, so a picture
judged in that window is judged against a raster that is about to move, and the
correction blanks the output for the encoder relook as any solve does.

Nothing here bounds the error. A boot that lands on a doubled rate still sizes a
divider from it and still spends `HeldRateRejectionLimit` refusing correct
readings before the count-based path lets it out; the confirmation is what stops
whatever survives that from being kept for the life of the boot.
