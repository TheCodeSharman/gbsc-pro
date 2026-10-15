# The pulse ignore is a fraction of the line in ADC samples

`SP_H_PULSE_IGNOR` is compared against pulse widths the sync processor counts
in ADC samples, so one value in the register is a different fraction of the
line at every divider. The byte fitted at a 2200-sample line reads a serrated
source there and ignores the source's own sync pulse through the 1446-sample
line a 1080p raster affords a 59.94 Hz source. That is why the Wii at 480p on
`ypbpr` acquired at 720x480 and never at 1920x1080, and why the same source
deadlocked at the earlier 1400 reference.

## Where the constant was fitted, and where it lands

0x6b, 107, was measured on PAL 576i through a 2200-sample line: every smaller
value counts the serrations as lines and 0x90 ignores the sync pulse itself,
`the-pulse-ignore-value-is-measured-not-chosen.md`. As a fraction of that line
it is 4.9%, which sits between the equalising pulses of broadcast composite
sync at about 3.6% of the line and the sync pulse at about 7.2%.

The Wii at 480p has a sync duty of 7.33%, measured as the duty the engine logs
at each divider it has been solved to. Against the constant:

| divider | the output that chose it | sync pulse, ADC samples | 107 is | `STATUS_SYNC_PROC_VTOTAL` |
|---|---|---|---|---|
| 2046 | 1080p solved for a bogus 40 Hz | 150 | under the pulse | 524 |
| 1858 | 720x480 | 136 | under | 521 / 524 |
| 1446 | 1920x1080 at 59.94 Hz | **106** | **over** | 210..278, then 97..163 |
| 1400 | the reference that deadlocked 480i | 103 | **over** | 97..156 |

Everything else measured on the failing state follows from the separator
ignoring the sync pulse: the vertical count collapsing to about half, the input
formatter's vertical going quiet so the field rate reads 0.00, 25, 40 or 147 Hz
against a true 59.93, and the reference retry cycling for ever because the
reference clocks run the PLL at 436 or 864 samples a line, where a 7.3% pulse is
31 or 62 samples and even further under the threshold.

## The experiment that settled it

Retry state on `ypbpr` at 1920x1080, 48 s of `the reference clock measured
nothing`, with automation running. One register written by name,
`SP_H_PULSE_IGNOR` 107 to 16:

```
49.51  SP_H_PULSE_IGNOR 107 -> 16
50.00  measured: 456 lines x 59.93 Hz -> line rate 27392, clock 1446@15625 htotal 1198 UNLATCHED
50.41  measured: 524 lines x 59.93 Hz -> line rate 31468, clock 1446@15625 htotal 1197 UNLATCHED
50.47  installing: divider from line rate 31468, doubled 0 -> 1446
50.47  duty: 106 pulse / 1446 divider, htotal 1446, negative
50.47  source key: 524@59.94/733??, no framing stored, shape 13333
50.52  rate match: source 59939 mHz, output 59927 mHz
50.76  sampling phase: chosen, oversample 2
50.76  sync pad: driven
50.79  source absent: 278 lines, 1446 samples against divider 1446
```

Half a second from the write to a correct measurement through the same
reference clock that had measured nothing for 48 s, and the solve then ran to
the sync pad. The collapse at 50.79 is the engine's own dynamic pass, two passes
after acquisition, re-applying the constant 107.

The vendor firmware derived the field the same way the fix does: its SD
composite arm wrote `0.081 x HPERIOD_IF + 0.38 x PLLAD_MD x duty`, a sync width
in ADC samples scaled by the divider, which this fork had collapsed to the one
byte that derivation produced on one source.

## The search has to see the pulse through the chip's own clock

With the fraction alone in force, an input change from `vga` still cycled the
references for 45 s before a stray solve let the PLL lock. The samples say why:
while nothing is locked the sync processor counts at the 27 MHz reference or
half of it, `STATUS_SYNC_PROC_HTOTAL` 860 or 430 against either divider, and
the Wii's pulse reads 63 or 31 samples there -- under 70 and 99 alike. The
search therefore ignores only what no sync pulse can be, 16 samples, which is
what the experiment above wrote by hand. Measured on the next input change:
`measured: 271 lines x 59.93 Hz` 1.7 s after the selection and the solve 0.6 s
later, where the previous run took 45 s.

**And the fraction must wait for a latched state.** Written the moment the
count came into range, through a 2040 reference the PLL had not latched -- the
block counting 1187 samples a line, the pulse 87 -- the divider's 99 sat above
the pulse again, and the counts the solve then read were 271 and 431.

**So the latch is the whole of the condition, and the search is entered from a
latched state too.** A scattered count arms the hunt on a source the engine was
holding, the divider it solved still latched, and there the search's own value
is under the floor an unserrated vertical interval imposes: measured on `vga`,
the RISC PC at 800x600@60 with `SYNC 1` passed through to 1920x1080, one reading
of 171 lines took the threshold from 99 to 16 with `PLLAD_MD` 2038 against
`STATUS_SYNC_PROC_HTOTAL` 2038, and the counts from there were 512, 558, 559,
824, 1069 and 1185 against the source's 622 for the remaining 35 s of the
window. `the-engine-makes-the-composite-count-unmeasurable.md`.

## What the engine does now

`SyncProcessor::applyPulseIgnore(csync, divider)` writes
`divider x 49 / 1000` for a composite source, 107 at 2200 as measured, 99 at the
2040 reference and 70 at 1446, and every pulse ignored for a source carrying its
own vertical sync. Every entry point that writes the field takes the divider:
`applyForSyncType()`, `prepare()` and the `Dynamic` pass on a present source.

`applyForSearch()` takes the latch as well, from `Dynamic`, which
`VideoPath::samplingClockLatched()` supplies: the divider's fraction where the
block is counting that divider's line, and the search's own 16 where it is not.
`VideoPath::writePulseIgnore()` re-places the fraction beside the retime origin
when the solve installs its divider, and a reference install leaves whatever the
hunt chose.

## What it is not

- **Not the divider failing to latch.** 1446 latches, `duty: 106 pulse / 1446
  divider, htotal 1446`, on every boot measured.
- **Not a count in 27 MHz units.** A 480p sync pulse is 63 counts at 27 MHz,
  under 107 at every divider, and the count is right at 1858 and 2046.
- **Not the coast window, the clamp or the parked vertical blank.** None of them
  moved in the experiment above, and the one register that did was enough.

## What this does not settle

- One fraction serves every composite source only while its sync pulse is wider
  than 4.9% of the line. CEA's tri-level 720p pulse is 2.4%, and a composite
  source with a pulse that narrow would need the threshold placed from the
  measured duty instead. No such source is on the bench.
- `SourceMeasurement` still takes any line rate at a moved count, so a garbage
  pair read through a collapsed count becomes the held rate on one reading.
  `../known-issues.md` carries it.
