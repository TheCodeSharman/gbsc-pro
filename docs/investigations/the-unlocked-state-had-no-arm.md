# The unlocked state had no arm

`VideoSourceAcquisition` named `SourceUnlocked` and then waited on it. The
divider in force was the reference one, the sync processor counted the source
perfectly against a line it was not clocking, and every arm in the ladder
declined because the source had not moved.

## The state

Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1`, output 1920x1080,
scaling:

```
state: unlocked, present: false
SP_SOG_MODE 1   STATUS_SYNC_PROC_VTOTAL 623   SP_H_PULSE_IGNOR 70
PLLAD_MD 2040   STATUS_SYNC_PROC_HTOTAL 975
```

2040 is the reference divider and 975 is what the sync processor counts through
it, so the ADC is not locked to the line being counted. The count itself is the
source's: 623 is what this source reads on the composite path, steady.

## What provokes it, and it is deterministic

**Selecting the input that is already selected.** Eight trials each, from a
settled acquisition, with a ten-second hold check after each:

| sync type | stalled |
|---|---|
| composite | **8 of 8** |
| separate | 0 of 8 |

A stall does not clear in 45 s. `/sc?~` does not clear it; an `ypbpr` then
`vga` bounce does, in 2 to 4 s.

**THE HOLD CHECK IS WHAT MAKES IT VISIBLE.** The same trial without one reports
5 of 6 acquiring, because `/input` is queued for `loop()` and the engine keeps
reporting the previous solve until it has been consumed -- so a wait that starts
at the request answers about the state before it.

## Why nothing armed

The console stops mid-sequence and says nothing further:

```
input selected: vga, reference divider 2040, applied +69ms
DETECT: 386ms, syncFound 1
frame time lock: the divider is not latched
source UNLOCKED: 623 lines, 976 samples against divider 2040
```

Against the same re-selection on separate sync, which runs the sync-type probe,
takes `DETECT: 2991ms, syncFound 3`, and goes on to `installing: divider from
line rate 37849, doubled 0 -> 1342`.

In `sourceMoved()` the count is plausible, it holds, and it equals
`solvedLines_`, so:

| arm | why it declined |
|---|---|
| unusable count | the count is inside the source bounds |
| unsettled count | it holds |
| first count | `solvedLines_` is not 0 |
| no source V sync | only asked on the separate path |
| count moved / rate moved | the source did not move, and it had not |

Only a solve rewrites the divider, and only an arm opens a solve. Nothing could
get out.

## The arm

`UnlockedArmPasses` of consecutive unlocked passes arms a re-measure, and the
run restarts so it arms again rather than once -- one attempt that does not take
is the difference between slow recovery and none. The count is sized as two
latch settles plus the run the count is read over, because a reading taken
inside a legitimate settle says nothing: the PLL relocks in its own time after a
solve writes the divider.

After it, 0 of 8 on both sync types, and a healthy solve is untouched --
composite still lands on 1436 with the same windows and the same emitted frame.

## Refuted

**"It does not reproduce outside the suite."** It reproduces in one request.
Seven narrower reproductions had each passed because none of them re-selected
the input already in force while the engine held a solve, and because an
acquisition check without a hold reports the previous solve.

**"One sample against an intermittent settles nothing, so the measurement is
several full runs per build."** It is not intermittent. A deterministic repro
answered in two minutes what was budgeted as an hour of full suite runs per
build.

**The pre coast is not the shake's cause.** A frozen walk of `SP_PRE_COAST`
inside one shaking acquisition read 2.573 at 7 against 0.006 at 2, with
`SP_POST_COAST` making no difference across 2..20 -- which looks like a clean
dependence and is not one. Engine-applied across repeated solves the shake
appears in about one acquisition in five whatever the pre coast is: 1 of 6 at
the default 7, and 1 of 4 at 2. So the pre coast is a lever inside a shaking
state rather than what decides whether a solve lands in one.
