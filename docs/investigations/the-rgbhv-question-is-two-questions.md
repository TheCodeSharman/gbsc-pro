# `sourceIsRgbhv()` cannot be derived from the output, because it decides the output

`rto->videoStandardInput` carries 14 for a scaled RGBHV source and 15 for a
bypassed one, and `docs/retiring-mode-detect.md` files both under *what output
was chosen*. That reading invites an obvious step: hold the output mode
somewhere of its own, define the three predicates over it, and the byte stops
carrying either value.

    bool sourceIsRgbhv() { return rgbhvBypass() || scalingRgbhv(); }
    bool scalingRgbhv()  { return PresetLoad::scalingRgbhvInForce(); }
    bool rgbhvBypass()   { return rto->videoStandardInput == 15; }

**It is circular, and the loop closes on the bench in under a minute.**

## The cycle

| step | what it reads |
|---|---|
| `runSyncWatcher()`'s RGBHV block runs | `steerableRgbhv()`, which is `sourceIsRgbhv()` |
| it sets `rto->isValidForScalingRGBHV` | only inside that block |
| `PresetLoad::enableScalingRgbhv()` | `preferScalingRgbhv && isValidForScalingRGBHV` |
| `loadComputedPreset()` sets the held flag | from that |
| `scalingRgbhv()` answers | from the held flag |

So the flag that says the output is scaling RGBHV is set only while the block
runs, and the block runs only while the predicate the flag feeds is already
true. Bypass is the one entry: detection writes 15, `rgbhvBypass()` goes true,
and the chain starts. **Once that entry is behind it, nothing can re-arm the
chain.**

**The state it falls into is SCALING, and that is the whole trick.** An output
is either bypassed or scaled and there is no third option -- but
`scalingRgbhv()` does not mean *scaling*. It is a conjunction: the output is
scaled AND the source is classified RGBHV. Drop the second half and the source
is still scaled, now as an ordinary broadcast standard, which is neither
`rgbhvBypass()` nor `scalingRgbhv()` and satisfies no predicate that would put
it back.

**A predicate that welds an output fact to a source classification is what makes
that representable at all.** With the two facts apart -- the output chosen, the
source measured -- "scaled as PAL SD while actually RGBHV" is not a state the
firmware can be in.

## What it looks like on the bench

The RISC PC at 320x256 on `vga`, which is an RGBHV source, is classified as
standard 2 -- PAL SD -- and stays there. `printInfo()`:

```
h: 511 v:   0 PLL:8 A:7b7b7b S:00.00.20 H+   I:6a D:0400 m:2 ht:2250 vt: 308 hpw: 159 u:  0 s: 0 S:12 W:-57
```

`m:2` is the whole finding: `getVideoMode()` opens with `sourceIsRgbhv()` and
returns the held byte for an RGBHV source, so with the predicate false it falls
through to the `STATUS_00` classification and names a broadcast standard
instead. `vt: 308` is the csync count rather than the 311 the separate-sync path
reads, so the sync type has gone with it.

**The picture survives, which is what makes it expensive.** Every register reads
plausible and the card is on screen; what changes is which branches the sketch
takes for the rest of the session.

## How it was caught, and what nearly hid it

`test_console_does_not_print_an_invalid_vperiod_as_a_number` fails, because
`STATUS_IF_VT_BAD` dithers once the source is on the wrong path and
`printInfo()` prints `v:` as a number on the passes where it reads 0.

**The test is not the evidence and it looks like a flake.** It passes in
isolation, skips when `VT_BAD` is clear at the moment it reads, and failed 2 of
3 then 4 of 5. What settles it is sampling the register directly against a build
of the parent commit:

| build | `STATUS_IF_VT_BAD` | `VPERIOD_IF` |
|---|---|---|
| parent commit | 1 in 40 of 40 | 57 in 40 of 40 |
| with the predicates rebased | dithers | 0, then 112 and 99 across runs |

**A count of agreeing HTTP reads is not stability**, and here it actively
misled: 40 reads of `VT_BAD` all returned 1 while `printInfo()`, reading from
`loop()`, saw 0 often enough to fail four tests running.

## What a step that works has to carry

**The byte answers two questions and only one of them is about the output.**
*Is this source RGBHV at all* is an input fact, established by detection before
any output has been chosen, and it has no other home today. *Scaled or
bypassed* is the output fact.

So removing 14 from the byte needs the input half held somewhere first --
detection setting it, and the RGBHV block reading it rather than reading its own
output. Rebasing the predicates without that is not a smaller version of the
step; it deletes the only path into the block.

## What is safe to take from the attempt

- The dead if/else in the new-mode block, whose two arms were empty.
- `Adc::applyScalingChargePump()` and the sync-processor writes folded into
  `SyncProcessor::applyForScalingRgbhv()`, which are ownership moves that do not
  touch the predicates.
