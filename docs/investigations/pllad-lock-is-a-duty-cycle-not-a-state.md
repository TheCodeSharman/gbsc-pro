# STATUS_MISC_PLLAD_LOCK is a duty cycle, not a state

`STATUS_MISC_PLLAD_LOCK` flickers several times a second on a picture that is
visibly perfect. Read once over HTTP it says almost nothing, and a single zero
is not evidence of anything at all.

Measured with `Tv5725::SamplingLog` at 25 ms, 800x600@60 on `vga`, on a picture
judged crystal clear at the bench:

| path | divider | `PLLAD_LOCK == 1` | transitions | `SP_HTOTAL` against the divider |
|---|---|---|---|---|
| scaling | 1440 | 86.30% of 1131 | 261 (8.7/s) | 1440 x951, 1439 x90, 1441 x90 |
| pass-through | 2039 | 99.41% of 507 | 6 (0.2/s) | 2039 in 507 of 507 |
| pass-through | 2038 | **100.00%** of 507 | **0** | 2038 in 507 of 507, deviation 0 |

A single read on the scaling row has a 13.7% chance of returning 0 while
everything is right. Every "the ADC PLL is not locking" reading taken over HTTP
during the session that produced this page was of that kind, and none of them
established the claim.

**Ask the percentage and the transition rate**, both of which need a sampling
log — an HTTP read answers at tens of hertz at best and cannot see a bit that
changes 8.7 times a second.

## The honest health metric is SP_HTOTAL against the divider

The sync processor counts in ADC clocks, so a locked ADC PLL reports the
divider back. That is a comparison, and it is stable where the lock bit is not:
0 or ±1 on every healthy state above.

A genuinely sick state looks nothing like either. Measured during failed
acquisitions, against a divider of 2039: `SP_HTOTAL` reading 1580, 1581, 1729,
2495, 2496, 2497 — hundreds out, with the duty computed from it landing
anywhere between 0.029 and 0.173 where the mode is due 0.121.

## What that costs downstream

`SourceTiming::lookUp()` matches the measured duty to a standard's
`syncPixels / totalPixels` within `SyncDutyTolerance`. A duty taken while
`SP_HTOTAL` is wandering matches nothing, so pass-through blanking falls back to
the sync envelope — `HD_HB_SP` 239 and `HD_HB_ST` 2039, the whole line, no right
blanking — and the source's border is fully visible. The same unit an
acquisition later reads `HD_HB_SP` 417 and `HD_HB_ST` 1962 and is clean.

So an intermittently visible border in pass-through is a symptom three steps
downstream of a measurement taken while the sync processor was not counting the
source, and not a fault in the blanking.

## What it does NOT explain

Beating on a high-frequency grating is not this. It is present on a
pass-through state measuring `PLLAD_LOCK` 100.00% over 507 samples with
`SP_HTOTAL` equal to the divider in every one, and absent on other acquisitions
of the same mode with the same registers.

What varies per acquisition and is absent from these fields is the sampling
phase, which `acquireSamplingPhase()` searches once per bypass entry. Nothing
here measures it.
