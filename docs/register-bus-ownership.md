# The TV5725 register bus has one owner

`loop()` is the only thing that reaches a TV5725 register. Everything arriving
from outside it submits work and waits.

**This is a debug facility, not a release feature.** Its only clients are
`/getreg`, `/getregs` and `/setreg`, which exist for the bench tooling, and the
queue serving them is compiled under the same `#if GBS_DEBUG` they are. A
`GBS_DEBUG=0` build has none of it — no routes, no queue, no `loop()` call — and
therefore nothing outside `loop()` that can reach the bus at all. The hazard
below is one the debug endpoints create, and the queue is what makes them safe
to use on a running unit.

## The hazard

Reaching a register is **two I2C transactions** — aim the segment pointer at
`0xF0`, then read or write the register itself. An access landing between the
two uses the segment pointer the first one just set, so it reads or writes the
wrong bank.

Measured 2026-08-09: segment 1 holding segment 3's bytes, `IF_HB_ST` reading
`VDS_DIS_HB_ST`'s **1348** — 71 units past the end of a 1277-unit line. The
symptom on screen is a zigzag on every vertical edge.

Two other explanations were tested first and are refuted: a stale segment-pointer
cache (the cache is never revalidated, but revalidating it does not help), and
the raw `writeOneByte(0xF0, n)` sites reached directly.

## Why the web server is the whole boundary

`ESPAsyncWebServer` serves from network-stack callbacks, **not** from `loop()` —
the same property behind the "HTTP answering does not mean the firmware is
running" trap in `CLAUDE.md`. So a route that touches the chip runs concurrently
with `loop()`, and `/getreg`, `/getregs` and `/setreg` were the only ones that
did. Everything else already defers the way `/sc` does, and the OSD's interrupt
handlers set a flag rather than write a register.

Measured: with those three idle, 30 s of OSD and sync watcher activity entered
the register path re-entrantly **zero times**. Nothing else needs to move.

## How the deferral works

`src/net/RegisterQueue.h`. A handler fills a `Job` and calls
`submitRegisterJob()`; `loop()` calls `serviceRegisterQueue()`, which claims one
job, performs the I2C and answers the parked request.

`Capacity` is 4 — the three routes plus one in flight. `submit()` returns false
when full, which the handler turns into a status the client can retry on rather
than a parked request nobody ever answers.

The routes, the queue, both functions and the `loop()` call are all inside
`#if GBS_DEBUG`, so a release build carries no part of it. Anything added that
reaches the chip from a web handler has to be gated the same way, or it
reintroduces the second bus user this exists to remove.

## Cancellation, which is the delicate part

Queueing is easy; the lifetime is not. The handler parks its
`AsyncWebServerRequest` and `loop()` answers it later, so a client disconnecting
in between leaves `loop()` holding a pointer the server has already freed.

The sequence is `claim()`, then `claimCancelled()` **after** the I2C and before
answering, then `complete()` either way. `cancel()` runs from the disconnect
callback: it drops a job that has not been claimed yet, and marks the in-flight
one so `loop()` knows not to answer it.

The token is opaque and never dereferenced by the queue, which is what lets it
compile and be tested on a host — `test/test_register_queue.cpp`.

## Checking it still holds

`tools/gbsc-pro-hwtest/race_probe.py` provokes the fault and needs no special
build — it drives `/getreg`, `/getregs` and `/setreg`, then watches `IF_HB_ST`
and reports any value outside the 1277-unit line.

```sh
python3 tools/gbsc-pro-hwtest/race_probe.py --host <ip>
python3 tools/gbsc-pro-hwtest/race_probe.py --host <ip> --frozen
```

Three ingredients are all needed, which is why the fault took a 134-second suite
run to hit once and this hits it in under a minute: pad presses, so the geometry
engine writes segments 1 and 3 in bursts; whole-segment reads, which hold the
handler in I2C for 256 registers; and a watcher, because the damage is silent
until something reads it back.

This is the regression check for the queue. A new route that reaches the chip
from a handler rather than submitting a job would show up here, and nowhere
else — the symptom on screen is an intermittent zigzag.
