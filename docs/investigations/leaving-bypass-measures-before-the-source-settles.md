# Leaving scaling-RGBHV bypass measures the field rate at 0.00 Hz first

`runSyncWatcher()`'s RGBHV steering leaves bypass by setting the sync path up,
measuring the source's field rate with `getSourceFieldRate(1)`, and choosing a
standard from that rate through `PresetLoad::rgbhvStandardFor(lines, rate)`.

**On a separate-sync source the first measurement is 0.00 Hz**, and the arm runs
again a few seconds later with the correct rate. Visible only because that rate
now reaches the console -- it was a bare `Serial.printf`, which goes to stdout
and not to `SerialM`.

**It does not happen on composite sync**, which is what makes it a race rather
than a missing setup: the same transition with the RISC PC on `SYNC 1` measures
50.08 Hz on the first leave, every time.

## Measured

Driven from ModeServ: `MODE X800 Y600 C256 F60` enters bypass, then
`MODE X320 Y256 C256 F50` leaves it. The console, timestamped:

```
32.35  evt,48149,rgbhv-leave-bypass,375,15
32.38  source absent: 311 lines, 1644 samples against divider 1180
32.40  evt,48194,rgbhv-leave-bypass,311,15
33.59  leaving bypass: 311 lines x 0.00 Hz
...
37.48  evt,53244,rgbhv-leave-bypass,311,15
38.20  leaving bypass: 311 lines x 50.05 Hz
```

The arm fires at **375 lines** -- a count no source runs, read while the RISC PC
is still changing mode -- and again 50 ms later at 311. The rate measured a
second after that is 0.00.

Reproduced on two builds. The count that reaches `rgbhvStandardFor()` is right
and the rate is not, so the standard is chosen against a zero.

The same run on composite sync, for contrast:

```
45.50  evt,1502894,rgbhv-enter-bypass,623,14
50.98  evt,1508303,bypass-switch,623,15
89.57  evt,1546990,rgbhv-leave-bypass,308,15
89.99  leaving bypass: 307 lines x 50.08 Hz
```

**Composite sync is SLOWER to enter bypass** -- 45 s against a few seconds on
separate sync -- which is its own trap: a snapshot taken 30 s after the mode
change shows a scaling solve at an intermediate count and reads as a source that
never enters bypass at all. It was recorded as one before this run.

## What it is not

**It is not the half-configured sync path.** The setup at that call site used to
spell out a subset of `SyncProcessor::applyForScalingRgbhv()` -- on separate sync
it omitted `SP_CLAMP_MANUAL`, `SP_SOG_P_ATO` and the vsync window -- which is a
plausible reason for a vsync measurement to time out. Tested directly by calling
the owner instead, so all of them are written before the measurement: **the
first rate is still 0.00 Hz.** The substitution is right for other reasons and
it does not fix this.

## What it is

`countHeldStill()` gates the arm on the line count holding still, and the count
does hold -- 311, correctly, 50 ms after a reading of 375. What it does not gate
is the **field rate**, which is measured afterwards by spinning for vsync edges
and times out while the source is still settling into the new mode.

So the arm has a steadiness run over one of the two measurements it uses. The
engine already holds both, measured behind its own run
(`SourceMeasurement::rateSettled()`), which is why the pass that follows reports
`sampling: 311 lines x 50.08 Hz` correctly while this one reports nothing.

## Why it self-corrects, and why that is not a fix

The bad standard loads, the source is re-examined on the next settled pass,
`rgbhv-keep-scaling` re-loads against the correct rate, and the picture is right
within about five seconds. That is the retry loop doing its job, so the cost is
latency and two extra preset loads rather than a wrong picture -- which is why
this has never presented as a fault.

It matters for the byte's retirement: `rgbhvStandardFor()` exists to turn a
(lines, rate) pair back into a standard byte so `applyPresets()` can dispatch on
it. Once the load takes an output rather than a standard, the arm should ask the
engine for the rate it has already measured instead of measuring again --
`docs/video-source-acquisition.md`, step 10.

The csync/separate asymmetry says where the race is. On composite sync the
separator is already slicing vsync out of the same signal the sync processor
counts, so a vsync edge is there to be found the moment the count settles. On
separate sync V arrives on its own pin through `SP_EXT_SYNC_SEL`, and that path
is what the sync-type probe has just been switching -- so the spin can start
before anything is driving it.
