# `IF_HBIN_ST` blanks the captured tail, and it is the field nothing owns

The coloured bar at the right-hand end of the picture on the bench RiscPC is
removed by writing `IF_HBIN_ST`, with the capture window held and the picture
unmoved. Nothing else on the input formatter reaches it. `IF_HBIN_ST` is also
the one field in that block with no per-solve owner: it is written once, in
`InputFormatter::init()`, which runs only when `BringUp` is armed.

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
default framing. A trip through RGBHV bypass (`/uc?x` either side of the mode
change) leaves it at about the same rate, so bypass is not required.

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

`IF_HBIN_SP` is owned per scan mode by `InputFormatter::applyScanMode()`.
`IF_HBIN_ST` was written in `InputFormatter::init()` and nowhere else, and
`init()` runs from `BringUp::init()`, which `doPostPresetLoadSteps()` calls only
when `BringUp::armed()` -- armed by `setOutModeHdBypass()` and
`bypassModeSwitch_RGBHV()` and by nothing else. A mode change did not write it,
so its value across a mode change was whatever the last arm left. That is the
same shape as the `IF_HBIN_SP` fault the scan-mode owner was introduced to
close, and it is the second half of the same window, so `applyScanMode()` now
owns both.

**That gives the field an owner; it does not remove the bar.** The bar is the
capture window overrunning the source's active video, and where the window goes
is `AxisHorizontal`'s assumed active extent of 0.864 against the 0.852 this
source wants. One source is not a mandate to move a global default, and
`SourceTiming` supplies exact placement for a source running a raster the
standards state -- a table of VESA DMT and CEA-861 entries, which this source is
not in.

## What is left open

**Why the divider lands on 2206 on some trips and 2280 on others**, from the
same source in the same mode.

**Why an excursion sometimes destroys the instance.** Walking `IF_HBIN_ST` up to
40 and back to 0 restored the bar unchanged on one instance, three times running
(sum 1259 / 94 / 1259 / 94 / 1259). On two other instances one excursion removed
it for good, and only a further mode round trip brought it back. The register
value is identical in both outcomes.

**Whether the loop's timing matters.** Hammering `/getreg` throughout the round
trip, which defers `loop()` and slows every register write the engine makes,
gave bars on 2 of 4 trips against 2 of 4 unloaded. No effect at that sample
size.

**What value the field should hold.** RD-5725-1.1 gives `IF_HBIN_ST` as the
horizontal blank for scale-down start position and states nothing about choosing
it. The value that clears the bar here is about 32, on one source at one
framing, which is a bench observation and not a derivation.
