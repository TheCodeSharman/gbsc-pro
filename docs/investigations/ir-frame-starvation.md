# IR presses are dropped by starvation, not by misdispatch

**Status:** measured, closed.

Held keys go missing with the OSD open. Two consumers — `OSD_selectOption()` and
`OSD_IR()` — decode into the same global `results`, so the first question was
whether one of them was taking frames meant for the other.

## How it was answered

`IrReceiver` counts. Every decode in the sketch goes through it, so differencing
`decodes()` around each call attributes the frame without editing a single decode
site:

```cpp
uint32_t irBefore = irrecv.decodes();
OSD_selectOption();
uint32_t irAfterSelect = irrecv.decodes();
OSD_IR();
traceIrFrames(irAfterSelect - irBefore, irrecv.decodes() - irAfterSelect, ...);
```

A probe, not a policy: the counter only ever increases, and nothing steers on it.

## The answer

**Neither consumer.** Of 31 frames, 30 went to `OSD_selectOption()` and the odd
one to `OSD_IR()`, which is its job.

What drops the presses is the loop pass. It runs **353–1508 ms** with the OSD
open, against the remote's **~110 ms** repeat interval. A frame arriving before
the receiver has been resumed is discarded, so at that pass time most of a held
key's repeats land in the gap.

That makes the pass time the thing to fix, not the dispatch. `irWorstLoopMs` is
recorded and printed on every `traceIrFrames()` line for exactly that reason, and
a press that reached the geometry engine prints an `ADJ` line after it — so
"decoded but did nothing" shows as a trace line with no `ADJ` following.
