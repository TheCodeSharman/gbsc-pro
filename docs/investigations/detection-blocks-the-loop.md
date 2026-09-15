# Input detection blocks the loop, and the console can only see it as silence

Selecting an input costs seconds that no engine code is spending, and the
console says nothing at all while it happens -- because `loop()` is not running.

## What was measured

`/input?src=…` with the console timestamped, on the bench unit:

| | to first console line | total to `acquired` |
|---|---|---|
| `vga` | 2.52 s, 2.78 s | ~4.5 s |
| `ypbpr` (Wii, 480p) | **7.18 s**, 6.02 s | ~9 s, ~15 s |

The gap is one unbroken silence. Nothing prints, because `SerialM` reaches the
websocket from `loop()` and `loop()` is inside `detectAndSwitchToActiveInput()`.

Instrumented at the caller:

```
DETECT: 2524ms, syncFound 3        vga, exited early
DETECT: 6015ms, syncFound 2        ypbpr, ran the full timeout
```

## It is upstream's, and both loops are byte-identical

`detectAndSwitchToActiveInput()` holds two waits of `6000 ms`, each a
`while ((millis() - timeOutStart) < 6000) { delay(2); … }` that exits early when
the line count becomes a source count and otherwise walks the sync-on-green
slice level. Both are present unchanged in `upstream/main`.

What our fork changed is the early-exit predicate, not the structure:
upstream asks `getVideoMode() > 0`, we ask
`VideoSignal::countIsSource(SyncProcessor::lineCount())`.

## The ypbpr case returns the same value either way

The second loop returns 2 when it exits early and 2 again when it times out, so
the six seconds buy nothing but the sync-on-green walk -- and the engine then
takes several seconds more before its first measurement regardless. Whether the
walk is what finds sync on that input, or whether the timeout is simply being
paid for nothing, is not settled: the loop does not report which slice level it
was on when the count appeared.

## Why this is worth knowing before diagnosing anything

A blocked `loop()` looks exactly like a dead firmware from the console, and
exactly like a healthy one from HTTP -- `ESPAsyncWebServer` answers from network
callbacks. `/getreg` blocks with it, while `/freeze` answers instantly, which is
the signature that separates "wedged" from "inside detection".
