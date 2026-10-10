# The shape a picture is shown in

The engine scales every capture to fill the raster it is given. A 4:3 source on
a 16:9 output therefore comes out stretched, and the only 4:3 available is the
television's own mode — which **crops**, because it is fitting a 16:9 signal.

So the board states the shape itself: the captured picture is scaled into a
centred sub-rectangle of the raster whose ratio is declared, and the display
aperture blanks the rest.

## It cannot be measured, so it is declared

The chip sees sync edges, not pixels. The source's pixel clock is unknowable and
how many samples a line is cut into is `PLLAD_MD`, which is our own choice — so
320x256 and 640x256 arrive as one key and neither states a shape.

It is therefore a convention, and it is handled as one: defaulted from the
published raster the source matched, stored per source, and overridable.

**A reset puts it back with the framing.** Both are stored against the source
key in the framing file and both are what "back to default" means, so
`VideoPath::reset()` forgets the stored entry, clears the pan and zoom and takes
the shape from `SourceTiming` again. A shape left behind would keep its bars
over a reset picture. Screen Settings' `Reset` row is the menu's end of it.

`Tv5725::Aspect` holds width over height in ten-thousandths — 4:3 is 13333,
16:9 is 17778, 5:4 is 12500 — because that is what the framing file already
carries, so a stored shape is one more integer in a grammar with no floats.
**Zero means fill**, which is the absence of a shape rather than a shape.

## The sink is 16:9, so the raster's own counts are not what a shape narrows against

EDID reaches no MCU on this board, so the panel cannot be asked what shape it
is, and a set stretches whatever raster it is given onto that panel. A
1280x1024 output is therefore not 5:4 on the screen — it is the panel's shape,
and a 4:3 picture narrowed against 5:4 came out wider than 4:3 once the panel
had stretched it.

`OutputMode::PanelShape` is the assumption, stated once and 16:9, and every
mode's `displayAspect()` returns it. Pass-through keeps `Fill`, having no raster
and so no shape to narrow into. **This is an assumption and not a measurement**:
a 4:3 or 5:4 sink would want a different constant, and there is no way to find
out from here which it is.

A consequence worth knowing: `Fill` and `SixteenNine` are now the same picture,
so filling is no longer a step the shape cycle offers and a filled shape reports
as 16:9 on the menu.

## Where the default comes from

Every row of `SourceTiming`'s three tables states its own shape, in a twelfth
column. They are all 4:3 but one: `1280x1024@60` is 5:4.

It is stated per row rather than derived, because it cannot be derived from the
counts already there — `720x480p` is 3:2 measured in pixels and 4:3 on the
screen, and `720x576p` is 5:4 in pixels and 4:3 on the screen. The shape is the
DISPLAY's.

A source matching no row is shown as **4:3**. An unrecognised source is far more
often a computer mode than a widescreen one, and filling is what the user can
always ask for.

## The room narrows; nothing else moves

`Aspect::roomFraction()` is the whole of the arithmetic:

```
roomFraction = 1                 on the axis that still fills
             = wanted / shown    horizontally, where wanted < shown
             = shown / wanted    vertically, where wanted > shown
```

`shown` is the OUTPUT MODE's display aspect, `OutputMode::displayAspect()`,
stated per mode for the same reason the source rows state theirs.

That fraction multiplies the room in `OutputWindow::fitToRaster()`, and
everything else follows because everything else derives from the room: the fit
produces a narrower picture, `placePicture()` centres it, and
`OutputWindow::solve()` closes the display aperture on it. **The bars are the
display blanking doing its ordinary job** — no register is written for them.

The raster's own units are not square and need not be. The encoder resamples the
whole line into the standard's active pixel count, so a picture taking three
quarters of the room takes three quarters of the emitted width.

### It is an output transform, and the framing does not move

This is the requirement the design is shaped by. A framing that fills the screen
with a border flush on all four edges must, at 4:3, fill the 4:3 region with
that border still flush to it.

So the picture is **scaled down into the narrowed room, never cropped to it**.
Insetting the aperture without rescaling would leave the picture full width and
blank its outer columns, which discards a flush border rather than moving it.

Measured on the bench, RiscPC 320x256@50 into 1080p, cycling the shape with the
framing untouched at `2153 6249 1154 8205`:

Measured into 1080p, whose raster is the panel's own shape:

| shape | emitted picture | bars | ratio |
|---|---|---|---|
| fill | 1899 x 1078 | 7 / 14 | 1.762 |
| 16:9 | 1899 x 1078 | 7 / 14 | 1.762 |
| 4:3 | 1423 x 1078 | 243 / 254 | 1.320 |
| 5:4 | 1335 x 1078 | 287 / 298 | 1.238 |

Against what filling achieves, 4:3 lands on 0.7493 of the width where 0.7500 was
asked, and 5:4 on 0.7030 where 0.7031 was asked. The residual against the
STANDARD ratios — every row is about 1% narrow, fill included — is the existing
full-screen shortfall and not the shape's.

## Two consequences that are not the shape itself

**`placePicture()` centres in the ROOM, not on the raster total.** The two agree
wherever the picture fills, which is why nothing had seen the difference; with
bars they differ by half the back porch, about 41 emitted columns at 1080p.

**`narrowestCapture()` takes the shape and `widestCapture()` does not.** The
first is the zoom-in stop, and a narrowed room is reached by a smaller capture,
so the stop follows it down and a shaped picture still reaches its bars. The
second bounds `VideoPath::narrowToRaster()`, which CROPS the capture by it — and
the shape must never cost picture.

## `carriedPx` is kept out of it

It is a compensation, not a deficit. The engine paints into the fraction of its
own line that the chain carries, and after that compensation the picture lands
on the panel's **full** emitted width. Folding it into the shape would letterbox
every SD output by about four percent, invisibly.

## Where the shape cannot be reached

`VDS_?SCALE` divides 1024 and tops out at 1023, so the part cannot minify: the
produced picture is always at least as wide as the capture. Where the narrowed
room is **smaller than the capture**, the shape is unreachable without cropping,
and the axis fills instead.

Measured on the bench, 800x600@60 into 1080p: the 60 Hz raster is 1592 units, so
its room is 1389, and 4:3 asks for 1042 against a capture of 1090.

The refusal is reported, because no register distinguishes it from having been
given no shape at all — the console says `aspect: 13333 refused, the part cannot
minify` and `/geometry` carries `"shaped":false`.

**Raising the display clock does not help, and that is measured.** At 129.6 MHz
the raster widens to 1910, but `VideoPath::dividerCeilingForOutput()` raises
`PLLAD_MD` with it — 1438 to 1744 — so the capture grows in the same proportion
and the ratio of capture to room is unchanged. The picture filled at both.

What would lift it is the input formatter's own scaling-down block, which sits
ahead of the VDS and can minify.
[the-input-formatter-can-scale-down.md](investigations/the-input-formatter-can-scale-down.md).

## Storage

A fifth number on the record `FramingLine` owns, so both `/framing.txt` and
`/slots.txt` carry it:

```
311@50.08/686++ = 2153 6249 1154 8205 13333
```

**It is optional on read.** Every file already on a unit was written without it,
and a record rejected for lacking one is a whole tuning discarded. Absent and
malformed are different: a record that ends after the fourth number is an older
one, and a record whose fifth field is rubbish is skipped like any other
malformed line.

**An absent field reads as `Fill`, which is not what a fresh source gets.**
`FramingLine::read()` starts the shape at `Aspect::Fill` and leaves it there when
the record ends after the fourth number, so a source carrying a framing written
before shapes existed comes up unshaped, where the same source with no stored
framing is defaulted from the raster it matched. Whether that is right is open:
`known-issues.md`.

## The board's half is a switch

A display has a shape control of its own, and a set put into its 4:3 mode
shapes what it is sent. The board's shaping and the set's then COMPOUND, and
the picture comes out narrower than either asked for -- so whether the board
shapes at all is a setting, System Settings' `Aspect ratio`, valued `Apply` and
`Ignore`.

Ignored, every solve is handed `Aspect::Fill` and each source fills the raster
whatever shape it carries. **The shape itself is untouched**: it stays stored
against the source key, `aspect()` still reports it, and turning the switch
back on puts the bars back where they were. `VideoPath::appliedAspect()` is the
whole of the mechanism -- the stored shape while applying, `Fill` otherwise --
and every solve site asks it rather than the stored value.

The Screen Settings `Aspect` row greys while it is ignored, because that row
asks WHICH shape and has nothing to say when none is being applied.

`/geometry` carries `shaping` for it. `shaped` cannot answer the question: an
axis told to fill is indistinguishable from one that honoured what it was
given, so both read true.

## Control

`/uc?G` cycles 4:3, 16:9, 5:4 and round — `Aspect::next()`, which the menu row
and the letter share. Filling is not in it: against a 16:9 panel it is the same
picture as 16:9, so it was a step that changed nothing. A preset cycle rather than a
number, because the remote has one button for it; the shape is stored against
the source, so the cycle starts from whatever that source was left at.

`/uc?S` toggles whether any of it is applied, and is the `Aspect ratio` row.
The engine owns the switch and `apply-aspect` in the preferences file is how it
survives a power cycle, defaulting to applying.

`/geometry` reports `aspect` in the same ten-thousandths -- the STORED shape,
applied or not -- `shaping` for whether the board is applying it, and `shaped`
for whether the last solve could honour it.
