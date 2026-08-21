# ADC PLL lock range

Bench source 320x256@50, VTOTAL 311, line rate 15575 Hz, automation frozen.

**The lock range is a property of the VCO frequency, not of `PLLAD_MD`.** The
divider alone cannot predict lock, because `PLLAD_KS` divides the VCO down to
CKO and so decides what the VCO runs at for a given divider:

```
CKO = PLLAD_MD x line rate
VCO = CKO x 2^PLLAD_KS
```

`Adc::applySampleRate()` picks `KS` from a crossover table on CKO -- 0 above
80 MHz, 1 above 40, 2 above 20, 3 below -- so a divider swept through those
boundaries changes `KS` underneath the sweep, and the lock bit moves with the
VCO rather than with the divider.

## The same VCO reached two ways

`PLLAD_MD`, `KS` and `CKOS` written and latched together, then
`STATUS_MISC_PLLAD_LOCK` sampled 60 times per point. The two walks share no
divider at all -- `KS` 3 spans `PLLAD_MD` 803..1445 and `KS` 2 spans 1605..2889 --
so a divider-shaped effect cannot put an edge in the same place in both.

| VCO | `KS` 3 | lock | `KS` 2 | lock |
|---|---|---|---|---|
| 100 MHz | MD 803 | 0% | MD 1605 | 0% |
| 110 MHz | MD 883 | 0% | MD 1766 | 0% |
| 120 MHz | MD 963 | 98% | MD 1926 | 95% |
| 130 MHz | MD 1043 | 85% | MD 2087 | 95% |
| 140 MHz | MD 1124 | 98% | MD 2247 | 98% |
| 150 MHz | MD 1204 | 100% | MD 2408 | 100% |
| 160 MHz | MD 1284 | 100% | MD 2568 | 97% |
| 170 MHz | MD 1364 | 100% | MD 2729 | 98% |
| 180 MHz | MD 1445 | 100% | MD 2889 | 100% |

Both walks put the edge **between 110 and 120 MHz**, and both are solid from
150 MHz up. `STATUS_SYNC_PROC_HTOTAL` echoes the divider at every point in both,
locked or not.

## The refuted model, and why it was convincing

An earlier sweep wrote `PLLAD_MD` directly and left `KS` at 2 throughout. Read
as a statement about the divider it gives a single clean edge between 1792 and
1856, and a trap: that 1124 -- the divider the write limit produces for a
non-line-doubled SD source -- sits far below it and can never lock.

Recomputed as VCO, that sweep is one row of the table above and agrees with it
exactly:

| `PLLAD_MD` at `KS` 2 | VCO | lock |
|---|---|---|
| 960..1792 | 59.8..111.6 MHz | clear |
| 1856..2816 | 115.6..175.4 MHz | set |
| 2880 | 179.4 MHz | clear -- a dropout inside the span |
| 2944 | 183.4 MHz | set |
| 3008 | 187.7 MHz | unit left the network here |

**The trap does not exist as stated.** The firmware never writes a divider
without also writing the `KS` that goes with it, and 1124 through
`applySampleRate()` selects `KS` 3, which puts the VCO at 140 MHz and locks --
measured at 98% of 60 samples. A divider is only unreachable if the `KS` its CKO
selects leaves the VCO under the edge.

What survives from that sweep untouched, because neither depends on the PLL
locking:

- Between CKO 15 and 28 MHz the sync processor counts the source correctly while
  the lock bit stays clear, so **VTOTAL being right is not evidence the PLL
  locked**, and the lock bit carries information nothing else does.
- Below `PLLAD_MD` 960 VTOTAL reads a rock-steady 155, exactly half of 311 --
  one line counted per two sent. Steady and wrong, which is the signature
  CLAUDE.md warns is not evidence of a lock.

## Not established

The upper VCO edge. 187.7 MHz took the unit off the network and nothing above it
has been tried, so the top of the range is unmeasured. The `KS` 0 crossover, CKO
above 80 MHz, is unreachable on a 15.6 kHz source within the divider ceiling.
