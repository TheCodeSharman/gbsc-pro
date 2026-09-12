# A stored pass-through preference boots a 15 kHz source into HD bypass, and nothing gets it out

`presetPreference == OutputBypass` is a user preference for pass-through, and it
persists. On a source the display cannot show passed through, it is enough on
its own to leave the panel dark from the first boot.

## Measured

RiscPC on `vga` at 320x256@50, separate sync, `STATUS_SYNC_PROC_VTOTAL` 311, on
a build that acquires that source cleanly with any other preference.

| `/preferencesv2.txt` byte 0 | after boot |
|---|---|
| `:` (10, `OutputBypass`) | `DAC_RGBS_BYPS2DAC` 1, `PLLAD_MD` 1856, `/geometry` `state: absent`, no picture |
| `5` (5, `Output1080P`) | `DAC_RGBS_BYPS2DAC` 0, `PLLAD_MD` 2206, `state: acquired`, PM5544 sharp |

Reproduced across a flash and two restarts, and the only thing that differs is
the stored byte.

## Why

The sync watcher's new-mode block reads the preference directly:

```cpp
boolean wantPassThroughMode = uopt->presetPreference == 10;
if (!wantPassThroughMode)
    applyPresets(detectedVideoMode);
else
    setOutModeHdBypass(false);
```

**`bypassCanBeDisplayed()` is not asked on that path.** Every other entry to
bypass asks it -- the serial commands, `applyPresets()`'s request arm -- and
refuses a line rate the sink cannot take. This one does not, so a 15 kHz source
is handed to the encoder, which shows nothing.

**And there is no exit.** `steerableRgbhv()` stands the sync watcher's steering
down on the HD bypass channel, which is correct for a source deliberately held
there, so the block that would put an RGBHV source back on the scaling path
never runs. `/sc?~` clears it sometimes and not reliably: measured, it recovered
the unit once and left it in HD bypass on the next attempt.

## What repairs it

Choosing any scaled resolution -- `/uc?s` for 1080p -- rewrites the preference
and the next boot is clean. Note that `/sc?K` writes `OutputBypass` back and
saves it, so a pass-through experiment leaves the unit in this state.

The steering block's leave-bypass arm also rewrites the preference, through
`OutputChoice::scaledOr()`, which is why a unit that reaches the scaling path
once stops booting into bypass. That overwrite is itself a fault -- it destroys
a preference the user set -- and the two are the same missing piece: a
pass-through request that is re-checked against what the display can show,
rather than a global boolean that one path obeys blindly and another discards.
`../video-source-acquisition.md`, *What decides bypass, once the block has moved*.
