# The duty is counted before the processor relocks

**The reference divider this record is written around is gone.** It was the
bootstrap rather than a reference, and what replaced it, along with the duty
error the two clocks were causing, is
`the-reference-divider-was-the-bootstrap.md`. The measurements below stand.

`STATUS_SYNC_PROC_HLOW_LEN` divided by the divider in force is the hsync duty,
and the capture window is placed from it. Taken without asking whether the sync
processor was counting the line that divider describes, the result is arbitrary
rather than imprecise — and the framing is stored as proportions of a region
that duty places, so a source that never moved gets framed differently depending
on where it had been.

## The measurement

Measured on the bench mode, RiscPC 320x256@50 on `vga`, same cable, with the
live duty reading 7.06% (`HLOW_LEN` 177 / `PLLAD_MD` 2506) throughout:

| arriving from | capturable region |
|---|---|
| 640x480@60 | 1147 |
| 320x256@70 | 1109 |

Reproduced in both directions. 38 units on a region of about 1145.

## What separates a reading worth having

`STATUS_SYNC_PROC_HTOTAL` counts real ADC clocks per line, so while the block is
locked it echoes the divider in force. Eight source modes in one sweep, 36
samples of `(low, divider, htotal)` taken as the console prints them:

| \|htotal − divider\| | samples | duty error against the mode's |
|---|---|---|
| ≤ 1 | 26 | −0.24 to +0.18 points |
| ≥ 102 | 10 | −5.64 to +3.25 points |

Nothing lands between 1 and 102. `Adc::dividerLatched()` already answers it at a
tolerance of 2, and the log line already printed both sides of the ratio without
anything acting on them.

**The failure is confined to the 15.6 kHz reference divider.** Every garbage
sample was taken at 2250 and every good one at 1124. At 2250 the count swings
between about 41 and about 225 while htotal reads 2098..2406, all within a few
hundred milliseconds.

## The obvious test does not work

Successive samples agreeing selects the fault rather than rejecting it. Measured
in one transition: 225, 225, 225 across three consecutive passes, while htotal
read 2148, 2403, 2406. It is the same trap `HPERIOD_IF` has, for the same
reason — a register that is wrong and still is wrong passes a stability check
perfectly.

`STATUS_SYNC_PROC_HSACT` reads 1 right through the garbage, and
`STATUS_IF_HT_OK` is not a validity signal either.

## Three rules, and any two of them together break something

- **An unlocked reading never replaces one a locked pass took.** It carries
  nothing.
- **It only holds the solve back while nothing has been measured at all.**
  Blocking on every later unlocked pass leaves a source that measured once and
  then lost the count unable to solve for ever.

  **The reason first given for that — that the recovery ladder disturbs the sync
  path the wait depends on — is refuted.** The ladder is not what stops the
  count agreeing: in a console capture of the failing window the unlocked
  readings begin at 5.88 s and the first ladder rung fires at 7.04 s, more than
  a second of failure before recovery touches anything. The cause is below,
  under *Why the count never agreed at 2250*.
- **When the floor under that wait expires, the fallback is taken rather than
  the reading.** An unlocked count lands PLAUSIBLE — 9.96% on a source whose
  duty is 7.03% — and `VideoSourceLine::forDuty()` accepts it as real, where a
  duty of nothing is refused and becomes `FallbackDuty`.

## After

Arriving at 320x256@50 from five different source modes, the capturable region
reads within one unit across all five, against 38 before.

## Why the count never agreed at 2250

**The ADC PLL was not locking at the reference divider, and the reason is
arithmetic.** `Adc::applySampleRate()` picks the post divider from
`divider x lineRate` against rows at 80/40/20 MHz, and the VCO gain from the CKO
shifted up by it. The rate it was handed came from `estimatedLineRateHz()`,
which answered `lineCount x NominalFieldRateHz` — a stand-in of 60 — in
preference to the rate already measured:

| rate used | CKO at 2250 | post divider row | VCO | gain |
|---|---|---|---|---|
| nominal, 311 x 60 = 18660 | 42.0 MHz | ≥40 → **1** | 84 MHz | **0** |
| measured, 312 x 50.08 = 15625 | 35.0 MHz | <40, ≥20 → **2** | 141 MHz | **1** |

So on any 50 Hz source both the post divider and the VCO gain were wrong, and
the PLL was configured for a VCO half the one the hardware has to run. Measured
with `Tv5725::SamplingLog` through a change into the bench mode:

```
 ms    divider  lock  sp_vtotal  sp_htotal
8695      2250     0        311       2348
8815      2250     0        311       2408
9555      2250     0        311       2152
9694      2250     0        311       2093
10076     2506     1        311       2506
```

`STATUS_MISC_PLLAD_LOCK` reads 0 on **every** sample at 2250 and 1 on the first
sample at the operating divider. `sp_vtotal` is 311 and `HPERIOD_IF` 431
throughout, so the source is not the problem. The ±98 and ±157 offsets are the
free-running VCO beating against the line.

**And the state had no exit.** Because the count settles at once, the nominal
product never *moved*, so the guard keyed on it never re-applied the clock. Held
at 2250 through the solve path instead, which uses the measured rate, the same
divider locks: `PLLAD_LOCK` 1 and `htotal` 2250 in 14 of 14 samples. Same
divider, same source, opposite outcome — the row is what differs.

## There is no circularity to bootstrap out of

The reason a nominal rate was used at all is that the reference clock appeared to
come before any measurement. It does not: **both readings behind the real rate
survive a clock the PLL cannot hold.**

- `STATUS_SYNC_PROC_VTOTAL` read the source's 311 at four dividers written by
  hand that the PLL could not lock to, with `htotal` meaningless beside it.
- The field rate read 50.08 Hz right through the unlocked window, on **both**
  test buses — see
  [`field-rate-measured-downstream.md`](field-rate-measured-downstream.md),
  whose concern does not reach this state.

So the rate is measured first and the clock is installed **once**, sized for it.
`applyReferenceSampling()` installs nothing while no rate has been measured, and
re-applies only when the measured rate moves by more than
`VideoSignal::ratesAgree` allows — an exact test re-latches the PLL on every
pass, which restarts the settle and leaves a source that never finishes
measuring.

## A latched clock still needs a settle, and it is 82 ms

Separate from the row, nothing read in the pass that latched a clock means
anything: the PLL has to relock and the sync processor to count one line at the
new rate. Measured with `SamplingLog` across a source mode change,
`STATUS_SYNC_PROC_HTOTAL` took **82 ms** to echo a newly latched divider,
reading 808 against 1124 in between — four passes at the detection cadence, so
`LatchSettlePasses` is five and the whole pass is gated on it rather than the
duty alone.

**`STATUS_MISC_PLLAD_LOCK` is not what that waits on.** On a settled source it
dithers: 136 transitions in 1109 samples, 77 of them isolated single zeros, with
the count exact throughout. Gating on it would refuse about 7% of good readings
at random. It never lied in the unsafe direction — 0 samples with the bit set
and the count adrift — so it can corroborate a reading and can never refuse one.

## After

The duty is taken at the reference divider **during** the change, 160/2250 =
7.111%, `htotal` 2250, with no unlocked pass at all. Arriving at the bench mode
from five source modes the capturable region reads 1145 in four and 1146 in the
fifth — one count of `HLOW_LEN` apart, 7.067% against 7.111%.

**This no longer blocks putting the duty in `SourceKey`**: the duty is in hand
before the solve rather than after it, and the spread is 0.044 points against
the half-point bins a key would use.
