# Every mode the RiscPC's monitor definition offers

A sweep of all 28 distinct geometries, measured and photographed against a
1024p output on a 320x256-tuned framing. Colour depth is excluded: it changes
nothing the scaler can see.

`exp` is the value the mode should give, `27e6 / (4 x lineRate) - 1`.

| mode | VTOTAL | lock | `HPERIOD_IF` | exp | line Hz | path |
|---|---|---|---|---|---|---|
| 240x352@70 | 448 | 1 | 214 | 214 | 31358 | scaled |
| 320x250@50 | 311 | 1 | 431 | 432 | 15575 | scaled |
| 320x256@50 | 311 | 1 | 431 | 432 | 15575 | scaled |
| **320x480@60** | 524 | **0** | 48 | 214 | 31440 | **no lock** |
| **320x480@73** | 519 | **0** | 176 | 178 | 37788 | **no lock** |
| **320x480@75** | 499 | **0** | 176 | 179 | 37425 | **no lock** |
| 360x480@60 | 524 | 1 | 213 | 213 | 31519 | scaled |
| 384x288@70 | 448 | 1 | 214 | 214 | 31396 | scaled |
| 480x352@70 | 448 | 1 | 214 | 214 | 31396 | scaled |
| 640x200@60 | 261 | 1 | 429 | 431 | 15626 | scaled |
| 640x250@50 | 311 | 1 | 431 | 432 | 15575 | scaled |
| 640x256@50 | 311 | 1 | 430 | 432 | 15575 | scaled |
| 640x352@60 | 363 | 1 | 308 | 308 | 21815 | scaled |
| **640x480@60** | 524 | 1 | **50** | 214 | 31440 | scaled, railed |
| **640x480@73** | 519 | **0** | 176 | 178 | 37788 | **no lock** |
| **640x480@75** | 499 | **0** | 176 | 179 | 37425 | **no lock** |
| 640x512@50 | 533 | 1 | 251 | 251 | 26735 | scaled |
| 768x288@50 | 311 | 1 | 431 | 432 | 15575 | scaled |
| 800x600@56 | 624 | 1 | — | — | — | bypass |
| 800x600@60 | 627 | 1 | — | — | — | bypass |
| **896x352@60** | 363 | 1 | **511** | 309 | 21758 | scaled, railed |
| 1056x250@50 | 311 | 1 | 431 | 432 | 15575 | scaled |
| 1056x256@50 | 311 | 1 | 431 | 432 | 15575 | scaled |
| **1280x480@60** | 524 | 1 | **50** | 214 | 31440 | scaled, railed |
| **1280x480@73** | 519 | **0** | 48 | 178 | 37788 | **no lock** |
| **1280x480@75** | 499 | **0** | 48 | 179 | 37425 | **no lock** |
| 1600x600@56 | 624 | 1 | — | — | — | bypass |
| 1600x600@60 | 627 | 1 | — | — | — | bypass |

**Eighteen of twenty-eight are healthy**, with `HPERIOD_IF` on its expected value
to within one.

## Three failure classes, and one of them is not a failure

**The `HPERIOD_IF` railing, at VTOTAL 524 and 363.** 640x480@60 and 1280x480@60
read a steady 50 where 214 is due, and 896x352@60 reads 511 where 309 is. This
is the fault `CLAUDE.md` already names — *a steady 50 measured at 640x480@60
where 213 was due* — and the sweep reproduces it on demand. It is **not
deterministic by line count**: 360x480@60 is the same VTOTAL 524 and reads a
correct 213, which matches the recorded 44% failure rate for that destination.

Read over eight samples it is not steady either: 640x480@60 gives
**212 ±162, flagged UNSETTLED**. So the mode is not merely wrong, it is
swinging — which is the instability seen on the screen.

**No lock at the 73 and 75 Hz 480-line modes**, and at 320x480@60. `PLLAD` never
locks, though `HPERIOD_IF` sits close to its expected value in several of them.
All are 37-38 kHz or the 524-line 60 Hz case. Nothing here has been diagnosed
further.

**Bypass at 800x600 and 1600x600** is the documented RGBHV trap, not a fault:
VTOTAL 624 and 627 are over the 535-line gate, so they are passed through rather
than scaled. See [../rgbhv-bypass-trap.md](../rgbhv-bypass-trap.md).

## The clipping at 640x480 is the untuned default, not a solver fault

The picture overflows the screen on both sides. The solve is self-consistent —
capture 889 units, produced 1204 px against a 1202 px display window, 2 px
cropped — so nothing is miscalculated. What is wrong is the **capture window**,
which `PanAndZoom.h` states is *a starting point for an untuned source, not a
derivation*: a fixed fraction of the input line that happens to suit the 15 kHz
modes and crops this one.

Panning and zooming out to `zh -48, zv -4, ph 37` shows the whole card, clean.
So the mode needs tuning rather than fixing, and the tuning is lost on the next
mode change — which is the case for
[../framing-presets.md](../framing-presets.md).

## Method

Driven from the host: `ModeServ` on TCP 6502 sets each mode, eleven seconds of
settling, then one pass reading every register the arithmetic uses, then a
photograph. The settling matters — `CLAUDE.md` records that raw sampling across
a sweep throws garbage at nearly every change that resolves on its own.
