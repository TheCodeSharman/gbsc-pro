# The recovery ladder never restarts the ADC PLL

A boot comes up with no picture perhaps four times in five, and every cheap
recovery fails: `/input?src=vga`, `/sc?~`, an `ADC_INPUT_SEL` bounce and a source
mode round trip all leave the unit unacquired. The console shows the escalation
ladder cycling for ever, `own V sync: yes` every few seconds, and no `sampling:`
line at all.

## What the unit is doing

Boots are repeatable from a session: `/restart` reboots the ESP, and
`BOOTLOG_BYTES=2048` or more keeps the trace. Across 30 boots the split is
clean, and a detection that SUCCEEDS is what a failing boot has:

| final `DETECT` | outcome |
|---|---|
| 719..960 ms | absent |
| 1542..2034 ms | acquired |

Detection returns 3 -- sync found -- in every one of them. Traced at the moment
it returns, the chip reads:

```
DETECT: found vsyncWait=1ms hsyncWait=1ms presets=302ms VT=0 HT=0 HPERIOD=265 lock=0 SOG=1
```

`STATUS_SYNC_PROC_VTOTAL` 0, `STATUS_SYNC_PROC_HTOTAL` 0 and
`STATUS_MISC_PLLAD_LOCK` 0, in eight boots out of eight. **The sync processor is
counting nothing and the ADC PLL is unlocked**, while detection has just
declared the source found.

It can declare that because the branch that returns 3 asks only
`STATUS_SYNC_PROC_VSACT` and `STATUS_SYNC_PROC_HSACT`. Neither requires a count.
Its two sibling branches both wait on
`VideoSignal::countIsSource(SyncProcessor::lineCount())`; this one does not.

## Why an unlocked ADC PLL looks exactly like no source

The sync processor counts in ADC clocks. With the ADC PLL unlocked there are no
clocks to count, so `STATUS_SYNC_PROC_VTOTAL` reads 0 whatever the source is
doing -- which is indistinguishable, from the engine's side, from a dead cable.

Measured on a unit that had cycled the whole ladder for minutes: applying the
sampling group and restarting the PLL took it from `VTOTAL` 0 to **`VTOTAL` 627,
`HTOTAL` 1792, `lock` 1** -- the source's own line count -- within one pass.

**`PLLAD_LEN` is what is left wrong.** Read on a stuck unit it is 0: the PLL's
clock enable is off, so no amount of rewriting the group can make it lock.
Releasing `PLLAD_VCORST` is not enough on its own; the enable has to be driven
and the group reloaded after it.

**No rung of the ladder reached any of this.** Every one of the eleven wrote
sync-processor registers, which cannot lock an ADC PLL, and `FullReset` at pass
150 is measured not to recover it.

## The second half: nothing arms a re-measure

A pass only measures while a mode change is outstanding --
`runPass()` returns early unless `videoPath_.changingMode()`. What arms one is
`sourceMoved()`, and every arm in it needs the count to move, to be out of
range, or to fail to settle.

A source that has sat at one steady count since boot satisfies none of them. So
once the arm taken at boot has been spent against a chip that could not be
measured, **the engine waits for a movement that never comes** while the ladder
pokes registers past it indefinitely.

## What is in the firmware now

Two rungs, both before `FullReset` because that one is measured not to help:

| pass | rung | what it does |
|---|---|---|
| 60 | `RestartSamplingClock` | puts the divider in force back on the chip and restarts the PLL under it, enable included |
| 70 | `RemeasureSource` | arms `inputTimingsChanged()`, so a spent arm cannot deadlock the engine |

`Adc::restartPll()` is the primitive: reset the VCO, reload the group, restart
the phase adjusters, drive `PLLAD_LEN`, latch again.

With both in place a failing boot now reaches the correct reading and the
correct divider, which it never did before:

```
recovery: restart sampling clock at pass 60
recovery: remeasure source at pass 70
sampling: 627 lines x 60.31 Hz -> line rate 37879 (field rate)
sampling: rate 37879 doubled 0 -> divider 2006
```

## What is still open

**Boot reliability is not restored.** After that correct measurement the ladder
still escalates to `full reset at pass 150`, so the solve that follows the
measurement does not complete. The discriminator against an acquiring boot is
one line: a good boot goes on to `externalClockGenSyncInOutRate()`, a failing
one does not, so `VideoPath::solveWindows()` is refusing -- in
`sizeCaptureWindow()`, `calculateInputFormatterRegisters()` or an unusable
`calculateOutputRaster()`. That is where to look next, and the boot log will
say which once those three report their refusals.

A boot that acquires reads `lock=1` in its own `DETECT: found` line, so the
difference upstream is whether detection happens to leave the PLL locked.

**`HPERIOD_IF` railing is not this fault and is not fixed here.** It rails in
these boots too, and the engine is right to fall back to the field rate: every
acquiring boot measures `(field rate)`. `docs/investigations/hperiod-if-railing.md`.

An `ADC_INPUT_SEL` bounce is not a recovery for this state. Measured against it,
the bounce left `HPERIOD_IF` railed and dropped `STATUS_MISC_PLLAD_LOCK` from 1
to 0 -- it made a counting unit stop counting.
