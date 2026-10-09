# The reference clock can deadlock the measurement

The engine installs a sampling clock only FROM a measurement, and the sync
processor measures in ADC clocks. So a reference clock the ADC PLL will not lock
to on the arriving source is a closed loop: nothing is measured, so no divider
is chosen, so nothing is ever measured. The recovery ladder cannot leave it,
because its `Reconfigure` act re-asserts the divider in force -- which on a
source that has never been measured is that same reference.

`the-reference-divider-was-the-bootstrap.md` records the same deadlock shape
from a PLL parked in reset. This is the shape surviving a reference clock that
is running.

## What it looks like

Measured on the Wii at 480i on `ypbpr`, selected from an acquired 800x600@60
RISC PC on `vga`. Deterministic -- three selections, three stalls, none of which
ended on its own:

```
 0.37  input selected: ypbpr, reference divider 1400, applied +62ms
 0.42  evt,det enter,0 / evt,det hsact,0
 0.84  DETECT: 455ms, syncFound 0
 0.85  LOWPOWER: entered (no sync found)
 1.83  DETECT: 24ms, syncFound 2
10.39  recovery: reconfigure after 10s
20.50  recovery: reset the blocks after 20s
30.39  recovery: move the input after 30s
40.39  recovery: reconfigure after 40s
```

**No `sampling:` line is printed at all**, for as long as it is left -- 60 s and
five acts in one run. Not a wrong reading; no reading. Polled every 3 s
throughout, by name:

| field | reading |
|---|---|
| `PLLAD_MD` | 1400, never moves |
| `STATUS_SYNC_PROC_HTOTAL` | 1419..1437, never within 2 of the divider |
| `STATUS_MISC_PLLAD_LOCK` | 0 at every sample |
| `STATUS_SYNC_PROC_VTOTAL` | 97..156 against the source's 525 |
| `VPERIOD_IF` | **524 at every sample** |
| `ADC_SOGCTRL` | 13, then 14 after the first reconfigure, then still |
| `SP_PRE_COAST` / `SP_POST_COAST` | 7 / 6 |
| `SP_SOG_MODE`, `SP_EXT_SYNC_SEL` | 1, 1 |

`VPERIOD_IF` holding the source's true frame all the way through is what rules
out the signal, the cable and the separator: the input formatter measures the
source correctly while the sync processor counts nothing, and the two differ
only in that one counts in ADC clocks.

## The divider is the cause, and the reset sequence is not

`/sampleclock` applies a divider AND runs the full reset sequence --
`resetVideoBlocks`, `SyncProcessor::reset`, `MemoryBus::restart`,
`Adc::restartPll`, `forgetPositions`, `outputUp`, both phases, `latch`. So a
recovery that cleared the stall could be either half. Separating them takes one
pair of runs on the stalled unit:

| applied | sampling lines | outcome |
|---|---|---|
| `md=1400&os=4` -- the SAME divider, whole reset sequence | 1 in 30 s, `213 lines x 17.41 Hz -> line rate 0` | still absent |
| `md=2048&os=4` | first at **0.11 s**, correct at **0.16 s** | **acquired at 1.64 s** |

**`/sampleclock?md=X` IS NOT THE GROUP A REFERENCE INSTALL AT X WRITES**, and
reading it as one makes two measurements look like a contradiction. The route
goes through the engine's chosen sampling, so the crossover row is read against
the HELD rate rather than against `BringUpLineRateHz` -- and in a stall the held
rate is the outgoing source's. The same divider therefore lands on a different
post divider depending on what the engine last believed:

| | post divider written |
|---|---|
| the reference install at 1400, against 15625 Hz | `PLLAD_KS` 2 |
| `/sampleclock?md=1400` with vga's 37879 Hz held | `KS 1` |
| `/sampleclock?md=2048` with the same held | `KS 3` |

So `known-issues.md` recording `md=1400` taking a 26 s stall to acquired in
1.9 s is not refuted by the row above: it is a different group on a different
held rate, and neither reading transfers to the other state.

```
0.40  sample clock applied: MD 2048 KS 3 CKOS 1 DEC2_BYPS 0 HSYNC_RST 0 HTOTAL 748 lock 0
0.51  sampling: 290 lines x 59.93 Hz -> line rate 17441
0.56  sampling: 259 lines x 59.96 Hz -> line rate 15591
0.68  sampling: rate 15591 doubled 1 -> divider 2200
1.64  sync pad: driven
1.64  scan: interlaced, count 260
```

**So the lever is the divider, and the reset sequence is refuted as one.** The
value 2048 is not the answer either -- it is not what the source wants. It only
let a measurement complete, and the engine then chose 2200 for itself 0.12 s
later and landed on the state a clean `ypbpr` acquisition holds: `PLLAD_MD`
2200, `STATUS_SYNC_PROC_HTOTAL` 2200, `VTOTAL` 260, `PLLAD_LOCK` 1,
`IF_HSYNC_RST` 1100.

**What IS refuted for this stall is reset-then-PLL-restart as the lever.**
`restartPll()` runs inside `applySampleRate()` on both branches, so every
`Reconfigure` act already restarts the PLL, and the row above restarts it after
a full block reset -- `resetVideoBlocks`, `SyncProcessor::reset`,
`MemoryBus::restart`, both phases, `latch` -- and changes nothing. The ordering
of the resets against the restart is not what this stall turns on.

## Why one reference is not enough

The reference pair is read for its crossover row against `BringUpLineRateHz`,
and the actual CKO is the divider times the source's real line rate:

| divider | source | CKO | post divider | VCO | gain | measures |
|---|---|---|---|---|---|---|
| 1400 | 800x600@60 on `vga`, 37.9 kHz | 53.0 MHz | /2 | 106 MHz | low | yes |
| 1400 | Wii 480p, 31.5 kHz | 44.1 MHz | /2 | 88.1 MHz | low | yes |
| 1400 | Wii 480i, 15.7 kHz | 22.0 MHz | /4 | 88.1 MHz | low | **no** |

The VCO is the same 88 MHz in the two Wii rows, so the VCO frequency alone does
not separate them, and what does is not established. What is established is that
one stated pair does not cover the sources the board carries, and that the
regime was never measured: the commit that sized the reference at 1400 swept
37.9, 45.0, 67.6 and 75.0 kHz and recorded `ypbpr` acquiring in 5.3..5.4 s with
a solved divider of 1448 -- the Wii in 480p. Nothing below 31 kHz on a
sync-on-green input was in it, and 480i is that case.

**Raising the reference instead is bounded, and the bound is why it was lowered.**
The input formatter's line counter is eleven bits, so an undoubled reference may
not exceed 2047 -- `the-line-counters-are-eleven-bits-measured.md` -- and high
VCO gain needs CKO past 32.5 MHz at 15625 Hz, which is a divider of 2080. The
two constraints do not overlap. A reference of 2506 does reach it, which is what
the bootstrap page measured as the state the unit locks in, but only with the
reference scan doubled, which is what was given up to stop the arriving source's
scan being one the reference could not represent.

## Which reference can measure which source

Asked end to end, through the real selection path, two trials a cell. The
discriminator is the escape itself: a leg that acquires inside the 10 s budget
was measured through the FIRST reference, and one landing at 11-13 s means the
first failed and the escape to the second rescued it.

| source | 1400 first | 2040 first |
|---|---|---|
| ypbpr 480i, sync on green | **11.5, 11.6 s** -- rescued | **2.3, 2.6 s** |
| vga 800x600@60, 37.9 kHz | 5.6, 5.8 s | 5.5, 6.3 s |
| vga 640x480@60, 31.5 kHz | 4.7 s | 4.9, 4.6 s |
| vga 320x256@50, 15.6 kHz | 2.9, 3.2 s | 6.3, 5.7 s |

**2040 measures every source the bench carries and 1400 is the only one that
fails any of them**, so the deadlock is answered by the reference's VALUE and not
by retrying: the interlaced sync-on-green source goes from never acquiring, to
11.5 s behind the escape, to **2.3 s** with nothing recovered at all.

**THE PREDICTION THAT A HIGH DIVIDER WOULD FAIL THE FAST SOURCES IS REFUTED, and
it was the reason not to try one.** The crossover row is read against the assumed
15625 Hz while the actual CKO is the divider times the real line rate, so a
37.9 kHz source at 2040 runs 77.3 MHz against a /4 sized for 31.9 MHz -- a VCO
far outside its range on paper. It measures anyway, in the same time 1400 takes,
because the PLL locks to every kth hsync and
`SourceMeasurement::measureSourceLinesCorrected()` recovers k. The arithmetic
that makes a divider look unusable describes the LOCK, and the count correction
is what makes the measurement survive losing it.

The cost is at the other end: 320x256@50 goes from 2.9 to 6.0 s. Both are inside
the budget, and a source that never acquired is worth two seconds on one that
always did.

## How to ask this question, because two ways do not work

**`/sampleclock?md=X` cannot answer it.** It installs through the engine's chosen
sampling, so the crossover row comes from the HELD rate rather than from
`BringUpLineRateHz`: the same 1400 lands on `PLLAD_KS` 1 in a stall holding
vga's 37879 Hz where a reference install holds `KS` 2. `/refclock?md=` installs
one as a reference, which is the only way to compare candidates.

**Scraping the console for a `sampling:` line gives false negatives.** The
console drops bursts and the route is queued for `loop()`, which during
detection sits in waits of seconds -- so a cell scored silent while the unit was
in that very configuration, acquired, with `STATUS_SYNC_PROC_HTOTAL` equal to
the divider.

**Installing a candidate while the source is already acquired tests nothing.**
The engine never leaves `acquired`, so the probe reports success without a
measurement having been taken through the new clock. Judge a candidate by
selecting the input with it already in force, and let acquisition be the proof:
acquiring REQUIRES a measurement.

## What the engine does now

`Tv5725::Adc` holds the reference clocks as a sequence rather than one pair.
A selection installs the first. `VideoPath::restartSamplingClock()` -- the
`Reconfigure` act's last step -- installs the NEXT one where a reference is
still in force, and re-asserts the divider in force where a measurement chose
it. The sequence cycles, so a source the first reference suits gets it back.

Whether the clock in force is a reference is held rather than inferred:
`Adc` keeps the divider its last reference install put there, so re-applying the
divider in force -- which a mode change does -- still reads as a reference,
while a solved divider does not.

**The escape is what is general here, not either value.** A reference that fails
on some source is a property of having one stated pair, and the measured fact
behind it is that CHANGING the divider is what restarts the measurement, after
which the engine solves the right one unaided in about a second. The bench needs
no second candidate now that the first measures all of it -- which is exactly
why the list stays: the next source to arrive is not on this bench, and a
reference it cannot be measured through would otherwise be terminal.
