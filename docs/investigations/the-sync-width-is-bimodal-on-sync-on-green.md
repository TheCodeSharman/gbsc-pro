# The sync width is bimodal on sync on green, and one reading decides the duty

`SourceMeasurement::readSource()` takes ONE reading of the hsync width per mode
change, guarded only by the divider being latched and the ratio being inside
`HsyncPulse`'s band. On the Wii at 480p on `ypbpr` that register is bimodal, so
the duty — and with it the source key and the capture origin — is whichever
sample the solve happened to land on.

## What the register does

Sampled with `Tv5725::SamplingLog` at 25 ms from inside `loop()`, 1143 rows over
two captures on separate acquisitions, divider 1858:

| `STATUS_SYNC_PROC_HLOW_LEN` | share | runs |
|---|---|---|
| 135 / 136 | 78.5% | n=83, mean 6.30, max 9 |
| 163..166 | 20.9% | n=79, mean 1.76, max 4 |

135 is the correct reading: 480p's sync is 62 of 858 pixels, and
`0.0723 x 1858 = 134.3`. The excursion is 23% wide and lasts up to 100 ms.

**NO REGISTER BESIDE IT SEES THE EXCURSION.** `STATUS_SYNC_PROC_HTOTAL` equals
the divider in **100% of rows in both bands** — 523 of 523 in the band and 139
of 139 in the excursion — so `Adc::dividerLatched()` passes every one of them.
That register echoes `PLLAD_MD` rather than measuring anything, which is why it
cannot: the width is counted in ADC clocks and the echo is not.

`STATUS_MISC_PLLAD_LOCK` reads 0 in **231 of 231** excursion rows and in 21% of
band rows. That is corroboration and not a discriminator the engine may use:
the bit is a duty cycle, and `pllad-lock-is-a-duty-cycle-not-a-state.md` settles
that it may corroborate a reading and never refuse one.

## What it costs

The duty reaches three places, and all three move with it.

**The source key never repeats.** `SourceKey` carries the width at
`SyncWidthIdentity`, 0.002 of the line. Three consecutive solves of a motionless
source:

```
source key: 521@59.94/877??, no framing stored
source key: 521@59.94/377??, no framing stored
source key: 521@59.94/732??, no framing stored
```

877 and 732 are the two bands; 377 is a third reading the register produces. The
spread is 7 to 17 times the tolerance, so no stored framing is ever recalled and
none can usefully be stored.

**The capture window moves between acquisitions.** `VideoSourceLine::forDuty()`
sizes the window's start from the duty and `SyncProcessor::retimeStopFor()`
places the counter's origin from it. Three consecutive boots, `/geometry`:

```
fh=137   fh=70   fh=164
```

## Both obvious fixes are measured and neither works

The duty is the one measured fact taken from a single sample — the count and the
field rate each wait for a run — so a steadiness run is the obvious fix. It has
two forms and the bench refuses both.

**A run that WAITS costs a divider re-install, and the source does not survive
it.** `measureDuty()` releasing `rateTaken_` on every pass has the next pass
measure the rate again; on a source whose count dithers that yields a different
rate, so `installSampling()` picks a different divider and re-latches the ADC
PLL under the measurement. The boot log of such a build:

```
measured: 524 lines ... installing: divider ... -> 1854
duty: 135 pulse / 1854 divider, SETTLING
measured: 520 lines ... installing: divider ... -> 1856
duty: 68 pulse / 1856 divider, SETTLING
measured: 265 lines ... installing: divider ... -> 1854
duty: 87 pulse / 1854 divider, UNLOCKED
```

Measured either side on the same bench minutes apart, `hlow` inside 130..190 as
the liveness guard: **99.4% of rows live on the shipped build, 10.0% and 1.2%
with the wait**. The duty itself is fixed — `fh` reads 137 on five boots of five,
and every excursion prints `SETTLING` and is refused — but the source stops
acquiring.

**A run that does NOT wait fixes it only sometimes.** Filling the run from the
passes the rate already spends costs no pass at all, and liveness is unaffected
— 98.2% against 99.4%, with the count distribution unchanged (521 at 73.4%
against 75.7%). But a duty pass reached before the run has settled has nothing
better than its own reading, and `fh` then reads 137, **165**, 136 across three
boots: the excursion still lands.

## What has to change first

A pass of the mode-change window costs a rate re-measurement and a divider
re-install, so no measurement here can afford to take more than one pass. That
is the thing to fix, and it is upstream of the duty: the same loop re-installs
dividers on a dithering count, which is what the rate re-solve and the terminal
reference-retry cycle are made of.

With a pass made free, the waiting form lands as written — it is measured to fix
the duty completely and its only cost was the re-install.

The patch for the waiting form is not in the tree, having been measured to
regress acquisition.
