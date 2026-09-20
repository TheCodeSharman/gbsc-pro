# The ADC PLL's VCO gain follows the VCO, and nothing was deriving it

`PLLAD_FS` selects the ADC PLL's VCO gain. RD-5725-1.1 documents the bit as
*"0: default, 1: high gain selected"* and gives no band for it, so every writer
in the firmware carried a literal. `Adc::applySampleRate()` picked the crossover
row from the clock and left the gain alone, and `Adc::applyForBypassRgbhv()`
froze it at 0.

**At a VCO above about 140 MHz that does not lock**, and the symptom is a black
screen with every register self-consistent: `PLLAD_MD` reads the divider that was
written, `STATUS_MISC_PLLAD_LOCK` reads 0 and `STATUS_SYNC_PROC_HTOTAL` sits a
few tens of counts below the divider.

## Measured

Bench RiscPC on `vga` at 800x600@60, 37879 Hz line, pass-through. The divider was
walked with `/sampleclock?md=N&os=1`, which writes the whole group and restarts
the PLL, then `PLLAD_FS` was set by hand and latched. Lock sampled three times at
each point.

| CKO MHz | `PLLAD_KS` | VCO MHz | gain 0 | gain 1 |
|---|---|---|---|---|
| 22.7 | 2 | 90.9 | **lock** | no lock |
| 30.3 | 2 | 121.2 | lock | lock |
| 36.0 | 2 | 143.9 | no lock | **lock** |
| 39.8 | 2 | 159.1 | no lock | **lock** |
| 45.5 | 1 | 90.9 | **lock** | no lock |
| 53.0 | 1 | 106.1 | **lock** | no lock |
| 56.4 | 1 | 112.9 | lock | 2 of 3 |
| 60.6 | 1 | 121.2 | lock | lock |
| 64.4 | 1 | 128.8 | lock | lock |
| 68.2 | 1 | 136.4 | lock | lock |
| 72.0 | 1 | 143.9 | no lock | **lock** |
| 77.2 | 1 | 154.5 | no lock | **lock** |

**IT IS THE VCO AND NOT CKO, AND NOT THE DIVIDER.** 90.9 MHz appears at post
divider 2 and at post divider 1, from dividers of 600 and 1200, and both need
gain 0. 143.9 MHz appears at both post dividers, from 950 and 1900, and both need
gain 1. Read against CKO the pairs contradict each other; read against the VCO
they agree.

That is what the post divider is for: RD-5725-1.1's own crossover table
(`PLLAD_KS` — 162..80 MHz at /1, 80..40 at /2, 40..20 at /4, 20..min at /8) keeps
the VCO inside one band whatever CKO the divider asks for, so
`VCO = CKO x 2^PLLAD_KS`.

Gain 0 locks up to 136.4 MHz and fails by 143.9. Gain 1 locks down to 121.2 MHz,
is marginal at 112.9 and fails by 106.1. **Both lock between 121 and 136**, so a
threshold inside that overlap is a choice rather than a boundary;
`Adc::HighVcoGainAboveHz` is 130 MHz, its middle.

## What it cost, and what was covering for it

Pass-through was reaching `Adc::applyForBypassRgbhv()`'s frozen gain of 0 at
VCO 154.5 MHz, which does not lock. What rescued it was a second owner: the sync
watcher steered `PLLAD_KS`, `PLLAD_FS` and `PLLAD_ICP` from a band table keyed on
`getPllRate()`, a test-bus measurement, and that table happened to ask for gain 1.
It ran a few hundred milliseconds after the switch, so the entry appeared to work
while writing a group that never locked on its own.

Two things hid behind it.

- **The oversampling ratio pass-through runs at.** The band steer asked for 4,
  clamped to 2 by the post divider, so pass-through ran at ratio 2 while
  `HdBypass::BypassOversample` said 1. Ratio 1 could not be tested, because the
  only way to reach it was to write the group without the steer following — which
  left the PLL unlocked and the screen black. With the gain derived, ratio 1
  locks first time.
- **The ratio looked like the cause.** Applying ratio 2 with the frozen gain
  (`/sampleclock?md=2039&os=2`) does not lock either, and setting `PLLAD_FS` to 1
  with nothing else moved locks at once. The gain is the whole of it.

`Adc::applySampleRate()` writes the gain beside the divider and the row now, from
the VCO the two imply, and the band steer is gone with its table.

## The scaling path was already running the gain the rule gives

The rule is derived from a pass-through sweep, so it is worth saying what it
does to the other path. Bench RiscPC at 320x256@50 on `vga`, scaling, after the
change:

    PLLAD_MD                 2208     CKO 34.5 MHz
    PLLAD_KS                    2     VCO 138.0 MHz
    PLLAD_FS                    1     what vcoGainFor() gives above 130 MHz
    STATUS_MISC_PLLAD_LOCK      1     in 8 of 8 samples
    STATUS_SYNC_PROC_HTOTAL  2208     the divider latched
    HPERIOD_IF                431     the value 311 lines at 50 Hz is due

**Unchanged, and it was luck rather than a derivation.** Nothing on the scaling
path chose the gain: `Adc::init()` writes 1 at bring-up and the only other
writer for this source is `SourceStandard::apply()`'s standard-8 arm, which this
one does not reach. The bench source happens to sit at 138.0 MHz, eight above
the threshold -- so a source a little slower would have run on the bring-up's
literal with nothing to say whether it suited.

## What this does not show

- **Where the boundaries actually are.** The sweep has 7.5 MHz steps and the
  crossings are bracketed, not found: gain 0 fails somewhere in 136.4..143.9 and
  gain 1 somewhere in 106.1..121.2.
- **Anything about `PLLAD_ICP`.** The charge pump was 4 throughout the sweep and
  is not what separates the two columns.
- **The other five writers of `PLLAD_FS`.** `Adc::init()`, `setResetParameters()`,
  `SourceStandard::apply()` for standard 8, `HdBypass::applyHd()` for standards 5
  and 7, and `HdBypass::applyRgbhvPll()` for standard 13 all still write literals.
  Standards 5, 6, 7 and 13 are unexercised on this bench, so what their arms
  freeze is not checkable here either way.
