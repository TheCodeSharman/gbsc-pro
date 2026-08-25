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
| `SP_H_PULSE_IGNOR`, the coast widening on sync loss, the SOG level step, `SP_DIS_SUB_COAST` | a serrated line: low line rate **and** csync |
| the HD bypass htotal doubling, its blanking offset, its line-count measurement | the line rate alone |

`Tv5725::SourceMeasurement::lowLineRate()` answers the second from the held line
rate. The first is that and `rto->syncTypeCsync`.

## Why the rate is held rather than measured on demand

Three of the readers run during a sync loss, when nothing is measurable.
`lineRateHz()` is the last rate MEASURED and a refusal clears it;
`heldLineRateHz()` is the last one that passed the cross-check against the line
count, so it survives the refusal. Unmeasured answers false, so a source that
has never arrived is configured for nothing.
