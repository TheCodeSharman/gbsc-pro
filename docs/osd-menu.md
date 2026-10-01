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

**A page carries its items rather than their text**, because a row's current
value is only knowable from a context and neither the cursor nor the page has
one. The renderer resolves it at draw time. The bracketing in `MenuRenderer` is
the panel's: it buffers a frame and flushes it, where the overlay writes
characters as they arrive and ends with nothing to do. A host test substitutes a
recording renderer and reads what the menu SAYS without either device.

**The tree costs 1292 bytes of globals, and `constexpr` does not fix that.**
Const data on this part lands in RAM rather than in flash, so a `constexpr`
constructor buys constant initialisation and not placement; reaching flash would
mean `PROGMEM` and a `pgm_read` at every access. Free heap at boot went 12784 to
11128, which is above the 8000 the console's broadcast gate needs.

### What is described, and what is not

Described: the root ring, Output Resolution, Picture Settings, System Settings
and Reset Settings.

**Not described, because their items act by calling a sketch function rather
than by asking for a letter** -- each waits on its action reaching one command
surface:

| subtree | what its items call |
|---|---|
| Input | `InputVGA_mode()` and its siblings |
| Sv-Av InPutSet | the HC32 frame, and `SetReg` on the ADV7391 |
| Move / Scale | `geometryControls` with the hold ramp |
| R / G / B, Y gain | `R_VAL` and friends, then `Color_Conversion()` |
| Sharpness | `VDS_PK_LB_GAIN` read back to decide what to draw, with no held field |

`Pass Through` is deliberately absent rather than pending: it is not a
resolution, `Tv5725::OutputChoice` says so in as many words, and the option
behind it is the upscaling preference under System Settings. A second label for
one option is a divergence with no reason.

### `/menu` drives it, so a menu change needs no remote

Behind `GBS_DEBUG`. `key` is `up`, `down`, `left`, `right`, `ok`, `menu` or
`exit`, and the reply is the page the menu would draw.

```sh
curl 'http://<ip>/menu'              # the page, pressing nothing
curl 'http://<ip>/menu?key=down'
curl 'http://<ip>/menu?key=ok'
```

```json
{"open":true,"depth":2,"asked":"G","queue":"uc",
 "rows":[{"label":"Aspect","value":"Fill","selected":true},
         {"label":"Use upscaling","value":"ON","selected":false},
         {"label":"Deinterlace","value":"Adaptive","selected":false}]}
```

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

**The chain is still what the remote drives by default**, and nothing has been
deleted from it. What remains is the panel's renderer, and switching the remote
over a subtree at a time with `/menu?ir=1`.

### `OSD`, and four things about the STV9426

`OSD` writes a page as three rows of 28 character cells. A row is
written WHOLE, so it reflows -- the chain painted each label at a fixed `P`
position, which is why removing an option's value left its label behind.

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
