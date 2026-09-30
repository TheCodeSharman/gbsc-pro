# The encoder's window is placed per source, and the capture lands two pixels early

**The placement is per return of the sync pad, not per state, and the rows
below carry the window each mode change placed.** The same state reads 158.9
in Tier B and 172 in Tier C with every register identical: the sink chooses the
window's start when `PAD_SYNC_OUT_ENZ` is driven again, from the first non-black
content at the aperture's edge if that is earlier than its own position for the
raster, and a source `MODE` round trip does not toggle the pad and does not move
it. [the-encoder-places-its-window-when-the-sync-pad-returns.md](the-encoder-places-its-window-when-the-sync-pad-returns.md).
What stands here unchanged is the width, the capture side and the black counts;
what a "per (source, output) state" repeat measured was the same transition
taken the same way.

Every black column at an edge of the emitted frame has one of four owners, and
`tools/gbsc-pro-hwtest/framing_sweep.py` reads all four on one frame: the
encoder's window E (measured by walking our own blanking into it), the model of
it Em (`OutputMode::solve()`), the aperture A (`VDS_DIS_?B_*`), the written
picture P (the write model) and the card's green border C. The black at the left
is `C0 - E0 = (C0 - P0) + (P0 - A0) + (A0 - E0m) + (E0m - E0)`, which is
dCapture + dPlace + dModelApplied + dEncoder, mirrored at the right, top and
bottom; a negative term is clip. `framing_report.py` prints the table and refits
every walk and re-derives every verdict on load.

Measured on the bench RISC PC on `vga`, stock AKF50, automation frozen for each
judged clip, the link re-locked by a source `MODE` round trip before it, the
emitted frame read by the USB capture. Tier A is two sources at 60 Hz -- AKF50
640x480@60 (59.94 Hz stated, 60.00 measured; its display starts 6 px before the
DMT row it keys to, which the design predicts as +13 units of border at each
end) and 800x600@60 (identical to DMT, expected 0) -- into 1080p, 720p, 960p and
1024p.

## What is measured

| mode | output | T | Hz | black L/R/T/B | L: dEnc dModel dPlace dCap | R: dEnc dModel dPlace dCap | expected L/R | verdict |
|---|---|---|---|---|---|---|---|---|
| X640 Y480 C256 F60 | 1080p | 1600 | 60.00 | 9/0/0/2 |   +6.7   +0.0   +0.4      - |   -6.1   +1.0   -1.3   +8.4 | +13.1/+13.1 | L:dEncoder+clip R:dCapture T:clipped B:dCapture |
| X800 Y600 C256 F60 | 1080p | 1592 | 60.32 | 5/0/0/0 |   +2.3   +0.0   +0.1   +3.0 |   -2.3   +2.0   -1.4      - | +0.0/+0.0 | L:dCapture R:clipped B:dCapture |
| X640 Y480 C256 F60 | 960p | 1800 | 60.00 | 0/0/1/2 |   +1.4   -1.0   +0.6      - |   -0.6   +1.0   -0.1      - | +12.4/+12.4 | L:clipped R:clipped T:clipped B:dCapture |
| X800 Y600 C256 F60 | 960p | 1790 | 60.32 | 5/0/1/1 |   +2.0   +0.0   +0.2   +2.6 |   -0.2   +1.0   -1.1      - | +0.0/+0.0 | L:dCapture R:clipped T:dEncoder B:dModelApplied |
| X640 Y480 C256 F60 | 1024p | 1688 | 60.00 | 10/0/1/4 |   +7.0   -1.0   +0.9      - |   -6.4   +1.0   -1.1   +8.9 | +12.0/+12.0 | L:dEncoder+clip R:dCapture T:clipped B:dEncoder |
| X800 Y600 C256 F60 | 1024p | 1680 | 60.32 | 1/0/1/1 |   +1.7   +0.0   +0.3   +0.7 |   -0.4   +1.0   -1.8      - | +0.0/+0.0 | L:dEncoder R:clipped T:dEncoder B:dCapture |
| X640 Y480 C256 F60 | 720p | 2400 | 60.00 | 10/0/1/0 |  +11.9   -1.0   +0.5      - |   -9.7   +1.0   -1.2  +14.0 | +17.4/+17.4 | L:dEncoder+clip R:dCapture T:clipped B:dCapture |
| X800 Y600 C256 F60 | 720p | 2388 | 60.32 | 1/0/1/0 |   +1.8   +0.0   +0.0   +1.4 |   -0.3   +1.0   -0.1   +2.9 | +0.0/+0.0 | L:dEncoder R:dCapture T:dEncoder B:clipped |

Units are raster units, `T = VDS_HSYNC_RST + 1`; `dCap` is against the picture,
and `expected L/R` is what the mode file predicts for it. A `-` is a card border
off the frame, which the verdict names as clipped.

## The placement is deterministic, and the two instruments agree

Two acquisitions of 800x600@60 at 1080p put the window's start at 157.7 and
157.6; two at 720p at 396.2 and 395.9; three of 640x480@60 at 1080p at 153.3,
153.7 and 153.7, the last two identical to the column at every walk step. The
shown position is latched at lock and it lands in the same place each time, so
a difference between two states is a difference between the states.

The differenced strip and the black count read the same edge on the same
frames to a median of 1.1 units over 35 walked edges, worst 3.5, the count
reading outward at both ends by about a unit. The earlier twelve-unit
disagreement between the two on one state is not reproduced.

## The width holds and the start does not

The window's width is `T x carriedPx / totalPx` to within 0.0..2.2 units on all
eight states, so the fraction model of the span holds at 60 Hz as it did at 50.

Its start does not sit `+20` after the scaled sync and porch. On the 800x600
source the delay is 17.7, 18.0, 18.3 and 18.2 across the four outputs -- one
number, to within a unit, on rasters from 1592 to 2388. On the 640x480 source
it is 13.3 at 1080p, 13.0 at 1024p, 8.1 at 720p and 18.6 at 960p. Same
outputs, same clock, rasters within eight units of each other, and the window
opens five to ten units earlier for one source than for the other on three of
the four -- and in the same place on the fourth. Our output differs between
the two sources in nothing but the field rate the raster is run at: 60.00 Hz,
the CEA and DMT standards' own, against 60.32.

The whole window moves, not one end: `E1 - E1m` follows `E0 - E0m` on every
state. So the 9 to 10 black columns at the left of 640x480@60 at 1080p, 1024p
and 720p are the encoder's, and the same number of columns of picture is lost
off its right, while 800x600@60 shows 1 to 5 and loses 0 to 2.

What this refutes: the window does not open 13 units LATER than the model as
an uncharged `HsyncStartPx` would put it; it opens earlier, by 2 on one source
and by 7 to 12 on the other.

Not exercised: at 60 Hz every output, 720p included, runs the 108 MHz seed
(T 2400 at 720p, where 81 MHz would give 1800), so the clock axis the 50 Hz
measurement had is absent here.

## The capture's two-pixel remainder reaches the default framing

The capture window is placed exactly where the row says on every state --
start 0.180 and extent 0.800 of the line on 640x480, 0.2045 and 0.7576 on
800x600, to four figures -- and the card says the picture inside it sits later
than that. On 800x600@60, whose display IS the DMT active area, the left border
sits +3.0, +2.6, +0.7 and +1.4 units inside the picture's start (one to two
source pixels of the source's own blanking captured, harmless) and the right
border is off the frame at 1080p, 960p and 1024p and +2.9 inside at 720p. On
640x480@60 the right border reads +8.4, +8.9 and +14.0 against the +13.1,
+12.0 and +17.4 the mode file predicts.

That is the remainder `docs/known-issues.md` already carries under the
undoubled capture origin -- +1.1 source pixels on 640x480@60 and +3.7 on
800x600@60 by `counter_origin.py`, fitted as a line in sampling density whose
slope is the source's own video lagging its sync -- read here by a second
instrument, through the encoder's window at the default framing, on four
outputs. What it adds is the cost at acceptance: a source with no border loses
its last one to three pixels at the right on every output, and the fix the
plan names for a `dCapture` term is the row, not the window.

## The card at 960p

At 960p the 640x480 source's border is off the frame at the left, the right
and the top, with no black beside it: the window carries 1280 of 1800 units and
the picture fills it, so the two-pixel loss above and the design's own +12
units at the left both land off the frame.

## What the instrument could not read, and what was done about it

- The zoomed framing is chosen as the middle third of the line and frame, and
  on 800x600 that puts the card's own black under the bottom edge, so the
  vertical far walk changes nothing there. A fit through a constant is refused
  now (`transmitted_window.MIN_SLOPE_PX_PER_UNIT`) and the edge is unmeasured.
- On 640x480@60 at 720p the differenced strip found nothing at either
  horizontal edge and the black count served, at 1.03 columns per unit.
- An output request the engine had not honoured was accepted because the wait
  only asked for a line total that held still; it asks for the mode's own frame
  lines now, re-sends a letter the unit did not answer, and re-sends once more
  one it answered and did not apply. The loss reproduced twice more and
  `docs/known-issues.md` carries both of its mechanisms.
- A record was refused whenever a sync-processor counter read a unit either
  way between the two register reads around a clip; the solved registers alone
  decide it now, and the refusal names the field.

## Tier B: the rate axis at 1080p

Eight sources into 1080p, full probes, the same instrument:

| mode | output | T | Hz | black L/R/T/B | L: dEnc dModel dPlace dCap | R: dEnc dModel dPlace dCap | expected L/R | verdict |
|---|---|---|---|---|---|---|---|---|
| X640 Y480 C256 F60 | 1080p | 1600 | 60.00 | 8/0/0/2 |   +6.3   +0.0   +0.4      - |   -5.6   +1.0   -1.3   +8.3 | +13.1/+13.1 | L:dEncoder+clip R:dCapture T:clipped B:dCapture |
| X640 Y480 C256 F73 | 1080p>1024p | 1392 | 72.81 | 0/9/1/1 |   +0.5   +0.0   +0.2      - |   +1.2   +1.0   -1.7   +7.6 | +9.9/+9.9 | L:clipped R:dCapture T:dEncoder B:dEncoder |
| X640 Y480 C256 F75 | 1080p>1024p | 1350 | 75.00 | 0/32/1/1 |   +1.9   -1.0   +0.5      - |   +0.1   +2.0   -1.4  +20.1 | +22.4/+22.4 | L:clipped R:dCapture T:dEncoder B:dCapture |
| X800 Y600 C256 F56 | 1080p | 1706 | 56.25 | 8/10/0/0 |   +6.6   +0.0   +0.2      - |   -5.6   +0.0   -1.1  +17.2 | +18.6/+18.6 | L:dEncoder+clip R:dCapture B:dCapture |
| X800 Y600 C256 F60 | 1080p | 1592 | 60.32 | 4/0/0/0 |   +2.6   +0.0   +0.1   +1.7 |   -2.2   +2.0   -1.4      - | +0.0/+0.0 | L:dEncoder R:clipped B:dCapture |
| X320 Y256 C256 F50 | 1080p | 1916 | 50.08 | 21/1/2/0 |   +1.1   +0.0   +0.2  +21.2 |   -0.1   +1.0   -2.7      - | +0.0/+0.0 | L:dCapture R:clipped T:dCapture |
| X640 Y512 C256 F50 | 1080p | 1914 | 50.16 | 0/6/0/0 |   -4.3   +0.0   +0.4   +5.6 |   +5.1   +1.0   -1.3      - | +0.0/+0.0 | L:dCapture R:dEncoder+clip B:dEncoder |
| X640 Y200 C256 F60 | 1080p | 1604 | 59.87 | 16/0/2/0 |   +3.2   -1.0   +0.6  +10.4 |   -1.6   +2.0   -1.0   +2.4 | +0.0/+0.0 | L:dCapture R:dCapture T:dCapture B:dCapture |

Where the encoder cannot transmit 1080p at the source's rate -- 640x480 at
72.8 and 75 Hz -- the engine solves 1024p, and the row says so as
`1080p>1024p`; the model is the mode the registers carry.

**Where the model's rate clamp binds, the model is right.** At 72.8 and 75 Hz
into 1024p the scaled sync and porch plus the span no longer fit the line,
`OutputMode::solve()` pulls the start back to `T - FrontPorchMinPx - span`,
and the encoder's window sits within two units of that at both ends: dEncoder
+0.5 / +1.2 at 72.8 Hz and +1.9 / +0.1 at 75 Hz, the far end 14.8 and 15.9
units before the line's end against a front porch of 16. The 9 and 32 black
columns at the right on those two are the mode file's own border, captured as
the DMT row places it -- expected +9.9 and +22.4 units, measured +7.6 and
+20.1, the two-pixel remainder again -- and not the encoder's.

**Where it does not bind, the start is one number per source and no function
of the raster.** The delay after the scaled sync and porch, per source, all at
1080p and all at the 108 MHz seed:

| source | Hz | T | scan | delay | acquisitions |
|---|---|---|---|---|---|
| 800x600@56 | 56.25 | 1706 | flat | 13.4 | 1 |
| 640x480@60 | 60.00 | 1600 | flat | 13.3, 13.7, 13.7, 13.7, 12.3 | 5 |
| 640x200@60 | 59.87 | 1604 | doubled | 16.8 | 1 |
| 800x600@60 | 60.32 | 1592 | flat | 17.7, 17.6, 17.4, 17.3 | 4 |
| 320x256@50 | 50.08 | 1916 | doubled | 18.9 | 1 |
| 640x512@50 | 50.16 | 1914 | flat | 24.3 | 1 |

Fitted against T the six sources give a slope of 0.018 units per unit of
raster with residuals up to 3.7 units, and against the clock nothing, since
the clock is one value. A fraction of the line would give 0.087. The two 50 Hz
sources are two units of raster and 0.08 Hz apart and the window opens 5.4
units later on one than on the other; the two at 60 Hz are eight units and
0.32 Hz apart and differ by 4. Neither the raster total, the field rate, the
scan mode nor the line rate orders these six, and each repeats to under half
a unit. The encoder places its window by something in the signal this
instrument does not vary one at a time.

What the earlier 50 Hz measurement saw -- 15 black columns at the right on
320x256@50 and 6 on 640x512@50, none at 60 Hz -- is these rows: 640x512's
window opens 4.3 units after the model and closes 5.1 after it, so the
aperture's first units fall outside it and the last five of the window carry
no picture; 320x256's opens 1.1 before and closes on the model, and its 21
black columns at the left are captured source blanking (dCapture +21.2), the
15 kHz row's own placement, with one column at the right.

## Tier B: the same eight sources into 720p

| mode | output | T | Hz | black L/R/T/B | L: dEnc dModel dPlace dCap | R: dEnc dModel dPlace dCap | expected L/R | verdict |
|---|---|---|---|---|---|---|---|---|
| X640 Y480 C256 F60 | 720p | 2400 | 60.00 | 10/0/1/0 |  +13.0   -1.0   +0.5      - |   -8.1   +1.0   -1.2  +15.1 | +17.4/+17.4 | L:dEncoder+clip R:dCapture T:clipped B:dCapture |
| X640 Y480 C256 F73 | 720p | 1978 | 72.81 | 13/0/1/0 |  +12.2   +0.0   +0.5      - |   -8.3   +0.0   -0.8  +14.7 | +14.4/+14.4 | L:dEncoder+clip R:dCapture T:dEncoder B:dCapture |
| X640 Y480 C256 F75 | 720p | 1920 | 75.00 | 37/0/1/0 |  +32.7   -1.0   +0.6      - |  -28.4   +2.0   -1.9  +34.3 | +32.6/+32.6 | L:dEncoder+clip R:dCapture T:dEncoder B:dCapture |
| X800 Y600 C256 F56 | 720p | 1920 | 56.25 | 0/21/1/0 |   +0.3   +0.0   +0.5      - |   +1.2   +0.0   -1.0  +19.3 | +18.6/+18.6 | L:clipped R:dCapture T:dEncoder B:clipped |
| X800 Y600 C256 F60 | 720p | 2388 | 60.32 | 1/0/1/0 |   +2.2   +0.0   +0.0   +1.1 |   -1.3   +1.0   -0.1   +3.4 | +0.0/+0.0 | L:dEncoder R:dCapture T:dEncoder B:clipped |
| X320 Y256 C256 F50 | 720p | 2156 | 50.08 | 0/23/4/0 |  -17.8   +0.0   +0.2  +20.9 |  +19.1   +1.0   -2.7      - | +0.0/+0.0 | L:dCapture R:dEncoder+clip T:dCapture B:dCapture |
| X640 Y512 C256 F50 | 720p | 2154 | 50.16 | 3/1/1/0 |   +2.4   -1.0   +0.6   +2.6 |   +4.2   +1.0   -0.9      - | +0.0/+0.0 | L:dCapture R:clipped T:dEncoder B:dCapture |
| X640 Y200 C256 F60 | 720p | 2406 | 59.87 | 12/0/4/0 |   +0.6   +0.0   +0.2  +13.9 |      -   +0.0   -1.2      - | +0.0/+0.0 | L:dCapture R:clipped T:dCapture |

Here the clock axis IS exercised: at 50.08, 50.16 and 56.25 Hz the 108 MHz
seed would need a line over 2450 units and the engine takes 81 MHz (T 2156,
2154 and 1920), while the rest run 108 MHz. And here the placement spans
fifty units. The delay after the scaled sync and porch: 7.0 on 640x480@60.00
(8.1 on the earlier acquisition), 7.8 at 72.81 Hz, -12.7 at 75.00 Hz, 17.8 on
800x600@60.32 (17.9 and 18.2 earlier), 19.4 on 640x200@59.87, 19.7 on
800x600@56.25, 17.6 on 640x512@50.16 and 37.8 on 320x256@50.08. The two
states at T 1920 are one raster, 800x600@56 at 81 MHz and 640x480@75 at 108,
and the window opens 45 units apart on them: on the first the model is right
to a unit at both ends, and on the second the encoder opens 33 units before
it and closes 28 before, 37 black columns at the left and the same picture
lost at the right.

The width is the fraction of the line to within a unit at 81 MHz and three to
four units wide of it at 108 MHz.

The sources that are not at a standard's exact rate -- 59.87, 60.32, 56.25
and 50.16 Hz -- read 17.6 to 19.7 here and 16.8 to 24.3 at 1080p. Those at
one -- 60.00 Hz at both outputs, 72.81 and 75.00 Hz at 720p -- read 7 to 13
and -12.7, and 320x256 at 50.08 Hz reads 18.9 at 1080p and 37.8 at 720p. No
single term in T, the clock, the field rate, the line rate or the scan mode
orders the sixteen; each repeats to under half a unit.

## What follows

- **The width model holds and the start model does not.** `carriedPx /
  totalPx` of the line is right on every state at 1080p and 1024p and within
  four units at 720p; `TransmittedWindowDelayPx` at 20 is right for none of
  the sixteen states at 60 Hz and below, and where the rate clamp binds the
  clamp, not the delay, is what places the window.
- **The placement is a property of the (source, output) state**, repeatable
  to the column across mode-round-trip re-locks and across a session, so it
  can be measured once per state and would be worth holding as one; it is not
  a property of anything the engine solves from, so it cannot be derived.
- **What no constant can do**: a delay of 13 leaves black at the left of
  every state placed later, a delay of 20 loses picture at the right of every
  state placed earlier, and at 720p the states lie fifty units apart. The
  aperture can only cover the spread by being wider than the window at both
  ends, which loses picture off both edges on every state by up to the spread.
- **The capture's two-pixel remainder is the other half of the right-hand
  loss** and is a `dCapture` term, the row's to absorb.
- **The instrument's limits**: the zoomed framing's bottom edge lands in card
  black on some sources, so the vertical far edge is unmeasured there; the
  differenced strip found nothing on three doubled or 720p states and the
  black count served; 320x256@50 at 720p rests on the black count alone.

`tools/gbsc-pro-hwtest/sweeps/framing-20260930T*.jsonl.gz` are the records,
and `framing_report.py` renders any of them. `docs/known-issues.md` carries
what is open.

## Tier C: the 28 AKF50 modes into 1080p, full probes

Every mode the stock monitor definition offers, in the order `MODES` lists
them, one mode change each, the same instrument. Full probes rather than the
plan's `none`, because without the walks the card's columns are converted
through the model's window, and Tier B had measured that model wrong by up to
seven units, which would have landed inside the `dCapture` column this tier
exists to read.

| mode | output | T | Hz | black L/R/T/B | L: dEnc dModel dPlace dCap | R: dEnc dModel dPlace dCap | expected L/R | verdict |
|---|---|---|---|---|---|---|---|---|
| X240 Y352 C256 F70 | 1080p>1024p | 1448 | 70.00 | 20/0/1/3 |   +0.2   +0.0   +0.0  +14.1 |   -0.1   +1.0   -0.7      - | +0.0/+0.0 | L:dCapture R:clipped T:dEncoder B:dCapture |
| X320 Y250 C256 F50 | 1080p | 1916 | 50.08 | 7/15/15/10 |  -11.7   +0.0   +0.2  +21.7 |  +11.9   +1.0   -2.7      - | +0.0/+0.0 | L:dCapture R:dEncoder+clip T:dCapture B:dCapture |
| X320 Y256 C256 F50 | 1080p | 1916 | 50.08 | 7/15/2/0 |  -11.1   +0.0   +0.2  +21.2 |  +11.8   +1.0   -2.7      - | +0.0/+0.0 | L:dCapture R:dEncoder+clip T:dCapture |
| X320 Y480 C256 F75 | 1080p>1024p | 1350 | 75.00 | 0/18/1/1 |   -5.8   -1.0   +0.5   +7.7 |   +9.1   +2.0   -1.4      - | +0.0/+0.0 | L:dCapture R:dEncoder+clip T:dEncoder B:dCapture |
| X320 Y480 C256 F73 | 1080p>1024p | 1392 | 72.81 | 0/5/1/1 |   +1.3   +0.0   +0.2      - |   +1.1   +1.0   -1.7   +7.2 | +13.2/+13.2 | L:clipped R:dCapture T:dEncoder B:dCapture |
| X320 Y480 C256 F60 | 1080p | 1600 | 60.00 | 18/0/0/0 |   +1.7   +0.0   +0.4  +13.8 |   +0.3   +1.0   -1.3      - | +17.4/+17.4 | L:dCapture R:clipped B:dCapture |
| X360 Y480 C256 F60 | 1080p | 1600 | 60.00 | 96/96/0/0 |  +17.7   +0.0   +0.4  +83.9 |  -18.1   +1.0   -1.3 +134.3 | -99.2/+116.0 | L:dCapture R:dCapture B:dCapture |
| X384 Y288 C256 F70 | 1080p>1024p | 1446 | 70.08 | 11/0/1/4 |   +2.3   -1.0   +0.7   +6.7 |   -0.9   +2.0   -1.1      - | +0.0/+0.0 | L:dCapture R:clipped T:dCapture B:dEncoder |
| X480 Y352 C256 F70 | 1080p>1024p | 1446 | 70.08 | 1/0/1/3 |   +2.4   -1.0   +0.7      - |   -0.9   +2.0   -1.1      - | +136.9/-136.9 | L:clipped R:clipped T:clipped B:dModelApplied+clip |
| X640 Y200 C256 F60 | 1080p | 1604 | 59.87 | 13/0/2/0 |   +2.8   -1.0   +0.6   +9.7 |   -1.6   +2.0   -1.0      - | +0.0/+0.0 | L:dCapture R:clipped T:dCapture B:dCapture |
| X640 Y250 C256 F50 | 1080p | 1916 | 50.08 | 5/15/15/10 |  -10.9   +0.0   +0.2  +17.7 |  +11.1   +1.0   -2.7      - | -5.2/-5.2 | L:dCapture R:dEncoder+clip T:dCapture B:dCapture |
| X640 Y256 C256 F50 | 1080p | 1916 | 50.08 | 5/15/2/0 |  -10.9   +0.0   +0.2  +17.6 |  +12.8   +1.0   -2.7      - | -5.2/-5.2 | L:dCapture R:dEncoder+clip T:dCapture |
| X640 Y352 C256 F60 | 1080p | 1598 | 60.10 | 0/0/2/0 |   -0.8   +0.0   +0.4      - |   +1.8   +0.0   -0.2      - | +24.2/-7.9 | L:clipped R:clipped T:dCapture B:clipped |
| X640 Y480 C256 F75 | 1080p>1024p | 1350 | 75.00 | 0/34/1/1 |   +1.1   -1.0   +0.5      - |   +0.6   +2.0   -1.4  +20.0 | +22.4/+22.4 | L:clipped R:dCapture T:dEncoder B:dCapture |
| X640 Y480 C256 F73 | 1080p>1024p | 1392 | 72.81 | 0/8/1/1 |   +0.7   +0.0   +0.2      - |   +0.8   +1.0   -1.7   +7.5 | +9.9/+9.9 | L:clipped R:dCapture T:dEncoder B:dEncoder |
| X640 Y480 C256 F60 | 1080p | 1600 | 60.00 | 10/0/0/2 |   +7.7   +0.0   +0.4      - |   -6.9   +1.0   -1.3   +9.7 | +13.1/+13.1 | L:dEncoder+clip R:dCapture T:clipped B:dCapture |
| X640 Y512 C256 F50 | 1080p | 1914 | 50.16 | 0/6/0/0 |   -4.3   +0.0   +0.4   +5.5 |   +5.1   +1.0   -1.3      - | +0.0/+0.0 | L:dCapture R:dEncoder+clip B:dEncoder |
| X768 Y288 C256 F50 | 1080p | 1916 | 50.08 | 1/0/19/0 |   +0.9   +0.0   +0.2      - |   -1.0   +1.0   -2.7      - | +162.1/-172.6 | L:clipped R:clipped T:dPlace+clip B:clipped |
| X800 Y600 C256 F60 | 1080p | 1592 | 60.32 | 3/3/0/0 |   +0.1   +0.0   +0.1   +3.2 |   +0.0   +2.0   -1.4      - | +0.0/+0.0 | L:dCapture R:dModelApplied+clip B:dCapture |
| X800 Y600 C256 F56 | 1080p | 1706 | 56.25 | 8/10/0/0 |   +6.6   +0.0   +0.2      - |   -5.6   +0.0   -1.1  +17.2 | +18.6/+18.6 | L:dEncoder+clip R:dCapture B:dCapture |
| X896 Y352 C256 F60 | 1080p | 1602 | 59.94 | 15/0/2/0 |   +1.5   -1.0   +0.5  +11.7 |   -0.6   +1.0   -0.6      - | +0.0/+0.0 | L:dCapture R:clipped T:dCapture B:clipped |
| X1056 Y250 C256 F50 | 1080p | 1916 | 50.08 | 0/1/15/10 |   +0.0   +0.0   +0.2      - |   -0.1   +1.0   -2.7      - | +76.7/-90.7 | L:clipped R:clipped T:dCapture B:dEncoder |
| X1056 Y256 C256 F50 | 1080p | 1916 | 50.08 | 0/1/2/0 |   +0.0   +0.0   +0.2      - |   +0.8   +1.0   -2.7      - | +76.7/-90.7 | L:clipped R:clipped T:dCapture B:dCapture |
| X1280 Y480 C256 F75 | 1080p>1024p | 1350 | 75.00 | 9/33/1/1 |   +2.3   -1.0   +0.5      - |   -0.7   +2.0   -1.4  +20.5 | +20.8/+20.8 | L:dEncoder+clip R:dCapture T:dEncoder B:dEncoder |
| X1280 Y480 C256 F73 | 1080p>1024p | 1392 | 72.81 | 0/10/1/1 |   +0.7   +0.0   +0.2      - |   +1.1   +1.0   -1.7   +8.2 | +8.2/+8.2 | L:clipped R:dCapture T:dCapture B:dCapture |
| X1280 Y480 C256 F60 | 1080p | 1600 | 60.00 | 13/0/0/2 |  +10.0   +0.0   +0.4      - |   -9.4   +1.0   -1.3  +11.3 | +13.1/+13.1 | L:dEncoder+clip R:dCapture T:clipped B:dCapture |
| X1600 Y600 C256 F60 | 1080p | 1592 | 60.32 | 3/0/0/0 |   +2.6   +0.0   +0.1      - |   -1.9   +2.0   -1.4      - | +0.0/+0.0 | L:dEncoder+clip R:clipped B:dCapture |
| X1600 Y600 C256 F56 | 1080p | 1706 | 56.25 | 9/14/0/0 |   +6.6   +0.0   +0.2      - |   -5.3   +0.0   -1.1  +19.6 | +18.6/+18.6 | L:dEncoder+clip R:dCapture B:dCapture |

Units and columns as above; `expected L/R` is the mode file's own prediction
from `published_rasters.py`, in raster units.

- **The width holds on all 28**: `T x carriedPx / totalPx` to within 1.4 units
  at 1080p and one to three units wide at 1024p.
- **Modes sharing an output raster land together when the same thing is at
  the aperture's edge.** The seven 312-line modes run T 1916: the four with
  captured black at the edge read 170.9 to 171.7, the three whose picture
  fills the aperture, 768x288 and the 1056-wide pair, read 159.1 to 160.0.
  The nine at 449 lines and 70 Hz fall back to 1024p and read 331.6 to 333.8.
- **The mode file's layout predictions are read back where a border is on the
  frame.** 640x480@75 reads +20.0 at the right against +22.4 expected; 1280x480
  @75 +20.5 against +20.8; 800x600@56 +17.2 against +18.6; the 72.8 Hz trio
  +7.2, +7.5 and +8.2 against +13.2, +9.9 and +8.2; 320x480@60 +13.8 against
  +17.4. The two-pixel remainder is what separates them.
- **The different-layout class is as predicted**: 360x480@60 shows 96 black
  columns each side, its picture 84 and 134 units inside the window against the
  -99 and +116 the file predicts for the 640x480 row that answers it; 768x288,
  the 1056-wide pair and 480x352@70 are clipped on both sides, the row's
  picture area being narrower than theirs.
- **Vertically** the 250-line modes show the 15 and 10 rows the file predicts
  for the 256-line row that answers them; 768x288 loses 19 rows at the top.
- **What the record cannot say** is the sink's own position for each raster,
  since a transition places the window from a solve in progress; that needs
  the pad toggle the page above describes.
