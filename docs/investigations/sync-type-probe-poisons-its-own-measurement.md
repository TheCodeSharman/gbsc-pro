# The sync type probe poisons the measurements that follow it

`SourceMeasurement::sourceHasOwnVsync()` decides the sync type by writing
`SP_EXT_SYNC_SEL` to 0, waiting, watching for `STATUS_SYNC_PROC_VSACT`, and
restoring the register. It restores it and **returns immediately**.

`Geometry::poll()` runs `establishSyncType()` first in its mode-change branch,
then the scan mode, the reference divider, the steadiness gate and the line
rate. Every one of those is a measurement taken through the sync processor, and
the sync processor has just had its path moved out from under it.

## What the disturbance is, measured

Toggling `SP_EXT_SYNC_SEL` by hand on the bench source (320x256@50, 311 lines)
and sampling `STATUS_SYNC_PROC_VTOTAL` from the instant it is restored:

```
run   +0.01s   first agreement with 311   four agreeing counts
  1      305                     +0.30s               +1.17s
  2      330                     +0.38s               +1.25s
  3      425                     +0.30s               +1.11s
```

**The count is wrong and STABLE inside that window**, which is what makes it
expensive. `SourceMeasurement::sampleSteady()` takes one sample per poll and
asks for four in a row, and `loop()` runs far faster than 300 ms — so four
agreeing *wrong* counts fit inside the re-acquisition and no steadiness run can
tell the difference.

`SyncProcessor::PathSettleMs` is 500, and `establishSyncType()` waits it out
after applying the type. It belongs to the caller rather than to
`applyForSyncType()`: a blocking half second inside that function also lands on
`bypassModeSwitch_RGBHV()` and the preset arms, which are not about to measure
anything, and putting it there produced a sync-type probe every 7.2 s on a
settled source where an unmodified build was silent for 70.

## The loop it drove

On a csync source the probe is a real 1.24 s excursion off the configured path,
so the disturbance above lands on every mode change. Traced with the console
attached while ModeServ drove the source `SYNC 1` then `SYNC 0`:

```
probe -> 300 ms of wrong-and-stable counts -> the gate passes on them
      -> the solve runs against a garbage line count (223, 263, 208, 200, 370)
      -> solvedLines_ becomes the garbage
      -> sourceMoved() sees the real 308 differ -> mode change -> probe
```

Period ~1.6 s, sustained for the whole 45 s the source was on composite sync,
while an outside poller read a steady 308 between passes and `HPERIOD_IF` held a
correct 431 throughout. The rate was right and only the count was junk, so every
solve looked self-consistent.

## Refuted along the way

- **That the probe answers wrongly on a source that has not settled.** It does
  not: `no` throughout composite sync, `yes after 2ms` on separate sync, every
  time. What is wrong is what it leaves behind.
- **That the ADC re-latch is the disturber.** Latching the divider already in
  force, three times, gave 75 consecutive samples at 311 — no disturbance at
  all. A latch that *changes* the divider is a different question and is not
  tested here.
- **That `DAC_RGBS_PWDNZ` going to 0 mid-recovery is the engine.** It is the
  sketch's own detection sweep — `inputAndSyncDetect()`, which prints
  `sync type: N/3 field rate probes plausible` — running about twenty seconds
  after the source changed, and it is what rescued the unit before the engine
  could.

## Why the engine could not get out of it

`Geometry::sourceMoved()` is the only thing that arms a solve while the engine
is idle, and it returned false for any count outside
`CaptureWindow::SourceVerticalTotalMin..Max`. A source on the wrong sync path
counts 97..137 — measured, held for twenty seconds — so **the state that most
needs the sync type re-probed was the one state that could never arm a
re-probe**. It now arms one, once per run of unusable counts.

Measured recovery from a source sync-type change, five runs: **~22 s before,
~1 s after**.

## The rate arm, and why it needs two confirmations

`sourceMoved()` also compares the line rate, because 320x256 at 50, 55 and 60 all
count 312 lines and the raster stays sized for whichever it solved first.
`HPERIOD_IF` states the rate for one register read, but it rails — and it rails
to values that pass every check `lineRateFromHPeriod()` makes:

```
HPERIOD_IF 511 -> 13183 Hz -> 42.4 Hz at 311 lines   (real: 15625 Hz, 50.08 Hz)
```

Bounds pass, the three-sample agreement passes because the rail is steady, and a
corroboration run across polls passes for the same reason. Firing on that gave a
re-solve every ~2 s on a settled source. So a candidate rate must both hold
across a steadiness run **and** agree with `getSourceFieldRate()`, which is
measured a different way and does not rail with it. The vsync spin is what the
cheap gate exists to avoid, and it is affordable here only because a corroborated
disagreement is rare.

## Settled elsewhere

A source sync-type round trip leaves the unit needing an input reselect, with the
encoder locked at 1920x1080/50Hz and a black picture. It is not caused by
anything here, and it is not the capture freeze either: `CAPTURE_ENABLE` is
enabled for 83% of the black state, sampled in a run. The cause is the sketch's
`getVideoMode()` reporting 0 against a source the sync processor is counting at
311 lines, after which its no-sync handling ratchets `ADC_SOGCTRL` down and
`updateSpDynamic()` stamps a sync-search configuration over the one
`SyncProcessor::applyForSyncType()` wrote.
`docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`.

What arms the mode changes is the sketch's latched SOG interrupt
(`Interrupts::takeSourceDisturbed()`), every ~5 s after such a round trip, each
one paying a sync type probe. `source moved:` in the console names which of the
three inputs armed each one, and it reports `interrupt` rather than `count` or
`rate` -- neither the line count nor the line rate has moved.
