# `IF_HBIN_ST` blanks the captured tail, on a line-doubled source only

The coloured bar at the right-hand end of the picture on the bench RiscPC is
removed by writing `IF_HBIN_ST`, with the capture window held and the picture
unmoved. Nothing else on the input formatter reaches it.

`InputFormatter::applyLineDoubling()` owns the field and writes it per solve:
`DoubledTailBlanking` on a doubled line and 0 on a progressive one.

The bar itself is described in
[the-bar-at-the-right-edge-is-captured-line-tail.md](the-bar-at-the-right-edge-is-captured-line-tail.md).
This page is what moves it and what does not.

## Reproduction

At 320x256@50 on `vga`, default framing, `PATTERN PM5544`, a source mode round
trip leaves it:

```sh
printf 'MODE X800 Y600 C256 F60\n' | nc 192.168.88.10 6502   # wait ~16 s
printf 'MODE X320 Y256 C256 F50\n' | nc 192.168.88.10 6502   # wait ~18 s
```

**It arrives on some trips and not others** -- 4 of 6 identical trips at the
default framing. Adding `/uc?x` either side of the mode change leaves it at
about the same rate, and **that toggle does not enter bypass**: measured over
four entries at 800x600, `DAC_RGBS_ADC2DAC` and `OUT_SYNC_SEL` stay 0 and
`PLLAD_MD` never moves off 2039. So nothing here says bypass is involved, and a
plain mode round trip is the whole reproduction.

Strength varies by more than an order of magnitude between instances, from a
peak of 14 grey levels to 100 over a background of 0, and the colour varies with
it. Establish that it is on screen before designing anything around it.

## Measuring it

`tv-snap` rectified is 1280x720 of panel. A column mean over rows 100..620 in
`gray` reads the bar against a background of 0, because it stands in the black
beyond the picture rather than against picture content:

```sh
ffmpeg -v error -i shot.jpg -vf "crop=1280:520:0:100,scale=1280:1:flags=area,format=gray" \
       -f rawvideo -pix_fmt gray -
```

The bar occupies photo columns 1216..1253 at the default framing on this camera
position. Two things beside it are not the bar: a bezel feature at 1236..1240
reading about 20 in an RGB mean, and the picture's own right edge near 1012.

Held still it is stable: 16 samples over 80 s gave width 21 columns, peak 15-16,
sum 364..405.

## What removes it

`IF_HBIN_ST` 0 upward, capture window held at `IF_HB_SP2` 133 / `IF_HB_ST2`
1087, `PLLAD_MD` 2206. The picture does not move -- the card's right edge stays
at photo column 1021-1023 throughout:

| `IF_HBIN_ST` | bar columns | width | peak |
|---|---|---|---|
| 0 | 1216..1253 | 38 | 56 |
| 2 | 1219..1249 | 31 | 55 |
| 6 | 1222..1247 | 26 | 52 |
| 10 | 1224..1245 | 22 | 49 |
| 14 | 1227..1244 | 18 | 46 |
| 18 | 1230..1243 | 14 | 42 |
| 22 | 1233..1242 | 10 | 36 |
| 26 | 1236..1241 | 6 | 23 |
| 32 and above | -- | 0 | 6 |

It shrinks from **both** ends and dims, and the value only takes effect every
fourth unit -- the rows between those listed read identical to the one above
them.

Two negatives that bound the mechanism. A write of the value the register
already holds does nothing, so it is not a strobe. A change of one unit does
nothing, so it is the value rather than the transition.

Above about 320 it starts to move the picture: the card's right edge runs
1021 -> 1027 -> 1030 -> 1036 for `IF_HBIN_ST` 320 / 640 / 1000.

## What does not remove it

Every one of these was measured against a live bar, with the bar's width and
peak read off the photograph either side:

| written or pulsed | bar | picture |
|---|---|---|
| `IF_HBIN_SP` 272 -> 290 | unchanged (1513 -> 1460, back to 1512) | pans, right edge 1020 -> 1005 |
| `IF_LD_ST` 5 -> 25 | unchanged | unmoved |
| `IF_LD_WRST_SEL` 1 -> 0 | unchanged | unmoved |
| `IF_LD_ST` 5 -> 25 with `IF_LD_WRST_SEL` 0 | unchanged | unmoved |
| the same, after an `SFTRST_IF_RSTZ` pulse | unchanged | unmoved |
| `SFTRST_FIFO_RSTZ` pulsed | unchanged | unmoved |
| `SFTRST_MEM_FF_RSTZ` pulsed | unchanged | unmoved |
| `SFTRST_IF_RSTZ` pulsed | unchanged | unmoved |
| `PAD_SYNC_OUT_ENZ` toggled, 8 times | unchanged, 976-column card every time | unmoved |

So the two fields of the hbin window do different jobs: **`IF_HBIN_SP` pans the
picture and `IF_HBIN_ST` blanks the tail**, and neither does the other's.

`IF_LD_WRST_SEL` closes an open question in
[if-hbin-second-capture-window.md](if-hbin-second-capture-window.md). That page
carries two negatives for the bit and notes that neither included an IF block
reset, so a selector latching only on one would look the same as a selector that
does nothing. With the reset included it still does nothing, and `IF_LD_ST` --
the line-generated write reset position the bit is supposed to select between --
is inert under both of its settings. The bit stores a value and reads it back
while nothing behind it acts, which is the `HD_HSYNC_RST` case in
`docs/tv5725-chip.md`.

## The registers do not distinguish the two states

Captured with `snapdiff.py --save` either side of one `IF_HBIN_ST` write and its
return, bar plainly present in the first and absent in the second:

```
0 bytes differ, resolving to 0 fields
```

All 1536 registers are byte-identical. A register dump therefore cannot tell
whether the bar is on screen, and a diff taken across a state where it appeared
or vanished says nothing about why.

## The bar is an INSTANCE, and the state that carries it is in no register

Nothing in the configuration distinguishes a unit showing the bar from the same
unit not showing it, so what carries it is latched state in the capture and
scaling path rather than a placement the registers describe. Four measurements
say so together:

- all 1536 registers are byte-identical across one appearance and one
  disappearance, above
- an `IF_HBIN_ST` excursion clears some instances **permanently** -- put the
  register back and the bar does not return, and only a further source mode
  round trip brings it back. Other instances survive the same excursion
  unchanged, three times running, at the same register values
- strength varies by more than an order of magnitude between instances, from a
  peak of 14 grey levels to 100
- a mode round trip lands it on 4 trips of 6, from the same source in the same
  mode

**So an instance is the unit of measurement.** A framing compared against
another framing is only meaningful inside one instance, and an absolute width or
peak carried between instances is not a reading. Establish the bar is on screen
before every measurement, and take any comparison by moving one thing and
putting it back within the same instance.

The window's placement is still what the bar is made of -- it is captured
content that follows the source, and a pan moves it -- but placement alone
cannot be the whole account, because the same window and the same source give it
on some instances and not others. Whatever `IF_HBIN_ST` perturbs is upstream of
the write and holds across a register write and its return.

**And the instance carries a pan as well as a bar.** At the same registers the
picture sits about 38 IF units to the left whenever the bar is there, which is
the same displacement seen from the other end.
[the-pan-is-downstream-of-the-scaler.md](the-pan-is-downstream-of-the-scaler.md)
The pan belongs to the instance rather than to this field: raising `IF_HBIN_ST`
does not move the picture at any value to 160, measured below.

## It is captured content, not stale memory

Swapping the source pattern changes the bar and swapping back restores it
exactly -- `PATTERN PM5544` peak 47, `PATTERN CARD` peak 56, `PATTERN PM5544`
peak 47 with the same columns. So it follows the source, which rules out an
unwritten region of the frame buffer showing what was last there. That artefact
is real and is a different object; the bar page above tells them apart.

## Which solve the trip lands on decides whether it appears

A mode round trip settles on one of two samplings, and only one of them shows
the bar:

| `PLLAD_MD` | `IF_HSYNC_RST` | capture | capturable | `VDS_HSCALE` | card on panel | bar |
|---|---|---|---|---|---|---|
| 2206 | 1103 | 133..1087 | 1024 | 546 | 931 columns | usually, up to peak 100 |
| 2280 | 1140 | 169..1110 | 1010 | 539 | 976 columns | none, in every sample taken |

The card is 4.8% wider on the panel in the second, which is the window taking a
smaller fraction of the line and magnifying it further -- 0.865 of the line
against 0.826, a ratio of 1.047 against the 1.048 the card widths give. Nothing
panned; the two solves magnify differently.

**The second is protected by the write limit rather than by placement.** At
`PLLAD_MD` 2280 the IF line is 1140 units, past `VideoSourceLine::WriteLimitUnits`
of 1125, so `lastCapture()` clamps and the window stops short of the source's
active video. At 2206 the line is 1103, the clamp does not bind, and the window
runs to 0.986 of the line -- over the end of the picture and into the tail.

So the bar is the capture window overrunning, exactly as the bar page says, and
what varies between trips is which divider the engine lands on. `PLLAD_MD` 2280
also puts the line 15 units past the write limit, which is its own question:
`SourceMeasurement` is supposed to cap the divider to keep the line inside it.

## Where the fix has to go

Both halves of the hbin window are owned per scan mode by
`InputFormatter::applyLineDoubling()`, which runs on every solve. Before that
`IF_HBIN_ST` was written only in `InputFormatter::init()`, which runs from
`BringUp::init()` when `BringUp::armed()` -- armed by `setOutModeHdBypass()` and
`bypassModeSwitch_RGBHV()` and by nothing else -- so a mode change did not write
it and its value was whatever the last arm left.

The alternative the field replaces is a tail guard in
`VideoSourceLine::lastCapture()`, which was written, host-tested and reverted.
It works and costs three things this route costs none of: the default framing
keeps its width and slides 24 units left to fit the shorter span, pulling in
back porch; zoom-out reach drops from 1013 units to 978; and it encodes one
source's front porch globally, which nothing on the chip can measure.

## What is left open

**Why the divider lands on 2206 on some trips and 2280 on others**, from the
same source in the same mode.

A second instrument says the same thing and names the healthy one.
`test_capture_origin.py::test_the_capture_starts_a_lag_after_the_sync_edge`
checks the engine's first capturable unit against the measured hsync width, and
run three times across three trips it **fails at `PLLAD_MD` 2280 and 2206 and
passes at 2208** -- at 2280 reporting 115 where the sync is 81 units. So the
solve that carries the bar and the solve that is protected from it by the write
limit are both misplacing the capture window, and the one that neither carries
the bar nor trips the test is 2208. The bar is one symptom of the divider
instability rather than a separate fault, and the assumed active extent is not
implicated on its own.

**It also fails at `PLLAD_MD` 2506, and there the reading is NEGATIVE** -- the
engine reports a first capturable unit of -21 against a measured sync of 89,
where the failures above are positive and too large. Measured on two builds
either side of an unrelated change, so the sign is a property of that divider
rather than of a build. A negative first capture is not the same fault as a
misplaced positive one and should not be folded into it.

A third witness reaches the whole picture rather than one edge. `PLLAD_MD` is
how many ADC samples are taken across one source line, so 2206, 2208 and 2280
sample the same line at three densities and the grid lands at a different phase
against the source's finest grating each time. The beat on that grating
therefore changes between solves, which is aliasing rather than an artefact at
the edge, and it is visible on every frame. Three instruments now agree the
divider is the fault and that 2208 is the solve that passes: the bar, the
capture-origin check, and the grating beat.

**What the latched state above actually is.** The candidates it has to be
upstream of the buffer write and invisible to the configuration: the
line-double FIFO, the decimators, or the capture-request machinery. Walking
`IF_HBIN_ST` up to 40 and back to 0 restored the bar unchanged on one instance,
three times running (sum 1259 / 94 / 1259 / 94 / 1259), and removed it for good
on two others, with the register value identical in both outcomes.

**Whether the loop's timing matters.** Hammering `/getreg` throughout the round
trip, which defers `loop()` and slows every register write the engine makes,
gave bars on 2 of 4 trips against 2 of 4 unloaded. No effect at that sample
size.

**What value the field should hold.** RD-5725-1.1 gives `IF_HBIN_ST` as the
horizontal blank for scale-down start position and states nothing about choosing
it. The value that clears the bar here is about 32, on one source at one
framing, which is a bench observation and not a derivation.


## What the field counts, and why one measurement sizes it

**One `IF_HBIN_ST` count is one ADC sample**, which on a doubled line is half an
IF unit. Measured at 320x256@50, `PLLAD_MD` 2200, by two ladders taken at one
camera position without moving the engine between them:

| ladder | what moved | photo columns per step |
|---|---|---|
| `IF_HBIN_ST` 0, 16, 32, 48 | the strip's onset | 0.792 per count |
| `IF_HB_SP2`/`IF_HB_ST2` together, 0 to -60 | the picture's right edge | 1.540 per IF unit |

The ratio is **0.514 IF units per count**. The pan ladder is linear over its
four steps and returns to its starting column, so the ratio is a property of the
register rather than of the framing.

**The tail is about 36 IF units**, 55 photo columns of strip at the same scale,
and it sits against the next line's pulse.

**There is one divider to size the constant against.** `DoubledLineSampleLimit`
caps a doubled line at 2200 samples, and every doubled row of
[../sampling-table.md](../sampling-table.md) lands there -- 240p60, 288p50,
1056x256 50, 480i and 576i alike. So a count, a fraction of the line and a time
are not distinguishable in the doubled branch, because they all name the same
number at the only divider a doubled line takes. The RISC PC cannot supply a
second one either: the monitor definition has two timings, 311 lines at
15.6 kHz and 448 at 31.5 kHz, and every 70 Hz mode maps onto the second, which
is progressive.

## It may only be raised on a doubled line

Against `IF_HBIN_SP` at `NoHeadBlanking`, `IF_HBIN_ST` blanks the **whole line**
rather than its tail. Measured at 320x256@70, `PLLAD_MD` 1216, `IF_HBIN_SP` 2,
with the picture photographed at each step:

| `IF_HBIN_ST` | picture |
|---|---|
| 0, 1, 2, 3, 4, 6, 8, 12, 16 | whole, right edge unmoved at photo column 1561 |
| 18, 20, 22, 24, 64, 128 | black, room reflections only |
| back to 0 | whole again |

So the progressive branch keeps 0 and gets no tail guard from this field. The
low-active tail that `../known-issues.md` describes needs another mechanism.

## Raising it costs no picture

With the capture stop panned to 1050 -- inside active video and within reach of
the blanking -- the picture's right edge is identical at `IF_HBIN_ST` 0, 64, 96,
128 and 160, at photo column 1424 or 1425 throughout. With the stop at 958 the
field has no effect at all, which says the blanking is anchored to the LINE
rather than to the window's tail.

**The granularity is four**: a value between two multiples of four reads
identical to the one below it. Above about 320 it starts to pan the picture.

## What the constant landed on

`DoubledTailBlanking` is **96**, against a tail measuring 70 counts. The margin
is for the instance-to-instance variation this page records above -- one
instance cleared at 32 and another needed more than 64 -- and it is free,
because the field blanks the source's own blanking.

Confirmed on the unit: six `MODE X800 Y600 C256 F60` / `MODE X320 Y256 C256 F50`
round trips, the reproduction that landed the bar on 4 trips of 6, all six
landing on `PLLAD_MD` 2200 with the strip's columns reading the black floor.
A `/sc?~` and a `PAD_SYNC_OUT_ENZ` return, the other reproduction, reads the
same.
