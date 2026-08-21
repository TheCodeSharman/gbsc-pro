# The sample clock deadlocks the engine when it is inherited rather than computed

**Status:** settled. `Tv5725::Adc` computes every bit `PLLAD_LAT` loads; this
page is what the symptom looks like from the outside, and which three
explanations of it are wrong.

## The mechanism

`PLLAD_LAT`'s rising edge loads **MD, ND, KS, CKOS and ICP together**. A caller
that writes MD and latches therefore installs four values it never chose —
whatever the chip was holding.

`PLLAD_KS` is a VCO post divider selected by the CKO frequency, and RD-5725-1.1
gives the crossover rows: `00` over 162–80 MHz, `01` over 80–40, `10` over
40–20, `11` below. CKO is `divider x lineRate`, **not** the oversampled rate the
ADC then runs at — 2250 samples on a 15574 Hz line is 35.0 MHz, the 40–20 MHz
row, `KS 2`.

Choosing `KS` from the source's *mode* instead of its *clock* selects the wrong
row, and everything downstream follows:

```
KS 1 for a 35.0 MHz clock
  -> the oversampling wiring reads that KS and drops 4x to 1x
  -> CKOS, ADC_CLK_ICLK2X, DEC1_BYPS all follow it down
  -> the ADC clock is wrong
  -> the sync processor, which counts in ADC CLOCKS, reports garbage
  -> the settle gate never passes
  -> the pass that would re-latch the PLL never runs
```

The last step is what makes it a deadlock rather than a transient: the engine
refuses to solve against an unsteady line count, and the line count cannot
steady until the engine solves.

## What it looks like from outside

A forced preset load, then permanently:

| | working | deadlocked |
|---|---|---|
| `STATUS_SYNC_PROC_HTOTAL` | 2250, echoing `PLLAD_MD` | 3063, and swinging 414–3063 |
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 256–278 on the same 311-line source |
| `IF_HSYNC_RST`, `SP_RT_HS_SP` | written every mode change | never written again |

**The picture is not obviously broken and nothing reports an error.** A
register dump of a wedged unit against a working one differs in 21 fields, and
every one of them follows from `KS`.

Only a forced re-detection recovers it, because that path rewrites the PLL and
latches.

## Three explanations that are wrong

Each was tested on the bench and refuted, so none needs proposing again.

**`SP_H_CST_SP` is not the cause.** It reads 256 working and 1661 deadlocked,
which makes it the most conspicuous difference in the sync processor's own
configuration, and `1716 x 0.968 = 1661` identifies the writer exactly — the
coast position, computed from an averaged `STATUS_SYNC_PROC_HTOTAL` that hit the
`>= 2040 -> 1716` clamp. It is a **symptom**: writing 256 back, with nothing
else changed, leaves the measurement exactly as broken.

**`SP_RT_HS_SP` is not the cause either.** It is the retime stop, it is part of
what a mode change writes, and it holds the wrong value while deadlocked — but
it reads the *same* value in a healthy state, so it does not distinguish them.

**Re-latching the divider is not enough.** Writing `PLLAD_MD` and pulsing
`PLLAD_LAT` — which is the whole of a naive "re-assert the sample rate" — leaves
the measurement swinging 404–2973. The latch loads `KS` and `CKOS` from their
registers, so re-latching the wrong ones changes nothing. `KS` and `CKOS`
together recover it instantly and completely; `ICP` is not involved at all.

## Why hardening the settle detection does not fix it

The tempting reading of the jitter is that the measurement is noisy and the gate
is too strict. It is not noisy — it is **wrong**. The readings cluster around
256–278 on a source running 311, so a tolerance band, a median or a longer
window either still never passes (correct, and still deadlocked) or accepts a
line count that is off by a fifth and solves a raster and a divider from it.

The measurement is impossible until the clock is right. No amount of looking
harder at it helps.

## The part that generalises

The engine's rule is that registers are an output and never an input. A
function that latches a group while writing one member of it breaks that rule
invisibly: the write looks complete, the register reads back correct, and the
inherited members are the ones that decide what the chip actually does.

**Where a latch loads several registers, one owner writes all of them, in the
same call, before the edge.**
