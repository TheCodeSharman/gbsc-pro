# A boot that detects too early can never recover

Most resets leave the unit reporting `state: absent` for ever, with the recovery
ladder cycling every seven seconds and the source sitting there perfectly.
Re-selecting the input it is already on recovers it at once. This is why.

## It is not the input, and not the preferences

Both are working. The boot log of a FAILING boot:

```
PREFS: attempt 1 open=1 size=39 got=39 plausible=1 first=[35 30 41 30]
PREFS: loaded presetPreference=5 frameTimeLock=0 slot=65 SeleInputSource=2 suspect=0
INPUT: vga frame=0x61 ADC_INPUT_SEL=1 t=3614ms
```

The preference is stored, the read is not suspect, the frame is sent to the AV
module and `ADC_INPUT_SEL` is written. The source is present on that input
throughout.

## Detection concludes on a chip that is counting nothing

`DETECT: found` reports `syncFound 3` — sync present — while
`STATUS_SYNC_PROC_VTOTAL` reads zero. Six consecutive restarts, one line each:

| outcome | `VT` at `DETECT: found` | `presets=` |
|---|---|---|
| never acquires | **0** | 301 ms |
| never acquires | **0** | 301 ms |
| never acquires | **0** | 302 ms |
| never acquires | **0** | 301 ms |
| acquires | **98** | 1274 ms |
| never acquires | **0** | 555 ms |

`VT` at that moment is a complete discriminator over the sample, and the firmware
already computes and logs it. The preset time moves with it, which is the shape
of a race rather than a bad source: the boots that spend longer before detection
are the ones that work.

Downstream the consequence is total. A failing boot NEVER prints a `sampling:`
line, so the steadiness gate never passes, nothing is ever measured, and the
engine has nothing to solve from.

## And the ladder cannot escalate out of it

`SyncRecovery` has thirteen rungs. The relevant positions:

| step | pass |
|---|---|
| `FullReset` | 150 |
| `ReprobeSyncType` | 151 |
| `ToggleInput` | 413 |
| `ReopenSogSeparator` | 450 |

`ReprobeSyncType` asks whether the source has its own V sync, and on a
separate-sync source it always does:

```cpp
case SyncRecovery::ReprobeSyncType:
    // A V sync arriving is proof of a source, so the run restarts rather
    // than escalating on to the input toggle.
    if (!videoPath_.reacquireSyncType()) {
        tv5725Log("recovery: own V sync found, the run restarts");
        return true;
```

The run restarting resets the pass counter, so the ladder gets to 151 and starts
again — for ever, about every seven seconds. **Passes 152 to 450 are unreachable
on any source with its own V sync, which makes `ToggleInput` and
`ReopenSogSeparator` dead rungs.**

The premise in that comment is true and does not support the conclusion. A V
sync IS arriving; the source IS there and on the right input. What that proves
is that escalating to a *different* input would be wrong — not that continuing
on this one will ever work.

Two further things would stop `ToggleInput` helping even if it were reached:
`mayChangeInput()` is false whenever the input was explicitly chosen, which a
stored preference makes it; and the input is already correct, so changing it is
not the repair.

## What does recover it

`/input?src=vga` — selecting the input it is **already on**. That runs
`InputVGA()`, which re-runs detection from the top. Measured many times; it is
the only thing that works. `/sc?~`, a source mode round trip, an
`ADC_INPUT_SEL` bounce and waiting indefinitely all fail.

So the recovery that works is not a rung at all. Re-running detection on the
CURRENT input is absent from the ladder, and it is the one act that clears the
state.

## Three separable defects, one of them fixed

**Detection reported found while the sync processor counted nothing.** Fixed: it
now waits up to 600 ms for a count and reports not-found without one, so the
caller retries. That addresses the cause rather than the symptom, and it is
enough on its own -- restarts that reach `acquired` unaided went from 3 of 11 to
10 of 10.

The other two stand, and both would still bite a source that reaches the state
some other way:

- **A rung that always succeeds resets the escalation.** `ReprobeSyncType`
  restarting the run unconditionally makes the tail of the ladder unreachable on
  any source with its own V sync, so `ToggleInput` and `ReopenSogSeparator` are
  dead rungs. It should restart only when the probe changed something, or the
  restarts should be bounded.
- **The measured recovery is missing from the ladder.** Re-running detection on
  the current input is what clears this, and no rung does it.

## Not the HPERIOD_IF railing

This survived removing every load-bearing `HPERIOD_IF` reader. The failing boots
still fail with the line period no longer able to reach a decision, so the
railing was a separate fault that happened to share the symptom.
