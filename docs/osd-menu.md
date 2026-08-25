# The on-screen menu

The menu the remote drives, what each screen reports, and the three places the
word "OSD" points at in this tree.

## Three OSD subsystems, one of them live

Searching for "OSD" finds three unrelated things. Only the first draws the menu
on the television.

| | what | where | live? |
|---|---|---|---|
| **STV9426** | character OSD chip on the ESP's I²C bus at `0x5D`. Draws the menu, the volume bar and the Info screen | `OSD_TV/OSD_stv9426.h`, `OSD_menu_F()`, `OSD_c1()`..`OSD_c3()`, driven by the state machine in `OSD_selectOption()` | **yes** |
| **TV5725 internal OSD** | the chip's own icon-and-bar OSD, registers `s0_90`..`s0_98` | `OSDManager.{h,cpp}`, `osd.h`'s `MenuManager` | **no** — its only entry point is a commented-out `registerItem` in `initOLEDMenu()` |
| **OLED menu** | the 128x64 SSD1306 on the unit itself | `OLEDMenuManager`, `OLEDMenuImplementation.cpp` | yes, but it is a separate tree and holds no Move/Scale |

The TV5725 block is the trap: it has plausible-looking initialisation
(`Menu::init()` inside `doPostPresetLoadSteps()`, colours, position, zoom) and
none of it reaches the screen. Analysing it explains nothing about what the
remote does.

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

## Info reports two things that are not what they look like

**`Err` is an unhandled class, not a fault.** The resolution line classifies the
input into six standard-definition classes — 240p, 480i, 480p, 288p, 576i,
576p — and falls through to `Err` for anything else. Every VGA-class source
prints it, permanently and by construction.

**The frame rate is invalid in bypass**, because `getOutputFrameRate()` measures
on the VDS test bus. See
[rgbhv-bypass-trap.md](rgbhv-bypass-trap.md), "What bypass makes unreadable".

## The output resolution preference is live, and it overrides itself

`uopt->presetPreference` survives the deletion of the preset tables: it feeds
`chooseOutputMode()`, which returns the `Tv5725::OutputMode` the load solves for.
It chooses the output raster, not a register table.

**`matchPresetSource` silently rewrites the choice**, and nothing on screen says
so:

| source | preference asked for | preference used |
|---|---|---|
| 50 Hz | 960p | **1024p** |
| 60 Hz | 1024p | **960p** (unless standard 8, or scaling RGBHV) |

So selecting 960p against a 50 Hz source appears to do nothing, because the
result is the 1024p that was already loaded. The asymmetry — the 50 Hz side
having no guards while the 60 Hz side excludes two cases — is upstream's.

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
