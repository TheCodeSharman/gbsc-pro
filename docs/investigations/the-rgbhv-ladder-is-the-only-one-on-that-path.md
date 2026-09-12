# The RGBHV block's steadiness run is not a duplicate, it is the only one there

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
