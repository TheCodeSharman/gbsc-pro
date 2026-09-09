# Some boots never acquire, and only an input selection clears it

Roughly one boot in three comes up with no source and stays that way
indefinitely. Every reading this side of the board looks healthy. **The cause is
not yet known**; what follows is what has been measured, and the seven
candidates that have been ruled out.

## The signature

`/geometry` all zeroes with `state: absent`, and on the console:

```
h: 511 v:---- PLL:0 A:7b7b7b S:00.0c.00 H+V+ I:01 D:0760 m:0 ht:1023 vt:   0 hpw:   0 u: 96 s: 0 S: 3
own V sync: yes after 2ms
```

`m:0` with `s: 0` and `u: 96` is the no-sync latch that
`scaling-rgbhv-standard-latches-the-no-sync-branch.md` describes:
`getVideoMode()` returns 0, `runSyncWatcher()` never leaves its no-sync branch,
`applyPresets()` never runs, and the engine is never armed.

**`H+V+` is what makes it strange.** Both sync inputs are seen and the sync
processor still counts nothing -- `vt: 0`, with `STATUS_00` reading `0x00` and
`STATUS_SYNC_PROC_HTOTAL` pinned at 1023.

## What clears it

`/input?src=vga` recovers it in about 10 seconds, every time it has been tried.
`/sc?~` recovers it sometimes and not others.

## What it is NOT

Every row was tested against the fault on the bench.

| candidate | how it was ruled out |
|---|---|
| the preferences read race | the boot log reads `SeleInputSource=2 suspect=0` on stuck and healthy boots alike |
| the routing frame not being sent | the boot log reads `INPUT: vga frame=0x61 ADC_INPUT_SEL=1` on both, at the same millisecond |
| the routing frame being LOST by the HC32 | re-sending it every 10 s while the source is missing changes nothing: 5 of 10 boots still stuck, well past the cadence. The frame `/input?src=vga` sends is byte-identical -- `Checksum_Sendmode(VGA, 1)` ORs 1 into `0x60`, which is `sendInputFrame(0x61)` |
| the sync-type probe answering wrongly | one stuck boot answered `own V sync: no`, but another answered `yes` five times over 40 s and stayed stuck |
| the ADC PLL being out of lock | `STATUS_MISC_PLLAD_LOCK` reads 0 on a HEALTHY unit counting 311 lines, so the bit says nothing either way |
| the sampling group disagreeing | writing `SP_RT_HS_SP` and `IF_HSYNC_RST` back into agreement with `PLLAD_MD` changes nothing |
| the sync processor being unconfigured | writing the coast pair back to 7/3 changes nothing |
| the sync processor being wedged | pulsing `SFTRST_SYNC_RSTZ` moves `HTOTAL` off its rail and leaves `vt: 0` |
| the ADC clock group | `ADC_CLK_ICLK1X`/`2X` read 0 stuck against 1 healthy, but writing them to 1 and pulsing `PLLAD_LAT` changes nothing |
| the held ADC PLL band | `pllBand_` initialises to `NoPllBand`, so a fresh boot knows nothing to be wrong about |

**The boot logs of a stuck boot and a healthy one are byte-identical.** Whatever
diverges does so after the ESP has finished booting.

## Where the remaining difference is

`/input?src=vga` is `Checksum_Sendmode(VGA, 1)` then `applyInputSelection()`.
The frame is identical to the boot's, and `applyInputRegisters()` plus the frame
is measured NOT to recover -- so the recovering ingredient is one of the rest of
`applyInputSelection()`:

    SeleInputSource = settings.legacySource;
    Info = id;
    resetSyncProcessor();          // SFTRST_SYNC_RSTZ pulse, then LoadDefault()
    applyInputRegisters(settings); // measured insufficient on its own
    BriorCon = settings.brightnessSet;
    rto->sourceDisconnected = true;
    saveUserPrefs();

The reset pulse alone is measured insufficient, which leaves `LoadDefault()`'s
firmware-side state -- `syncWatcherEnabled`, `failRetryAttempts`, `presetID`,
`phaseADC`/`phaseSP`, `Adc::forgetPllBand()`,
`Deinterlacer::forgetScanlines()` -- none of which a register write can reach.

**The next experiment is a diagnostic build with one route per ingredient**, so
each can be tried against a stuck unit on its own. Register-level probing cannot
get further: everything left is ESP state.

## Measuring it

Reflash repeatedly and time how long the source takes to come back; each flash
resets the ESP and re-runs the boot.

**`/sc?a` does NOT restart the unit over HTTP** -- the command reaches the serial
handler only -- so a boot cannot be repeated without a flash. A run that assumes
otherwise records every trial as an instant recovery, which is how a 5-of-5 pass
was read as healthy before the check was added.

**The trials are not independent.** A stuck boot is followed by a recovery
command, which leaves the chip in a different state from a clean acquire, so
failures cluster and small samples swing widely. Two builds measured 10 of 11
and 4 of 8 and the difference did not survive: the second build reproduced the
fault at 5 of 10 on its next run, and the first reproduced it too.
