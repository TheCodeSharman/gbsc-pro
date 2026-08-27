# The own-V-sync probe timed out on a source that has its own V sync

`SourceMeasurement::sourceHasOwnVsync()` decides whether a source carries
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
what that costs is bounded: the probe runs **once per source**, not once per mode
change, and the extra wait is only ever paid where the timeout is the correct
answer -- a genuinely composite source, which has no V to arrive.

`SourceMeasurement::OwnVsyncWindowMs`.

## What reports it now

The probe logs `own V sync: yes after 3ms` to the console on every run, so the
distribution above can be re-taken on any source without a special build. The
constant is sized from that distribution and nothing else on the board measures
it.
