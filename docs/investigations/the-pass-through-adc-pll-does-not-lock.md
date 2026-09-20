# The pass-through ADC PLL does not lock

On the scaling path the ADC PLL holds lock. In pass-through it does not, and the
symptom is visible: the finest grating on the test card beats, and whether it
beats changes from one entry into pass-through to the next.

Measured on `vga`, RiscPC at 640x480@60, 31500 Hz line, 100% framing.
`STATUS_MISC_PLLAD_LOCK` and `STATUS_SYNC_PROC_HTOTAL` read together, 0.25 s
apart, both paths in one window:

| | `PLLAD_MD` | `KS` | oversample | CKO | VCO | lock | `STATUS_SYNC_PROC_HTOTAL` |
|---|---|---|---|---|---|---|---|
| scaling | 1494 | 1 | 2 | 47.1 MHz | 94.1 MHz | **39/40**, and 24/24 after a round trip | 1494, one distinct value in 40 |
| pass-through | 2039 | 1 | 2 | 64.2 MHz | 128.5 MHz | **2/38** | 2038..2040, three distinct values |

The wandering count is the signature `docs/tv5725-chip.md` gives for an unlocked
PLL -- a few counts either side of the divider, which reads as a small latch
error and is not one.

## It is not the charge pump

`PLLAD_ICP` was 4 in pass-through against 6 on the scaling path, which is the
obvious suspect and is **refuted**. Walked in pass-through with each value
latched by hand, lock sampled at 0.25 s:

| `PLLAD_ICP` | lock |
|---|---|
| 4 | 1/28 |
| 5 | 2/9 |
| 6 | 1/28 |
| 7 | 0/27 |

The field is three bits, so 7 is the top of it. Nothing it can hold locks. That
is why `Adc::applySampleRate()` now writes one value for every path: there was
nothing measured for a path to choose between.

## It is not the VCO gain either, on the evidence that exists

[the-vco-gain-follows-the-vco.md](the-vco-gain-follows-the-vco.md) sweeps the
VCO and has `CKO 64.4, KS 1, VCO 128.8 MHz` locking at gain 0 -- within 0.3 MHz
of what pass-through runs, at the same post divider, with the same gain. So the
VCO alone does not predict this failure.

## What differs from that sweep is the oversampling, and it is untested

That sweep was driven with `/sampleclock?md=N&os=1`. Pass-through solves to
oversample 2, which changes `PLLAD_CKOS` from 1 to 0 and so doubles the
conversion clock the tap feeds the pipeline: 128.5 MHz in pass-through against
94.1 MHz on the scaling path, at VCOs of 128.5 and 94.1.

**`os=1` cannot be tested through `/sampleclock` on this path.** The route goes
through `HdBypass::applyPassThroughSampling()`, which calls
`Adc::applyForBypassRgbhv()` and then `Adc::applySampleRate()` with the
oversample pass-through asks for, so the requested `os` does not survive --
measured, a request for `os=1` came back reading `ADC_CLK_ICLK1X` 1. The same
route re-imposes anything else the bypass entry writes, which is how a
hand-written `PLLAD_ICP` was silently reverted mid-experiment.

So the next measurement needs either a build that lets pass-through take a
requested oversample, or the group written and latched by hand rather than
through the route.

## Why the divider is what it is

`HdBypass::dividerFor()` takes the smaller of the clock bound and
`MaxChannelLine - RasterGuardSamples`, and at 31500 Hz the channel counter binds
-- 2047 is eleven bits, `the-line-counters-are-eleven-bits-measured.md`. So
pass-through runs a **larger** divider than the scaling path on the same source,
not a smaller one, and the conversion clock is higher for that reason rather
than by choice.

## What this does not explain

Pass-through does not show the horizontal landing jump, and an unlocked sample
clock is not offered here as a reason for that. The two are separate
observations about the same path.
