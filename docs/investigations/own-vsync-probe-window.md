# The own-V-sync probe timed out on a source that has its own V sync

`SyncMeasurement::hasOwnVsync()` decides whether a source carries
composite sync or brings its own H and V. It cannot read the answer off the chip
-- `STATUS_SYNC_PROC_VSACT` reports the path already configured -- so it *moves*
the path, clearing `SP_EXT_SYNC_SEL`, and asks whether V still arrives.
`docs/sync-type-selection.md` is why that shape is the only one that works.

The window it waited in was too short for its own source.

## The measurement

Twenty-five probes on the bench RiscPC at 320x256@50 over VGA, which has separate
H and V sync. Time from the path switch to `VSACT` going high:

```
2 ms x13    3 ms x4    6    28    61    71    146    151    242    >250 (once)
```

A short mode with a long tail. The window was **250 ms**, and the largest
success in the sample was **242**.

## What a timeout costs

The probe returning "no own V sync" on a source that has one puts the sync
processor on composite separation. Measured in that state:
`STATUS_SYNC_PROC_VTOTAL` collapses from 311 to 97, `GBS_OPTION_SCALING_RGBHV`
goes to 0, and the picture goes with them. It does not clear itself quickly:
in the failing cycle the unit re-probed five more times, got the right answer
every time -- 28, 6, 2, 71 and 2 ms -- and was still counting 97 lines
thirty-five seconds later. Re-selecting the input is what repaired it.

This is the same class of fault `riscpc-no-sync.md` records as fixed on
2026-08-01: a separate-sync source programmed as composite sees no V sync at all.
That fix introduced the probe. This one is the probe's window, not its logic.

Reproduced at **1 cycle in 20** by `/sc?k` then `/sc?~`, which is a full
detection pass. A register dump of the failed state shows nothing that names the
cause -- the verdict is the only witness, and until the probe reported *why* it
said no, a timeout and a vetoed confirmation were indistinguishable from outside.
The confirming re-read never dropped one in the whole sample.

## Why the window is sized well past the tail

The error is one-directional.

A false **no** latches composite separation onto a separate-sync source and
takes the picture with it. A false **yes** cannot happen: `VSACT` only rises when
V actually arrives, so waiting longer cannot invent one.

So the window is not sized to the measured maximum, which would leave the same
tail crossing it on a slower day or another source. It is sized far past it, and
the extra wait is only ever paid where the timeout is the correct answer -- a
genuinely composite source, which has no V to arrive.

**WHAT IS NOT BOUNDED ANY MORE IS HOW OFTEN IT IS PAID.** This sizing rested on
the probe running once per source. `Geometry::useSyncTypeProbe()` runs it per
source MODE change, because a source can change its sync type without the mux
moving -- `sync-type-selection.md` -- so a composite source now pays
`OwnVsyncSettleMs + OwnVsyncWindowMs` on every mode change. Measured on the
bench RiscPC with `SYNC 1`, that is **1.00 s of a 2.0 s transition**, arriving
between the arm and the first measurement:

```
3.00  MODE sent
3.17  source moved: interrupt (308 lines, solved 308)
4.17  own V sync: no after 1000ms
5.04  sampling: 308 lines x 50.56 Hz -> line rate 15625
```

The window is still earned and must not be cut: the tail above is measured
AFTER the settle, the same 320x256@50 source once exceeded it, and a false no
does not clear itself.

**SO THE COST COMES OUT OF THE CADENCE INSTEAD, AND THE LADDER PAYS FOR BEING
WRONG.** A mode change reuses the held sync type and measures nothing. The
evidence that the held answer is wrong is already collected: the wrong path
counts 97..137 on a 311-line source, which `sourceMoved()` arms as an unusable
count, and that arm forgets the held type so the re-measure it opens probes
again. `SyncRecovery::ReprobeSyncType` at ladder pass 151 is the backstop for a
wrong path whose count stays plausible.

Measured end to end by changing the SOURCE's sync type under a running unit,
`SYNC 1` to `SYNC 0` over ModeServ:

```
2.79  source absent: 99 lines, 2050 samples against divider 2050
3.08  source moved: unusable count (99 lines, solved 308)
3.15  own V sync: yes after 2ms
4.02  sampling: 311 lines x 50.08 Hz -> line rate 15625
```

0.36 s, unattended, with no probe on any of the mode changes either side of it.
An ordinary mode change on a composite source went from 2.0 s of engine to
0.15 s.

**A vertical sync arrives every frame, so the tail is not one being waited for.**
At 50 Hz the source emits V every 20 ms and the 242 ms is twelve of them; what
takes that long is the sync processor re-acquiring after `SP_EXT_SYNC_SEL`
moves. `VSACT` is a processor state, not an event, which is why waiting longer
is the only thing that helps and why polling faster does not.

`SourceMeasurement::OwnVsyncWindowMs`.

## What reports it now

The probe logs `own V sync: yes after 3ms` to the console on every run, so the
distribution above can be re-taken on any source without a special build. The
constant is sized from that distribution and nothing else on the board measures
it.
