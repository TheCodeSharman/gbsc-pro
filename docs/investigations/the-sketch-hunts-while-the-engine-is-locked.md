# A locked source shows black because `getVideoMode()` reports none

A source sync-type round trip -- `SYNC 1` then `SYNC 0` on the bench RiscPC --
leaves the unit with the encoder locked at 1920x1080/50Hz and the screen
entirely black. The geometry engine is not at fault and neither is the capture
path: the sketch's own classifier stops recognising the source, and its no-sync
handling walks the ADC and the sync processor away from a signal the engine is
measuring correctly.

## What the two halves each believe

Photographed black, with the console attached:

```
h:  70 v:---- PLL:3 A:7b7b7b S:00.1c.00 H+V+ I:81 D:0700
     m:0 ht:2250 vt: 311 hpw:2092 u: 96 s: 0 S: 5 W:-59
```

`m:` is `getVideoMode()` and it is **0**. Beside it `vt:` is 311 and `ht:` is
2250, which is the source exactly. `/geometry` agrees, and reads the same in the
black state as it does with a picture:

```
oh 152  eh 748  ov 37  ev 536  ch 1044  cv 622  lineRateHz 15575
```

So the sync processor is counting the source, the engine has solved against it,
and the sketch's classifier reports no mode at all.

## What the no-sync path then writes

`u:` is `noSyncCounter`, held at 150, and `S:` is `currentLevelSOG`. The first
status line after the round trip prints `S:12` and every one after it prints
`S: 5` -- the sync-on-green slicer ratcheting down under a signal that is
present.

`updateSpDynamic()`'s `vidModeReadout == 0` branch stamps its sync-search
configuration over the engine's on the same schedule. Three of its constants
appear unaltered in a dump of the black unit, against what the same unit holds
with a picture:

| field | black | with a picture | `updateSpDynamic()` writes |
|---|---|---|---|
| `SP_DLT_REG` | 48 | 0 | `0x30` |
| `SP_H_PULSE_IGNOR` | 2 | 255 | `0x02` |
| `SP_H_CST_SP` | 256 | 1661 | `0x100` |
| `ADC_SOGCTRL` | 5 | 12 | ratcheted, not written |

`SP_H_PULSE_IGNOR` is the clearest of them, because
`SyncProcessor::applyForSyncType(false)` writes `0xff` and `establishSyncType()`
runs it on every mode change. The register reads 2 regardless. **The field has
two owners and the sketch writes last.**

## Why the engine cannot recover it

The engine re-solves correctly and repeatedly, and is stamped over each time.
Its own cycle, measured over the 80 s after the round trip:

```
source moved: interrupt (311 lines, solved 311)   <- nothing moved
own V sync: yes after 2ms                          <- probe, correct
sampling: 311 lines x 214.93 Hz -> line rate 0     <- refused
sampling: 311 lines x 42.38 Hz  -> line rate 0     <- HPERIOD_IF railed at 511
sampling: 311 lines x 50.08 Hz  -> line rate 15575 <- accepted
```

Period ~5.4 s. `sourceMoved()` reports `interrupt`, not `count` or `rate`: the
line count matches what was solved and so does the rate, so the only thing
arming the change is the latched SOG interrupt -- which is itself a consequence
of the slicer being walked down.

## `CAPTURE_ENABLE` is not the mechanism

It is enabled for 83% of the black state -- 4.5 s of every 5.4 s cycle, frozen
only for the ~0.9 s each armed mode change takes to complete. Capture freezing
cannot account for a picture that is black continuously, and a run of samples is
what shows this where a single reading cannot:

```
CAP=1  99.02 .. 103.37   CAP=0 103.37 .. 104.27
CAP=1 104.27 .. 108.84   CAP=0 108.84 .. 109.70
CAP=1 109.70 .. 114.43   CAP=0 114.43 .. 115.31
```

## Recovery

`/input?src=vga` restores the picture within 10 s, because reselecting the input
is what makes `getVideoMode()` report a mode again. A register restore does not,
on its own: the sketch rewrites its no-sync configuration within 500 ms while
its classifier still reports 0.

## What the fix has to address

Not the engine's solve, which is correct throughout, and not the capture freeze.
Either the sketch's classifier has to recognise a source the sync processor is
counting, or its no-sync handling has to stop owning fields
`SyncProcessor::applyForSyncType()` owns. The second is the narrower change and
`SP_H_PULSE_IGNOR` is the field that demonstrates the conflict.
