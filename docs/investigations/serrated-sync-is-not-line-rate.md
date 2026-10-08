# A 15 kHz line and a serrated one are two facts

`rto->videoStandardInput == 1 || == 2` carried both at once: *NTSC-like or
PAL-like* meant a 15.7 kHz line **and** a broadcast vertical interval with
equalisation and serration pulses. Every source the enumeration was written for
had both, so nothing separated them.

A programmable RGBHV source has the first without the second. The bench RiscPC
runs 320x256@50 — a 15.6 kHz line, separate H and V sync, no serration.

## What the conflation costs

`SP_DIS_SUB_COAST` is disabled for an SD source once the coast position is set.
Measured on the bench source, with the bit written by hand and every other
register unchanged:

| `SP_DIS_SUB_COAST` | `STATUS_SYNC_PROC_HTOTAL`, 8 samples | `PLLAD_MD` |
|---|---|---|
| 0 | 3198, 3225, 3129, 3251, 3116, 3225, 3236, 3128 | 2250 |
| 1 | 2250 x 8, exact | 2250 |

`STATUS_SYNC_PROC_VTOTAL` holds 311 and `HPERIOD_IF` holds 431 throughout, so
the horizontal count is the only reading that moves. It is also the one witness
that the ADC divider latched, which is why a wrong value there is expensive:
locked, it equals `PLLAD_MD` exactly.

Sub-coast suppresses the sync processor's horizontal counting through the
serrated part of the vertical interval. On a source with no serration there is
nothing to suppress, and disabling it lets the vertical sync edges into the
horizontal count.

## The split

The line rate is measured; whether the interval is serrated is not, and no
register reports it. What does stand in for it is the sync type, which
`applyPresets()` already probes per source by asking whether a V sync line
arrives — [`sync-type-selection.md`](../sync-type-selection.md).

So the seven readers of the old predicate divide by which fact they need:

| reader | fact |
|---|---|
| `SP_H_PULSE_IGNOR`, `SP_DIS_SUB_COAST` | the sync type alone |
| the HD bypass htotal doubling, its blanking offset, its line-count measurement | the line rate alone |

`Tv5725::SourceMeasurement::lowLineRate()` answers the second from the held line
rate. **Nothing reads the two together any more.** The coast widening and the
SOG level step went with the recovery rungs that were their only callers, and
the two that remain are a pure function of the sync type, so the compound
predicate is gone.

## RETRACTED: an unmeasured source must not be configured for nothing

That was the conclusion here — *unmeasured answers false, so a source that has
never arrived is configured for nothing* — and it deadlocked a sync-on-green
source. The sub-coast is what lets the sync processor count a serrated source at
all, so withholding it until a measurement arrives withholds it until a
measurement that cannot be taken without it. A SELECTION has no measurement of
the source arriving, so it applied the OUTGOING source's answer.

The case this page never measured is an unserrated composite source WITH the
sub-coast, which is the bench RISC PC on `SYNC 1`. At 800x600@60, the one field
written by name and everything else untouched: `STATUS_SYNC_PROC_VTOTAL` 623 and
`STATUS_SYNC_PROC_HTOTAL` 1438 against a 1438 divider, lock held, acquired
throughout fourteen seconds. **Indifferent.** So there is nothing for the line
rate to protect.

The separate-sync measurement at the head of this page is untouched by that and
is what keeps the sub-coast off that path.
[`the-sub-coast-waited-on-a-measurement-it-made-possible.md`](the-sub-coast-waited-on-a-measurement-it-made-possible.md).
