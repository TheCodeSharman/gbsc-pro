# The on-screen menu

The menu the remote drives, what each screen reports, and the two places the
word "OSD" points at in this tree.

## Two OSD subsystems, one of them on the television

Searching for "OSD" finds two unrelated things. Only the first draws the menu
on the television.

| | what | where | live? |
|---|---|---|---|
| **STV9426** | character OSD chip on the ESP's I²C bus at `0x5D`. Draws the menu, the volume bar and the Info screen | `OSD_TV/OSD_stv9426.h`, `OSD_menu_F()`, `OSD_c1()`..`OSD_c3()`, driven by the state machine in `OSD_selectOption()` | **yes** |
| **OLED menu** | the 128x64 SSD1306 on the unit itself | `OLEDMenuManager`, `OLEDMenuImplementation.cpp` | yes, but it is a separate tree and holds no Move/Scale |

The TV5725 has an icon-and-bar OSD of its own at `s0_90`..`s0_98`. **Nothing
drives it** and the menu on the television comes from the STV9426, so the block
has no owner. It is not quite unwritten: `Tv5725::Chip`'s bring-up parks the
command handshake at rest — `OSD_COMMAND_FINISH` 1, `OSD_INT_NG_LAT` 0,
`OSD_TEST_SEL` 0 — because every writer of that handshake toggles it around a
command and nothing else establishes it. Three registers of about twenty.

The names are still in the register catalogue, so a search for "OSD" finds them.
The forty helper constants that used to sit beside them — zoom ratios, menu
styles, icon ids, colours, formats — are gone, along with the `osdIcon()` lookup,
because those were firmware rather than facts about the chip. `CODING_STYLE.md`
has why the declarations stay when the constants do not.

**Do not reinstate it.** Its initialisation read as live enough to be worth
analysing — colours, position and zoom all set up — from an entry point nothing
called, and analysing it explains nothing about what the remote does.

## The state machine

`OSD_selectOption()` is one long `else if` chain over `oled_menuItem`, each
branch drawing its screen and decoding IR itself. `0` is closed.

The root ring is `OSD_Input`, `OSD_Resolution`, `OSD_ScreenSettings`,
`OSD_ColorSettings`, `OSD_SystemSettings`, `OSD_ResetDefault`. `75` and `76` are
Move and Scale under Screen Settings; `1` is the volume overlay; `152` is Info.

Key roles, from `OSD_TV/remote.h`:

- **Menu** moves up one level, and opens the menu from closed.
- **OK** selects and descends.
- **Exit** leaves the OSD from any depth.
- **Up/Down** move within a level; **+/- Volume** are `kRecv2`/`kRecv3`.

**Ten branches have their entire IR switch commented out** — `72`, `73`, `97`,
`104`..`108`, `153`, `109`. No key reaches them, so anything that lands there
waits for the timeout. Do not add a handler to one without first establishing it
is reachable.

## What a row of the chain's menu looks like

Measured off the emitted frame with `osd_walk.py`, because nothing states it: the
handlers paint characters at numbered positions and the layout is only visible on
the screen.

| column | what is there |
|---|---|
| 0 | `0x15`, an arrow. Yellow on the selected row and the background colour on the others, so the cursor is a glyph AND the row colour |
| 1.. | the label. **On the root ring the number is part of the label string** -- `Osd_Display(1, "4 System Settings")` -- rather than a column of its own |
| after the label | `0x15` again where the item leads somewhere, at a column chosen per item (`P18` on one row, `P19` on the next) |
| the middle | for an adjustable value, a rule of `0x3e` hyphens |
| before the last | the value, right-aligned -- `128` on the colour rows |
| 27 | `icon5` (up) on row 1, the page character on row 2, `icon6` (down) on row 3, in `blue`, and only where that direction exists |

**The root ring is six items on two FIXED pages of three** -- `1 Input`,
`2 Output Resolution`, `3 Screen Settings`, then `4 System Settings`,
`5 Picture Settings`, `6 Reset Settings` -- which is what the page character at
column 27 counts. Every level is cut that way, by hand, a page letter per three
items.

## Removing a menu item is a layout judgement, not a deletion

Both menus navigate by explicit per-key targets written out at each branch, so a
state has no owner that knows its neighbours. **The Up and Down targets are not
symmetric**: `94`'s Up reaches Compatibility while the branch above it reaches
`94` on Up and `98` on Down, and `94`'s Down is `103` rather than the branch it
came from. Splicing a state out therefore means choosing what each inbound key
should reach, which the code does not say.

The television side is worse: a row is a run of `OSD_c2`/`OSD_c3` character
writes at fixed `P` positions, so removing an option's ON/OFF field leaves its
label painted and the page unreflowed.

**This is what blocks removing a user option** from the chain, rather than the
option's own plumbing. `PalForce60` and `matchPresetSource` are both dead -- the
first had its standard-byte swap deleted and the second never had a consumer at
all, only a preferences byte, a websocket status bit, an IR toggle and two menu
displays -- and both still occupy an OLED state and a TV OSD row for that reason.
Neither is in the described tree, where leaving an item out is leaving a line
out.

**The same mechanism loses items as readily as it keeps them, and five are
lost.** An item whose neighbours were never pointed at it is unreachable however
live its option is, and nothing says so. Walking Up, Down and Ok from the root
ring reaches every branch except these:

| branch | item | the only route left |
|---|---|---|
| 96 | Use upscaling (`preferScalingRgbhv`) | `/uc?x` |
| 110 | Restart | `/uc?a` |
| `OSD_Resolution_pass` | Pass Through | its Ok is commented out too |
| 91 | Y gain | its Left and Right are commented out too |
| 92 | Color | `/uc?V` and `/uc?R` |

Each has a live Up target and no inbound Down, which is what makes it invisible:
the branch draws, decodes and looks entirely healthy. A described level is a ring
by construction, so that class of loss cannot happen.

## The described form

`src/osd/` holds the machinery and the tree it describes:

| class | holds |
|---|---|
| `MenuItem` | one node: label, what Ok / Left / Right ask for, children, a value-text function |
| `MenuCommand` | one action: a letter and which command surface it belongs to |
| `MenuPad` | the four directions of a pad, each a nudge |
| `MenuContext` | what a value-text reads -- the preferences and the engine |
| `MenuTree` | the menu as data, one declaration per option |
| `MenuPage` | one screen: the items in view and which is selected |
| `MenuCursor` | where the remote is -- Up, Down, Ok, Menu as generic traversal, plus the three-row window |
| `MenuRenderer` | the three calls a device supplies: begin, row, end |
| `OSD` | the television's own renderer, and the only claimant on that name |
| `Menu` | a key in, a redraw and at most one command out |

**An action is a letter AND a surface, because the board has two.** `/uc?`
reaches `handleType2Command()` and `/sc?` the switch in `loop()`, and four of the
letters the menu sends mean different things in each -- `Z` toggles matched
presets on one and bumps `VDS_Y_OFST` on the other. So a letter alone does not
say what an Ok asked for, and Left and Right carry one each: `ADC gain` steps
through `/uc?` and toggles automatic gain through `/sc?`.

**Selecting a source is a third surface, because it is not a letter.** It is
`pendingInputSelection`, which `/input?src=` queues and `loop()` acts on, and it
reaches the HC32's analog switches as well as `ADC_INPUT_SEL` -- two muxes in
series, from one row. So an Input item names the source rather than a letter, and
a press that asks for one reports `"queue":"input"`.

**A pad is a fourth surface, because a tap is not a number of pixels.** The four
geometry letters `/sc?` carries -- `+ - * /` and `z h I O` -- are stated in output
pixels, and one capture granule is `granularity x magnification` of those, so a
tap stated as a pixel rounds to nothing above x2 and the press reports a limit
that is not there. A pad names one of `Tv5725::Nudge`'s four controls and the way
it goes; `Tv5725::Controls::nudge()` takes it in granules, and the number of them
is the remote's, from the hold ramp behind a held key. A press that asks for one
reports `"queue":"nudge"` and `"asked":"vpan+"`.

**Ok hands the arrows to the pad and Menu or Ok takes them back.** Exit closes
from inside one, and reopening leaves them with the level. While a pad has them
the cursor does not move, so Up and Down reach the picture rather than the level
-- which is why leaving is Menu as well as Ok.

**A tune is a fifth surface, because a letter per adjustable value runs out.**
`/uc?` had twelve single characters left and the rows wanting a pair of them
needed more than that, so a row names the value it steps -- `Osd::Tune::Red`,
`Brightness`, `Format` -- and the sketch routes it to whichever class holds that
value. A press reports `"queue":"tune"` and `"asked":"red+"`.

**It is not the pad's surface, and the difference is the gesture.** A pad takes
the arrows and asks for capture granules, which only the solve can size; a tune
row keeps the cursor and asks for counts of a value somebody holds. Both take
their number of steps from the remote's hold ramp.

**A page carries its items rather than their text**, because a row's current
value is only knowable from a context and neither the cursor nor the page has
one. The renderer resolves it at draw time and hands the device the page and
which of its rows to draw, the rest of a row's layout being what the page says:
the label, which row is selected, whether an item leads somewhere, and the
page's own number. The bracketing in `MenuRenderer` is the panel's: it buffers a
frame and flushes it, where the overlay writes characters as they arrive and
ends with nothing to do. A host test substitutes a recording renderer and reads
what the menu SAYS without either device.

**A level is cut into fixed pages of three, not scrolled a row at a time.** The
page character at column 27 counts pages, so a window following the cursor would
show triples the chain never draws. `MenuCursor` derives the page from the
index -- `index / Rows` -- and holds nothing for it; the level is still a ring,
so Up from the first item lands on the last, which is on the last page.

**The tree costs 1292 bytes of globals, and `constexpr` does not fix that.**
Const data on this part lands in RAM rather than in flash, so a `constexpr`
constructor buys constant initialisation and not placement; reaching flash would
mean `PROGMEM` and a `pgm_read` at every access. Free heap at boot went 12784 to
11128, which is above the 8000 the console's broadcast gate needs.

### What is described, and what is not

Described: the root ring, Input, Output Resolution, Screen Settings, System
Settings, Picture Settings and Reset Settings -- in that order, which is the
chain's. The labels are the chain's
too, except for the number each root item carries: the chain kept it inside the
label string (`Osd_Display(1, "4 System Settings")`) and here it is a column of
its own, which the overlay draws for the level with nothing above it. The panel
therefore gets the label without it.

Every row of the chain's tree is described, which is what `OSD_selectOption()`
can now be deleted against -- a subtree at a time, each judged on the remote with
`/menu?ir=1` first.

**Two things the chain's Input items do that these do not.** Each of its RGBs,
RGsB and VGA items writes `RGB_Com` -- the persisted compatibility preference the
Sv-Av level also shows -- so selecting an input there silently moves an option
the user did not touch. And its SV and AV items put the stored format option in
the frame's low nibble, where `InputSV()` and `InputAV()` send the bare `0x10`
and `0x20`; that is `/input?src=`'s behaviour as much as the menu's, and the
option lives on the Sv-Av level, which is where the described tree shows it.

`Pass Through` is deliberately absent rather than pending: it is not a
resolution, `Tv5725::OutputChoice` says so in as many words, and the option
behind it is the upscaling preference under System Settings. A second label for
one option is a divergence with no reason.

### Sharpness is the peaking band gains, and it is held rather than read back

`Tv5725::VideoProcessor::setSharpness()` writes `VDS_PK_LB_GAIN` and
`VDS_PK_LH_GAIN`, which are one control. Sharpened is **0x1F** on both bands:
the value is written as 0x5F in several places and each field is six bits wide,
so that is the gain the picture has always had. Unsharpened, the low band rests
at 0x16 and only the high band follows the output resolution -- 0x0A at 1080p,
0x18 elsewhere.

**`uopt->wantSharpness` is what the row reports**, in the preferences byte that
used to hold `wantFullHeight` and was read and discarded. The chain read
`VDS_PK_LB_GAIN` back instead, and `applyOutputResolutionSettings()` writes the
resting gain on every output change, so the setting was lost and the row then
reported the loss as the user's choice. `applyOutputResolutionSettings()` asks
the preference now, which is also why the two are one function's business.

**It leaves `VDS_PK_Y_H_BYPS` to `setPeaking()`.** The chain's `W` wrote that bit
as well, so pressing Sharpness moved the Peaking row to a value nobody set --
two owners on one bit. The consequence is that sharpening does nothing while
peaking is bypassed, which both rows now say.

### The colour balance is held in the basis the rows show

`Tv5725::ColourBalance` holds a red, green and blue balance and a luma gain, each
0..255 with **128 meaning the colour space's own rest**, and owns `VDS_Y_OFST`,
`VDS_U_OFST`, `VDS_V_OFST` and `VDS_Y_GAIN`. A balance is converted to the three
offsets on the way out, by the matrix the sketch used to mix in floating point,
in thousandths:

| | from R | from G | from B |
|---|---|---|---|
| Y | 299 | 587 | 114 |
| U | -169 | -331 | 500 |
| V | 500 | -419 | -81 |

**Where a neutral balance sits is the colour space's**, and `ColourBalance::Rest`
is the whole list: RGB at 0/0/0, component at 0x0E/0x03/0x04, a component OUTPUT
at -2/0x01/0x04 with a luma gain of 0x64 rather than 0x80. `ColourSpace` is handed
the balance and names the rest, so one class writes those four registers and a
load cannot put the balance back to neutral.

**The chain derived the balance by reading the offsets back**, which cost it two
things. The round trip is lossy -- 147/138/147 converts to 13/2/3 against the
14/3/4 it came from, so every colour space change walked the picture a count
further -- and the same read meant a component source rested at 147/138/147 and an
RGB one at 128/128/128, the same picture reported two ways. It reads 128 on both
now.

**Left and Right step, Ok keeps.** Each row's Left and Right name the value they
move, on the tune surface above; `Y` writes the preferences. Those are held keys,
so a save per step would write flash a hundred times for one adjustment; the
balance is four three-digit decimals appended to `/preferencesv2.txt`, in the
form the BCSH values there already use. A file written before that ends early and
reads as neutral.

The eight letters these rows briefly used -- `Z`/`T`, `N`/`M`, `Q`/`H` and
`P`/`S`, which before that stepped the raw offsets -- are free again.

**`Default colour` resets the balance** rather than writing the four registers,
and still writes the chroma gains and the ADC offsets, which are not the
balance's.

### Sv-Av InPutSet is the AV module's picture, and none of it reads back

Eight rows under System Settings, which is where the chain put them: the
decoder's standard, the ADV7391's line doubling and smoothing, its brightness,
contrast and saturation, a Default, and the compatibility preference the RGB
inputs share.

**The whole path is write-only.** The HC32's `USART4` TX goes to the J18 header
and the update button rather than back to the ESP, so nothing on the board can
read any of it, and `avOptions` is what each row reports. It is a struct of its
own rather than part of `userOptions` because Reset Settings wipes that one and
the AV module's calibration is not a scaler preference.

| row | what it asks for |
|---|---|
| Format | a tune, which rings through the twelve standards the frame's mode table carries. **One row for both decoder inputs**, following whichever is selected |
| DoubleLine | `/uc?b`, then `Send_Line()` |
| Smooth | `/uc?c`, then `Send_Smooth()`. **Gated on the doubler**: smoothing is a property of the doubled line, so the press does nothing while it is out, which is the chain's gate too |
| Bright, Contrast, Saturation | a tune each, written together by `applyAvPicture()`. Brightness reaches `SetReg(0x0a)` as a signed offset from the middle, where the other two are the value |
| Default | `/uc?k`, which sends the ADV7391's own reset and puts the three back to 128 |
| Compatibility | `/uc?d`, then `Send_Compatibility()` -- and `applyPresets()` on an RGB source, because the preference is shared with the RGB inputs |

`Send_Line()`, `Send_Smooth()`, `Send_Compatibility()` and `Send_TvMode()` each
save the preferences themselves, so those four rows need no Ok. The three
picture controls do, for the same reason the colour balance does.

**One press is all a surface queues**, so presses sent faster than `loop()`
consumes them coalesce: twenty `/menu?key=right` in a row landed four. A remote
press is one key event per loop and does not; a test of a row must press until
the row reads what it asked for, and must see a save land before asking for
anything else.

### `/menu` drives it, so a menu change needs no remote

Behind `GBS_DEBUG`. `key` is `up`, `down`, `left`, `right`, `ok`, `menu` or
`exit`, and the reply is the page the menu would draw.

```sh
curl 'http://<ip>/menu'              # the page, pressing nothing
curl 'http://<ip>/menu?key=down'
curl 'http://<ip>/menu?key=ok'
```

```json
{"open":true,"adjusting":false,"depth":2,
 "page":{"number":1,"previous":false,"next":true},
 "asked":"G","queue":"uc",
 "rows":[{"label":"Aspect","value":"Fill","selected":true},
         {"label":"Use upscaling","value":"ON","selected":false},
         {"label":"Deinterlace","value":"Adaptive","selected":false}]}
```

`page` is the one part of the drawn row the labels do not carry -- the number at
column 27 and whether there is a page either side of this one. `adjusting` is a
pad holding the arrows, where `key=up` asks for a granule of picture rather than
moving the cursor.

**The page returned WITH a press still shows the old value**, because the letter
is queued for `loop()` and has not run yet. Read again to see the effect.

A press queues its letter on the surface the item names, so this proves the tree
against the handlers that already serve `/uc?` and `/sc?` rather than against a
copy of them. Measured on the unit: four Oks on `Aspect` walked Fill, 4:3, 16:9
and 5:4 with `/geometry` agreeing at each step, and the emitted frame went from
1899x1078 filling to 1424x1078 with bars of 242 and 254.

**Two items act at once and without confirmation**: `Restart` resets the ESP and
`Reset Settings` wipes the preferences and reboots.

`ir=1` routes the remote's seven menu keys to the described menu instead of
`OSD_selectOption()`, which is how a subtree is judged on the remote before its
branches are deleted. Off by default and not persisted; `/menu` reports which is
driving as `remote`. Volume, Mute and Info are the chain's and are not reached
while it is on.

```sh
curl 'http://<ip>/menu?ir=1'         # the remote drives the described menu
curl 'http://<ip>/menu?ir=0'         # back to the chain
```

### `/ir` presses a key, and `osd_walk.py` photographs the result

`/ir?key=<name>` injects a frame at the receiver, so it reaches whichever menu is
live by exactly the path a real press takes -- the chain included, which answers
nothing over HTTP and decodes inside every branch. `menu up down left right ok
exit info save mute volup voldown`.

```sh
curl 'http://<ip>/ir?key=down'
python3 tools/gbsc-pro-hwtest/osd_walk.py --host <ip> --out /tmp/old  menu down ok
python3 tools/gbsc-pro-hwtest/osd_walk.py --host <ip> --out /tmp/new --described menu down ok
```

The same sequence run twice, once per menu, is what makes the two comparable.

**The chain is still what the remote drives by default**, and nothing has been
deleted from it. What remains is the panel's renderer, and switching the remote
over a subtree at a time with `/menu?ir=1`.

### `OSD`, and four things about the STV9426

`OSD` writes a page as three rows of 28 character cells. A row is
written WHOLE, so it reflows -- the chain painted each label at a fixed `P`
position, which is why removing an option's value left its label behind.

**A row is laid out as the chain's**, so the two can be compared on a photograph
of the same page. Measured on the unit against the chain's own rows: the cursor,
the mark, the rule, the value field and the page strip land on the same cells
and in the same colours.

| column | what is there |
|---|---|
| 0 | the cursor, on the selected row only. The chain draws it on every row and in the background colour on the others, which is the same picture |
| 1 | the row's position in its level, on the root ring only. The chain carried it inside the label string |
| 1.. or 3.. | the label, after the number where there is one |
| after the label | the same glyph again where the item leads somewhere, on the selected row only, flush against the label rather than at a column chosen per item |
| to the value | a rule of hyphens, where the row has a value |
| ..25 | the value, right-aligned. The chain's field is 23..25 -- three digits, or `OFF` -- and `ON` is the one value it puts a column further left |
| 26 | the gutter |
| 27 | up arrow on the first row, the page number on the second, down arrow on the third, each only where that page exists |

**A pad draws four arrows where a value would go**, which is the cluster the
chain drew at the column its own rule stopped at -- `0x03`, `0x08`, `0x18`,
`0x13`. They are the overlay's glyphs rather than the page's, so the page says
only that a pad has the arrows and each device draws its own.

Two differences from the chain are deliberate. **A row the page does not fill is
cleared rather than painted**, so a short level leaves no bar of background
across the picture -- which also means the page number is not drawn where the
second row is empty, and the up arrow already says there is a page before this
one. And **a row is drawn in one colour**, where the chain left a selected row's
rule and value in the unselected colour and only changed the label.

A level of one page leaves column 27 alone. The chain always drew a page
character because every level it drew had pages.

Nothing on the board reports any of this, so each was measured off the emitted
frame:

| | |
|---|---|
| the font | ASCII from 0x21 up, but **0x20 is an accented letter** and the **hyphen is at 0x3e**. `Osd_Display()` is what says so, and it skips a space rather than writing one |
| a cell | two writes, and the same value means different things at the two addresses: at the even one it is the colour, at the odd one a glyph |
| **0x11** | both -- a filled block in the bar's colour, which is how `background_up()` paints a row |
| **0xc0** | turns a cell off altogether, at either address. `OSD_Cut_0x01()` writes it to erase the overlay. As a glyph it does not leave the background showing -- it takes the whole cell out |

So a row is painted as a bar and then written over, and a row the page does not
fill is turned off rather than painted.

**A press does not draw.** The overlay is on the ESP's I²C bus and a `/menu`
press arrives from a network callback, so `press()` marks a redraw and `loop()`
performs it -- the same reason register access is deferred. It draws only while
the chain's menu is closed, or the two paint over each other.

**Closing draws an EMPTY page rather than nothing.** A device that keeps what it
was given has no other way to be told: measured before this, Exit left the bar
and all three rows over the picture for ever.

## Info reports two things that are not what they look like

**`Err` is an unhandled class, not a fault.** The resolution line classifies the
input into six standard-definition classes — 240p, 480i, 480p, 288p, 576i,
576p — and falls through to `Err` for anything else. Every VGA-class source
prints it, permanently and by construction.

**The frame rate is invalid in bypass**, because `getOutputFrameRate()` measures
on the VDS test bus. See
[rgbhv-bypass-trap.md](rgbhv-bypass-trap.md), "What bypass makes unreadable".

## The output resolution preference is live, and it overrides itself

`uopt->presetPreference` survives the deletion of the preset tables: it becomes
a `Tv5725::OutputChoice`, which the geometry engine resolves to the
`Tv5725::OutputMode` it solves the raster from. It chooses the output raster,
not a register table.

**`matchPresetSource` silently rewrites the choice**, and nothing on screen says
so. It swaps within two pairs, keyed on the field rate the engine measured:

| source | preference asked for | preference used |
|---|---|---|
| 50 Hz | 960p | **1024p** |
| 60 Hz | 1024p | **960p** (unless standard 8, or scaling RGBHV) |
| 50 Hz | 480p | **576p** |
| 60 Hz | 576p | **480p** |

So selecting 960p against a 50 Hz source appears to do nothing, because the
result is the 1024p that was already loaded. The asymmetry — the 50 Hz side
having no guards while the 60 Hz side excludes two cases — is upstream's, and
only the 960/1024 pair carries it.

## Reset settings wipes options and reboots

`OSD_ResetDefault` issues type-2 command `'1'`, which calls
`loadDefaultUserOptions()`, saves, and calls `ESP.reset()`. There is no
confirmation step.

It rewrites the eighteen fields that function sets — including
`presetPreference` to 1080p and `enableFrameTimeLock` to **0**. It does not
touch the input selection, the volume, or the AV module's own stored routing,
all of which survive.

Frame time lock being off is worth checking after any accidental press: a
FrameSync that never runs is one of this project's recurring misdiagnoses.
`/uc?5` toggles it, and byte 1 of `/preferencesv2.txt` is the value.

## Related

- [gbs-control-debug-interface.md](gbs-control-debug-interface.md) — the HTTP
  commands, including `/uc?`
- [audio-path.md](audio-path.md) — what the volume overlay is attenuating
