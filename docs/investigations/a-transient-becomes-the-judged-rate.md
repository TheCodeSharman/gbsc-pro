# A transient becomes the rate every later reading is judged against

A source mode change spent 2.28 s refusing a rate the engine had already
measured correctly, 59 times in a row.

## What was measured

Bench source on `vga`, driven over ModeServ from 320x256@50 to 640x480@60, with
the console timestamped throughout:

```
 46.41  sampling: 524 lines x 45.98 Hz -> line rate 24142
 46.45  sampling: 524 lines x 60.36 Hz -> line rate 0
 ...                                                       59 of these
 48.73  sampling: 524 lines x 60.36 Hz -> line rate 0
 48.77  sampling: 524 lines x 60.36 Hz -> line rate 31690
```

59 refusals, 46.45 to 48.73. The run ends exactly where `HeldRateRejectionLimit`
(60) drains, not where the source does anything.

## Why the first reading is admitted

`rateFollowsCount()` accepts any rate at a moved count, and is right to: a moved
count IS a mode change, so the rate is expected to move with it. The reading at
46.41 arrives while the count has just gone 311 -> 524, so nothing judges it.

45.98 Hz is not a rate the source ever ran. It is the field rate timed across the
moment the source changed mode, and it sits comfortably inside the plausibility
band -- which is the whole difficulty, because a bounds check cannot see it.

## The defect is what happened next

That reading was promoted straight to the pair every LATER reading is judged
against. From then on the count was unchanged at 524, so `heldRateJudges()` held,
and every correct 60.36 Hz reading disagreed with the transient and was refused.

The refusal budget is the only thing that ends it, and it exists for a different
purpose -- a source whose period genuinely wanders must not hold a mode change
open for ever. Here it is draining against a source that is not wandering at all.

## The fix, and the rule that has no exceptions

The reported rate and the judged rate are split. `lineRateHz()` still answers
with the last rate accepted, which is what a reader through a sync loss wants.
What `rateFollowsCount()` and `heldRateCorroborates()` judge against is promoted
only where `rateSettled()` has confirmed the reading agrees with itself.

**One weaker rule was tried and refuted.** Promoting at once "where something
judged this reading" -- nothing held yet, or a count that did not move -- looks
sufficient and is not: `VideoSourceAcquisition::rateMoved()` calls
`forgetHeldRate()` on a confirmed rate change, so nothing is held at exactly the
moment a transient arrives, and it is admitted again.

## What it bought, measured on the same transition

| | before | after |
|---|---|---|
| 320x256@50 -> 640x480@60 | 3.43 s | 1.33 s, 1.26 s |
| 640x480@60 -> 320x256@50 | 11.9 s, 5.6 s, 5.1 s | 5.23 s, 3.11 s |
| refusals | 59 in one transition | 3 across four |

The remaining cost on the 640x480 -> 320x256 leg is a different fault, recorded
in `the-count-is-unstable-while-the-divider-describes-the-last-mode.md`.
