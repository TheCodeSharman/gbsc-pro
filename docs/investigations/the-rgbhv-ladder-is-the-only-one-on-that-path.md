# The RGBHV block's steadiness run is the only escalation on that path

**THE CAUSAL CLAIM BELOW IS n=1 EACH WAY AND IS NOT SAFE. The structural one
is.** What is established by reading the code -- that all three of
`runSyncWatcher()`'s branches are gated `!rgbhvBypass()`, so `SyncRecovery` is
unreachable on the bypass path -- stands on inspection. What is NOT established
is that removing the RGBHV run is what prevented recovery, because the
reproduction turns out not to be repeatable: see *The reproduction is not
repeatable* at the end.

`runSyncWatcher()` appears to hold two escalation ladders: its own no-sync
branch counting `rto->noSyncCounter` through `SyncRecovery::stepAt()`, and a
second one inside `if (sourceIsRgbhv())` counting `RGBHVNoSyncCounter` with its
own stability test and its own destructive recovery.

**Removing the second one leaves a source with no way back.** Measured, both
directions, over the same reproduction.

## Why they look like duplicates, and one measurement that agrees

Both write `rto->continousStableCounter`. In the degraded state that is a real
conflict: measured with `getVideoMode()` returning 0, the no-sync branch zeroes
the run on every pass while the RGBHV block increments it, so `s:` sits at 0 and
the RGBHV steering's own `continousStableCounter >= 2` gate never opens.

In the healthy state they do not conflict -- `m:14 u:0 s:ff`, both incrementing.

So the conflict is real and it is confined to the state that needs recovery,
which is what made removal look right.

## Why removal is wrong

**All three of the main watcher's branches are gated `!rgbhvBypass()`:**

    no-sync     (detectedVideoMode == 0 || !status16SpHsStable) && !rgbhvBypass()
    new-mode    (detectedVideoMode != 0 && ...)                 && !rgbhvBypass()
    stable      getStatus16SpHsStable() && ...                  && !rgbhvBypass()

So on a **bypassed** RGBHV source the main watcher runs none of them. Nothing
increments `noSyncCounter`, nothing escalates, and `SyncRecovery`'s ladder --
including `FullReset` at position 150 -- is unreachable. The RGBHV block's own
run is the only recovery that path has.

`u: 0` pinned is the witness, and it is not the run being starved by a
competitor: it is the run never being advanced at all.

## Measured, on the bench

Reproduction: `SYNC 1` then `SYNC 0` over ModeServ, repainting after each. The
RISC PC on `vga` at 320x256@50 throughout, so the source never changes.

| | with the RGBHV run removed | with it present |
|---|---|---|
| after `SYNC 0` +20 s | `SP_SOG_MODE` 1, `VTOTAL` 97, PLL unlocked | **recovered**: `SP_SOG_MODE` 0, `VTOTAL` 311, `PLLAD_MD` 2206, PLL locked |
| +120 s | unchanged, `PAD_SYNC_OUT_ENZ` 1 -- no signal to the sink | holding, acquired |
| escape | `/sc?~` only | unaided |

`printInfo()` through the stuck state:

```
m:15 / m:0 alternating   u: 0   s: 1   ht: 5   S: 13,11,10,8,7,5,4,3,2,1
```

`u: 0` throughout, so no escalation; the SOG level ratcheting to its floor; and
`STATUS_SYNC_PROC_HTOTAL` reading 5.

## The state it sticks in, and the byte that puts it there

`m:15` is `getVideoMode()` returning 15, which it reaches through

    if (sourceIsRgbhv())
        return (detectedMode & 0x0a) > 0 ? heldStandard() : 0;

and `heldStandard()` reconstructs **15** for a bypassed RGBHV source while
`rto->videoStandardInput` holds **14**. One state, two spellings -- the same
shape as `two-spellings-of-scaling-rgbhv.md`.

That is what makes the gating bite. A bypassed RGBHV source is exactly the case
every branch excludes, and it is also the case the byte describes twice.

## The reproduction is not repeatable, so it cannot A/B a behavioural change

Measured afterwards, on a build carrying one change that is **provably neutral**
by inspection -- `heldStandard()` substituted for `rto->videoStandardInput` in
two comparisons, where the only case the two differ is the only case the branch
is gated out of, so no state evaluates them differently.

From the first genuinely clean baseline of the day -- `HPERIOD_IF` 431,
`PLLAD_MD` 2208, `VTOTAL` 311, PLL locked -- the same round trip gave:

```
after SYNC 0 +20s    acquired  15625   VTOTAL 311   PAD_SYNC_OUT_ENZ 1
         +40s        absent    15625   VTOTAL 291
         +60s        absent    15625   VTOTAL  26
         +90s        absent    23437   VTOTAL  89   PLLAD_MD 1124
        +120s        absent     6279   VTOTAL  69
```

A neutral change cannot cause that. So the outcome of this reproduction spans
"recovers unaided in 20 s" to "never recovers" **with the firmware behaviour
held constant**, and a single run of it says nothing about a change.

**Two confounds are known and one is not controlled.** `HPERIOD_IF` railing is
adopted as the held line rate and never recovers on its own, so a run started on
a railed register measures the railing -- that one the harness now controls by
requiring 431 at baseline. What is left uncontrolled is whatever varies between
runs that start clean, and it is enough to invert the result.

**So a behavioural change here has to be justified by inspection and host tests,
and the bench used for gross regression** -- does the source acquire, is the
picture right -- rather than for an A/B on recovery. Building the repeatable
reproduction is its own piece of work, and the sampling log reading from inside
`loop()` is the instrument for it: a full register dump over HTTP is hundreds of
deferred reads and demonstrably changes the outcome.

## The order this forces

The RGBHV run cannot be removed while the main ladder is gated out of the bypass
path. So the order is:

1. bypass stops being a state the ladder is excluded from -- it is an output
   mode, and an unlocked source is an unlocked source whichever route carries
   the video
2. `heldStandard()` stops reconstructing a second spelling, so there is one
   answer to compare against
3. *then* the RGBHV run comes out, because `SyncRecovery` reaches that path

Doing 3 first is what this page measures. `../video-source-acquisition.md`,
step 10.
