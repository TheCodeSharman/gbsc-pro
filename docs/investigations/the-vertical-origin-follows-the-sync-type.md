# The vertical capture origin follows the sync type

The vertical capture window is placed seven counter units late on every sync
arrangement except separate sync. On sync-on-green the picture tears and wraps;
on composite sync the top of the picture is lost and the rest is pushed down the
screen. Separate sync is correct, and it is the only arrangement the placement
was measured on.

`VideoSourceLine::frame()` builds the vertical line with no sync interval at all
-- no `syncUnits`, no head blanking -- while the horizontal line is built by
`forDuty()` from the *measured* hsync pulse. The vertical axis therefore has no
representation of where the input formatter's line counter zeroes relative to the
source's vertical sync, and that origin is not the same on every sync type.

## What it replaced

`VideoSourceLine::FrameLagUnits` held **-7** counter units and was applied in four
places across two classes: `videoAt()`, `fractionAt()`, `firstCapture()` and
`lastReachable()`. It was removed on the premise that what it compensated for --
both vertical placements counting the vsync pulse as leading blanking while the
counter zeroes on the pulse's trailing edge -- had been fixed at its source.

That premise is true for separate sync. It is a statement about how vertical sync
is *extracted*, and composite sync and sync-on-green extract it differently, so
the correction lands in the wrong place on both.

## The measurement

One input, one cable, one raster, `vga` at 800x600, the sync type the only
variable -- the RISC PC sets it from CMOS, so `SYNC 0` and `SYNC 1` are one
command apart:

| sync type | `IF_VB_SP` | `IF_VB_ST` | `STATUS_SYNC_PROC_VTOTAL` | picture |
|---|---|---|---|---|
| separate (`SYNC 0`) | 21 | 625 | 627 | clean, full screen |
| composite (`SYNC 1`) | 36 | 622 | 623 | top of the card absent, content pushed down and right |

The frame count itself differs, 623 against 627, so the vertical measurement is
not merely displaced on composite sync -- the counter is counting a different
frame.

On the Wii at 480p on `ypbpr`, sync on green, the same solve either side of the
removal:

| | `IF_VB_SP` | `IF_VB_ST` | horizontal | picture |
|---|---|---|---|---|
| with the lag | 21 | 505 | 99 / 1315 | clean, full screen |
| without it | **28** | **512** | 99 / 1315 | torn, wrapping at a moving seam |

Same window height, both edges seven units later, horizontal untouched. So
sync-on-green wants the placement seven units earlier than separate sync does,
which is the whole of what the deleted constant was worth on that source.

## Why a register dump cannot see it

`/geometry` reports the **framing** -- where the user put the picture as a
proportion of the source -- and the framing is identical either side of the
change: `ov` 30, `ev` 480, `ch` 1449, `cv` 525 on both. The displacement lands
only in `IF_VB_ST`/`IF_VB_SP`, which `/geometry` does not carry. Every other
register in the solve is byte-identical, `PLLAD_MD` 1448 against
`STATUS_SYNC_PROC_HTOTAL` 1448 and `HPERIOD_IF` 214 among them.

The picture is the only instrument, and it needs a settle: for about a minute
after the source is acquired the output is blank -- white on one build, black on
another -- and a photograph taken when `state` first reads `acquired` shows
nothing wrong on a build that is badly broken. Judging a frame taken at the
moment of lock produced false verdicts in both directions.

## What a bisect costs here, and the oracle it needs

Acquisition on `ypbpr` is itself unreliable across a wide span of history --
three consecutive input bounces on one build gave a grey field, then no signal,
then a solve stuck on the previous input's geometry. **Acquisition rate is not a
usable oracle.** Picture quality after a settle is: every build tested was
either clean on every acquire or torn on every acquire, with no build sitting
between.

## What is not established

The correct origin for **composite sync** is not measured. Sync-on-green wants
seven units earlier than separate sync; composite sync is displaced further and
also counts a shorter frame, so the two corrections are not assumed to be the
same number. A value for it has to come from the bench.

Whether the offset is a property of the sync type alone, or of the source's
vertical sync width, is also open. Seven units is one number measured on two
sources; the sync width is the quantity a placement rule would naturally be
written against, and the horizontal axis already takes its exclusion from a
measured pulse rather than from a constant.

## The shape the fix has to take

A constant applied at four call sites, compensating for an error owned by a
different class, is not something any of those call sites can be read against.
The origin belongs to whatever determines the sync arrangement, derived once from
the measurement and handed to `CaptureWindow` the way the hsync pulse already is
-- `docs/sync-type-selection.md` is where that choice is made.

Restoring the constant would restore the picture and reinstate exactly the
arrangement that hid the fault.

[the-capture-lag-was-the-retiming-bypassed.md](the-capture-lag-was-the-retiming-bypassed.md)
is the horizontal constant that turned out to be one misconfigured bit;
[the-vertical-capture-window-is-placed-late.md](the-vertical-capture-window-is-placed-late.md)
is where -7 came from and how a one-source-line feature made it measurable;
[framing-is-anchored-to-a-measured-pulse.md](framing-is-anchored-to-a-measured-pulse.md)
is the horizontal axis doing what the vertical one does not.
