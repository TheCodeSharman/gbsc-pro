# The composite picture rolls, and the engine is not doing it

`vga` on composite sync carries two separate faults, and only one of them is
about stability. Both are measured with
`tools/gbsc-pro-hwtest/picture_stability.py` at 320x256@50 into 1080p, against
separate sync in the same session as the control.

## What is measured

**When composite acquires, the picture rolls vertically.** 17% to 24% of frames
sit at an offset from the position the clip holds, at 6.3 to 8.6 jumps a
second. The offsets are 2, 4 and 8 output rows, with occasional excursions of
107, 197, 212, 214 and 216. At 4.22 output rows to the source line that is half
a line, one line and two, with the large ones at 25 to 51 lines.

It rolls as a WHOLE: correlating the top, middle and bottom thirds of the frame
separately gives one offset, so it is not a tear.

**About half the time composite does not acquire at all.** The console says the
whole of it:

```
LOWPOWER: entered at t=...ms (no sync found)
frame time lock: cleanup, forgetting the output rate
DETECT: 6382ms, syncFound 1
frame time lock: not armed: the divider is not latched
```

and then nothing for the next fifty seconds. No `sampling:` line is ever
emitted, so the source is never measured. The state left behind is
`STATUS_SYNC_PROC_VTOTAL` 0, `STATUS_SYNC_PROC_HTOTAL` 0, `PLLAD_MD` 2040
against the 2200 a working composite solve holds, and a black output.

**`/geometry` reports `state: acquired` throughout that**, which is why
`acquired_and_settled()` returns true and an acceptance run goes on to measure
a black screen. A black capture is displaced in 0% of frames and reads as
perfectly steady, which is why `picture_stability.judge()` voids a reading with
no picture in it rather than passing it.

## The roll is below the engine

The console is silent about the picture for the whole of a window carrying 119
offset changes in 12 seconds -- `#5` heartbeats and nothing else. No solve, no
`source moved:`, no `sampling:`. The engine is not re-solving several times a
second, so whatever moves the picture is downstream of the decision to place
it.

The jumps are also too fast for a solve: they land on adjacent frames at 60
frames a second, several inside 100 ms.

## What is refuted

**The coast window does not cause it, and does not cause the short count
either.** `SP_PRE_COAST` swept 0..12 and `SP_POST_COAST` swept 0..12 on a live
composite state leave `STATUS_SYNC_PROC_VTOTAL` at 308 throughout. Readings of
304, 305 and 307 appear among them and are the HTTP point-read artefact rather
than movement.

**The short count is not the fault.** Composite counts 308 where the source
sends 311, three lines short, and the obvious reading is that the lines under
the vertical sync pulse are not counted. The Wii on `ypbpr` counts 524 where it
sends 525, one line short, through the same separator -- and holds still. So a
separator losing the lines its own sync pulse occupies is ordinary, and a short
count is not evidence of instability.

**The separator path is not inherently unstable.** The Wii on `ypbpr` runs
`SP_SOG_MODE` 1 with `SP_PRE_COAST` 7 and `SP_POST_COAST` 6 -- the same
configuration composite runs -- and reads 0 displaced frames with 9 lost of
492. Whatever is wrong is specific to composite sync on `vga`, not to the
separator.

## What the two paths differ in

Read on one source mode, one output and one capture, with only the sync
arrangement moved:

| | separate | composite |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 308 |
| `SP_PRE_COAST` / `SP_POST_COAST` | 0 / 0 | 7 / 6 |
| `SP_DLT_REG` | 0 | 192 |
| `SP_SOG_MODE` | 0 | 1 |
| `SP_HS_INV_REG` | 1 | 0 |
| `PLLAD_MD` / `STATUS_SYNC_PROC_HTOTAL` | 2200 / 2200 | 2200 / 2200 |
| frames displaced | 0% | 17% to 24% |

The divider is latched and correct on both, so the horizontal half is not where
this lives.

## What is open

The engine solves the capture from the count it measured, and on composite that
count is 308 against a source sending 311. Whether a capture window sized three
lines short of the field is what lets the vertical origin wander is **not
tested** -- it is a static error on the face of it, and a static error does not
roll. The experiment is to hold the capture at the extent 311 lines give while
composite is acquired, and read the displacement share either side of it.

The other candidate nothing has addressed is where 2040 comes from in the
failed state, given 2200 is what a working solve on the same source holds.
