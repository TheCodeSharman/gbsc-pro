# The pulse-ignore fraction breaks a progressive source

`SP_H_PULSE_IGNOR` as one fraction of the line is refuted. The threshold must
be above the equalising pulse a SERRATED vertical interval carries and below the
source's own sync pulse, and on a progressive source those two bounds cross: a
progressive sync pulse is narrower than a broadcast equalising pulse, so the
fraction that reads a serrated source sits across the whole of it.

The engine therefore writes two values for a composite source, chosen by the
measured scan, and `the-pulse-ignore-is-a-fraction-of-the-line.md` is superseded
in that one respect -- the fraction remains, and is what every scan acquires on.

## The two bounds, and why no single value meets both

| | PAL 576i, divider 2200 | Wii 480p, divider 1446 |
|---|---|---|
| line | 64.0 us | 31.78 us |
| sync pulse, measured duty | 7.2%, 4.6 us, 158 samples | 7.33%, 2.33 us, 106 samples |
| equalising pulse | 2.35 us, 81 samples | none |
| value that reads the source | **107**, 3.1 us | **14**, 0.31 us |

107 sits between 576i's equalising pulse and its sync pulse, which is what makes
it right there: below it the serrations are counted as lines, measured as a
steady 314/315 against a true 310, and `the-pulse-ignore-value-is-measured-not-chosen.md`
carries the sweep. **A 480p sync pulse is narrower than the equalising pulse that
bound sits above**, so the same reasoning cannot place one value for both.

As a fraction of the source's own sync pulse the fraction is 66% on the Wii at
1446 and 67% on 576i at 2200 -- the same, because the fraction of the LINE and
the duty are both fixed. So the ratio is not what separates them, and no scaling
of one constant reaches both. The scan does.

## What 70 costs on the Wii at 480p

Our value at 1446, with everything else as the engine solves it, 1920x1080 out:

| | fraction, 70 | scan-chosen, 14 |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 524 with dips to 450/451/454 in 5..8% of samples | 524, steady over four reads two minutes apart |
| `STATUS_MISC_PLLAD_LOCK` | 59..78% | -- |
| emitted frames identical in a 40-frame burst | 2..24 of 40 | **40 of 40, four bursts** |
| field rate off `DEBUG_IN_PIN` | 43.6 or 40.0 Hz in 40% of 120 readings | -- |
| mean RGB over the capture band | 219.7 / 70.4 / 225.2 | 218.9 / 226.7 / 221.8 |

The engine amplifies each dip: one disagreeing count makes the source absent,
which resets the acquired run, forgets the sampling phase and keeps the frame
time lock from arming, so the picture shears in runs of frames rather than
dropping a line.

**The magenta cast went with it.** It was the unstable sampling and not a second
fault in the colour path: the green channel reads 70 against 219 and 225 while
the count dips and 227 beside them once it holds. What remains against the
vendor image is a LEVEL difference, not a tint -- `../known-issues.md`.

## The vendor image is the control, and it is instrumentable

The 1.3 tag's prebuilt image holds this source at 40/40 and cannot report a
register: its 310 `SerialM` prints are commented out and it has no read route.
Built from the tag with a `/getregs` route added -- the fork's wire form, so
`dump_registers.py` and `snapdiff.py` read it unchanged -- it still scores 40/40
before and after a full dump, so the snapshot is of the machine that passes.
`../bench-sources.md` has how to push it.

101 fields differ across all 1536 registers on this source. The two that bear on
the count:

| | ours | stock |
|---|---|---|
| `PLLAD_MD` | 1446, oversample 1, 1446 samples kept | 2558, oversample 2, 1279 kept |
| `SP_H_PULSE_IGNOR` | 70 | 14 |
| `STATUS_SYNC_PROC_HLOW_LEN` | 106 of 1446, 7.33% | 187 of 2558, 7.31% |

The divider difference is a deliberate divergence and not a fault: the kept
count is what carries the source's pixels, and ours keeps more of them. A frozen
single-register A/B at our own divider is what settles the threshold -- 14 alone
gives 40/40 and 70 alone breaks it, with the divider, the coast, the pulse width
difference, `SP_HS_LOOP_SEL`, `SP_H_PROTECT` and the vertical window all held.

## Where the scan comes from, and why it has to settle first

`SourceMeasurement::scanType()` answers progressive from the first sample, by
design -- deinterlacing a progressive source corrupts the picture where leaving
an interlaced one alone only combs it. A consumer that CONFIGURES the separator
cannot take that default: configuring an arriving interlaced source as
progressive scatters the count the measurement itself needs, so the answer would
never arrive.

`SourceMeasurement::scanKnown()` is what separates the two, and it is
`scanSteady_.settled()` -- false until `SteadySamples` counts agree, true after,
false again after `modeChanged()`. Unknown reads as the fraction, which is what
acquires 480i, 576i and 480p today.

`VideoSourceAcquisition::followDynamicFacts()` re-applies the dynamic pass the
pass the scan moves, alongside the searching state it already followed, rather
than waiting for the maintenance cadence 31 passes away.

## What this does not settle

- **An RGBHV composite source keeps the fraction**, being `pathSource`, where the
  physics says it wants the noise floor too. Measured indifferent across the
  range on the RISC PC at 800x600 with `SYNC 1` -- 107 and 2 both counting 623 --
  so nothing argues for moving it, and its sync is a logic-level pin rather than
  a level sliced off a picture-bearing channel.
- **The progressive value is a constant, not a fraction.** Nothing has measured
  a progressive composite source at a second divider, so there is no evidence to
  size a fraction from. The vendor's own fixed bytes fall as the line rate
  rises -- 14 for standards 3-4, 8 for 5, 6 for 6-7 -- which fits about 5.5 per
  thousand of its dividers, but its 14 on 576i is one of the values measured to
  count the serrations as lines, so the set is not a model to copy.
- **Why 66% of the pulse survives on a clean csync and not on sync sliced off
  luma.** A separator slicing a picture-bearing channel has a pulse width that
  moves with content; nothing has measured that width's spread.
