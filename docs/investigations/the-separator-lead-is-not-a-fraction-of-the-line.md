# The separator's lead is not a fraction of the line

`VideoSourceLine::SeparatorOriginPerThousand` is 70, applied to every source the
sync separator regenerates hsync for — composite sync and sync on green both.
A single fraction cannot be right: measured on one machine, one input, one sync
type and one line rate, the lead is **7.6% of the line at 640x480@60 and 2.2% at
720x480@60**.

The two modes differ in sync width, 173 units against 104, which is the obvious
candidate for what the lead follows. Two points cannot fix a form, and the modes
that would give a third do not acquire on the composite path at all.

## What is measured

RISC PC on `vga`, `PATTERN CARD`, `RetroScaler-Acorn.mdf`, output 1920x1080,
read off the USB HDMI capture. The card's one-pixel green frame marks the
source's outermost active pixel, so the placement is a number rather than a
judgement.

| mode | counter | sync | sync type | lead | lead |
|---|---|---|---|---|---|
| 720x480@60 | 1442 | 104.2 u | separate | **−0.9 u** | −0.06% |
| 720x480@60 | 1442 | 104.2 u | composite | **31.1, 32.2, 36.4 u** | 2.2% |
| 640x480@60 | 1444 | 173.3 u | separate | **+5.4, +5.8 u** | 0.38% |
| 640x480@60 | 1444 | 173.3 u | composite | **109.8 u** | 7.6% |

**Separate sync lands on zero on both modes**, which is what the model states and
what makes the method trustworthy: the green frame sits within 7 units of 1443
of where DMT and CEA put active video, at both ends.

In time, at the same 31.75 µs line: the composite lead is 2.41 µs where the sync
pulse is 3.81 µs, and 0.73 µs where the pulse is 2.29 µs. So it is not a
constant time either, and not a constant fraction of the pulse — 0.63 against
0.32 of it.

## The Wii agrees with the narrow-sync reading

Wii on `ypbpr` at 480p, sync on green, the same CEA 720x480p raster and the same
104-unit sync, counter 1446. Its content cannot be read against the card, so what
it gives is a bound rather than a value: the menu's lit content spans capture
units **191.5..1348.7**, and content cannot lie outside active video, which CEA
puts at 205.6..1419.0 from the hsync leading edge. So

```
205.6 − L ≤ 191.5   →   L ≥ 14.1
1419.0 − L ≥ 1348.7 →   L ≤ 70.2
```

**The applied lead is 101 and the bound excludes it.** Neither end of that bound
assumes anything about the black border the source paints, because a border only
ever makes the lit region smaller.

The bound contains the RISC PC's composite figure for the same raster, ~33. So
the sliced sync-on-green front end and the logic-level composite one are not
distinguished by any measurement here, and the raster is what the two low
readings share.

## What it costs on the Wii

At the engine's own default framing the capture window's picture region is
107..1321 against content at 191.5..1348.7:

| | units | emitted px |
|---|---|---|
| black shown at the left | 84.5 | 134 |
| content **cut** at the right | 27.7 | 44 |

The window is pinned at the capture floor — `videoAt()` asks for 105 and
`firstCapture()` is 106 — so the lead is already clamped and still 84 units out.
Reading the black at the left as the whole fault misses that the far end is
losing picture.

## The capture floor is not safe on the separated path either

At 640x480@60 on composite sync the source's active video arrives at counter unit
**150** while `CaptureWindow::firstCapture()` is **168**. The left of the picture
is below the floor and cannot be captured at any framing — the card's left green
line is absent from the emitted frame while the right one is there with blanking
beyond it.

[the-separator-moves-the-counters-origin.md](the-separator-moves-the-counters-origin.md)
states the floor does not come down with the lead, measured at 320x256@50 where
the floor is 101 and the blanking reaches 88. That mode has room; this one does
not.

## How to measure it again

**Every conversion comes from the frame it belongs to.** Two instruments agreed
to 0.015%, which is what makes a one-edge reading usable:

- the **scale** from a `VDS_DIS_HB_ST` walk in the frozen state under test —
  emitted columns against the register, residuals ≤0.5 px over 1120 raster units
- the **anchor** from the black run our own display blanking leaves at the head
  of that same frame, with `VDS_Y_OFST` lifted so the run is ours alone
- cross-checked against a capture-edge walk, `IF_HB_SP2` against the lit column,
  residuals ≤0.7 px

**Force the sync-type re-probe and check it took.** `establishSyncType()` keys on
the INPUT selection, so a `SYNC` command leaves the engine on the path it held,
and the wrong path still counts a plausible line total here — 518 against 525 —
so the escalation ladder never fires. `/sc?~` forgets it. Read `SP_SOG_MODE` back
before measuring anything.
[a-sync-type-change-arms-no-probe.md](a-sync-type-change-arms-no-probe.md)

**A freeze across the sync change blocks the re-probe outright**, so the whole
state belongs to the previous sync type while every register reads settled.

**Gate on the divider, not on `acquired`.** A sheared state reports `acquired`
with the right line count and emits a diagonally smeared picture no edge finder
can read. `STATUS_SYNC_PROC_HTOTAL` against `PLLAD_MD` is the comparison with a
right answer, and it catches it.

**`VDS_Y_OFST` does not separate captured blanking from a source's black
border.** It lifts everything inside the display window, so both read as the
same grey. Only a source whose content is at its own active edges settles a
placement, which is why the card carries the measurement and the Wii carries a
bound.

## What is not settled

**The form.** Sync width is the candidate and there are two points. The modes
that would give a third refuse the composite path: 640x480@72 and 640x480@75
both go `absent` on it, and 640x480@72 shears when it does acquire.

**The 320x256@50 point.** The 76.5 units / 6.96% in the page above was taken with
a different method and has not been re-measured this way. It is the only doubled
mode in the set, and a line through the two undoubled composite points predicts 3
units there against 76.5 measured.

**Whether the sliced and logic-level front ends differ at all.** Sync on green
reaches the separator through the SOG comparator at `SOGIN` and composite sync on
`vga` through the dedicated HSync pin, but the only sync-on-green source here is
the Wii, whose content is not at its active edges. An RGBs source would put a
card through the sliced path; the port has nothing attached.
`docs/bench-sources.md`
