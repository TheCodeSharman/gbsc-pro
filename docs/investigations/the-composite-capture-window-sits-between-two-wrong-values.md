# The composite capture window sits between two wrong values

On composite sync the horizontal capture window opens 174 units earlier than on
separate sync, and the picture sits right on screen. Forcing the separate-sync
window onto composite moves it too far the other way. **Neither value is
correct**, so this is not a case of one arrangement inheriting the other's
number — the truth is between them, and only a measurement says where.

## The whole placement chain differs in one pair

RISC PC on `vga` at 800x600@60, one cable, one raster, the sync type the only
variable — the machine sets it from CMOS, so `SYNC 0` and `SYNC 1` are one
ModeServ command apart. Both states `state: acquired`, both pictures full screen.

| field | separate | composite |
|---|---|---|
| `IF_HB_SP2` | 294 | **120** |
| `IF_HB_ST2` | 1384 | **1210** |
| `STATUS_SYNC_PROC_HLOW_LEN` | 176 | 173 |
| `STATUS_SYNC_PROC_HSPOL` | 1 | 0 |
| `SP_HS_INV_REG` | 1 | 0 |
| `SP_SOG_MODE` | 0 | 1 |
| `STATUS_SYNC_PROC_VTOTAL` | 627 | 623 |
| `HPERIOD_IF` | 177 | 176 |

Everything else in the chain is identical: `IF_VB_SP`/`IF_VB_ST`, `IF_HSYNC_RST`,
both scales, both memory windows, both display windows, both output sync pulses,
`VDS_HSYNC_RST`/`VDS_VSYNC_RST` and the divider. Same window width, 174 units
earlier, and a return to `SYNC 0` gives 294 exactly.

## Neither candidate is right, and that is the finding

Frozen, with the separate-sync window written onto the composite state, the
picture moves too far the other way — black at the right and the leftmost
castellation column cut, where its own window leaves black at the left instead.
Both bands are roughly equal by eye, about 180 and 190 px of a 1500 px picture,
which puts the correct value near the midpoint of 120 and 294.

**About half a pulse width**, which is what makes the measurement worth taking
rather than the difference being filed as a constant — because the two candidate
origins are a *whole* pulse apart, and the truth is at neither.

## The 174 units are one whole pulse, and the switch is the polarity bit

The gap is not a mystery quantity. `VideoSourceLine::forDuty()` takes the origin
end from `HsyncPulse::syncAtHead()`, which is a straight copy of the measured
polarity:

```cpp
return takeDuty(latched, HsyncPulse(duty, positive));
```

`syncAtHead` true means the pulse is leading blanking and video sits a pulse
behind the origin; false means video starts at the counter's own origin. So the
two arrangements are placed by different rules, and the difference between them
is exactly one pulse — 174 units against an `HLOW_LEN` of 173/176.

**The duty is not what differs.** Both readings are accepted: 176/1438 = 0.122
and 173/1438 = 0.120, well inside `forDuty()`'s 0.041..0.152, so neither state is
running on `FallbackDuty` and neither is in the complement state that
`STATUS_SYNC_PROC_HLOW_LEN` can latch. The window width is identical for that
reason. Only the origin end moves.

## Why the polarity bit cannot be trusted here

`normalisePolarity()` returns `STATUS_SYNC_PROC_HSPOL` as read *before* the
normalising write, and that bit reports the polarity of the signal arriving
**before the separator**. On composite sync the separator regenerates H, so its
output polarity is the separator's property rather than the incoming signal's —
and `syncAtHead` is therefore describing a signal the counter does not see.

Measured: `STATUS_SYNC_PROC_HSPOL` 1 on separate and 0 on composite, with
`SP_HS_INV_REG` following it, so the two arrangements reach the solver with
different polarity rather than normalised to a common shape. Normalising at the
hardware boundary is the standing rule and is not what happens on this path.

**That is the cause of the 174, and it does not give the answer.** Flipping
`syncAtHead` on the composite path lands on 294, which the forced-window
experiment above shows is also wrong. So the repair is not a corrected bit: the
origin has to be measured, and then whichever rule reproduces it can be written
down. If the true value is near the midpoint, neither end of the pulse is the
origin and a separator phase shift is the remaining explanation.

This is the capture-window half of the open polarity lead in
`../known-issues.md`, which records ten csync legs at 320x256@50 where five read
the duty as its complement.

## How to measure it

Creep, do not bisect and do not jump. `creep_window.py` is the worked pattern:

1. Put the source on `SYNC 1` and let it acquire.
2. Freeze automation, or the solver rewrites the window under the experiment.
3. Creep `IF_HB_SP2` one unit a press from 120 upward, moving `IF_HB_ST2` with it
   to hold the width, and read the boundary off the picture.
4. Keep creeping past the first clean edge — corruption comes in bands, so an
   edge found by halving is only the real one if nothing clean lies beyond it.

**Put content hard against both edges first.** At a default framing the last
thing on screen is captured input blanking, so "where the picture ends" measures
the source's border rather than the window. Zoom and pan until there is live
video either side of every boundary under test.

Read every register the arithmetic uses in one pass. The engine re-solves as the
measured field rate wobbles, so a capture from one solve paired with a window
from another invents a discrepancy.

## What it is not

**It is not the count shortfall.** `STATUS_SYNC_PROC_VTOTAL` reads 623 on
composite against 627 on separate, and `SourceMeasurement::reconciledFrame()`
already restores that — the vertical placement is byte-identical across the two
arrangements as a result, `IF_VB_SP` 21 and `IF_VB_ST` 625 on both. The
horizontal offset survives a correct vertical count, so it is a separate error.
[the-vertical-origin-follows-the-sync-type.md](the-vertical-origin-follows-the-sync-type.md)

**It is not the coast.** Coast stops serrations disturbing the PLL, and the RISC
PC's composite sync is not serrated.
[the-risc-pc-composite-sync-is-not-serrated.md](the-risc-pc-composite-sync-is-not-serrated.md)

[framing-is-anchored-to-a-measured-pulse.md](framing-is-anchored-to-a-measured-pulse.md)
is the horizontal axis's placement rule;
[the-duty-is-the-shorter-interval.md](the-duty-is-the-shorter-interval.md) is how
the pulse is read without consulting polarity.
