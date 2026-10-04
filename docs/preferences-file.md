# The settings file

`/preferences.txt` holds everything the user chose that is not a framing. It is
text, one `key = value` a line, and it is read and written by `Prefs::Settings`.

```
# GBSC-Pro settings, one key = value a line. A missing key takes
# its default and an unknown one is ignored, so adding or removing
# a setting cannot shift another. The last line is what says the
# file was written whole. docs/preferences-file.md
output = 1920x1080
slot = A
frame-time-lock = 0
...
input = vga
...
luma-gain = 128
end
```

**Read it with `fs_read(host, "/preferences.txt")`, or `read_settings(host)`
for a dict.** It needs no decoding against the writer, which is the point.

## The rules, and what each one costs if broken

- **A missing key takes its default.** A partial file degrades for the lines it
  lacks only. The positional file degraded for the whole of itself: a short read
  yielded a complete set of defaults, silently, and the next save wrote them to
  flash for good. That produced at least three separate misdiagnoses — a custom
  preset "not loading", the frame time lock "broken", and an input not applying.
- **An unknown key is skipped**, so a file written by a build with a different
  field set still loads. Adding or removing a setting cannot shift the meaning
  of any other, which is the whole reason the format is keyed.
- **A malformed line is skipped, not fatal.** The file is hand-editable, and one
  bad line must not cost the rest.
- **A value past its bound takes the default.** The bound is stated once, beside
  the name.
- **`end` is the last line a save writes, and a read that does not reach it is a
  read that failed.** That is the integrity check, and it works at any length —
  a byte count only worked while every build wrote the same number of them, and
  `PREFS_BYTES` had to be edited in step with the writer or a boot rejected a
  perfectly good file.
- **A boot that could not read the file refuses to save over it.** Otherwise the
  first setting the user touches persists a full set of defaults over settings
  still perfectly good on flash. The refusal covers "restore defaults" as well,
  so a save that writes something the next boot rejects is unrecoverable from
  the UI — `test_a_settings_file_this_firmware_wrote_loads_on_the_next_boot` is
  the guard.
- **The read is retried, and each attempt starts from the defaults.** A flash
  awake enough to serve metadata and not yet awake enough to serve content
  satisfies the open and the size on the first attempt and hands the parser
  nothing. Ten attempts at 100 ms; a part-applied read is never what the next
  one builds on.
- **The framing table is a separate file.** `/framing.txt` is variable length
  and keyed on the source; mixing it in recreates the fragility this format
  removes. `docs/framing-presets.md`.

## No migration

`/preferencesv2.txt` is abandoned rather than converted, and every unit takes
its defaults once. It is deleted on the first save, so a filesystem listing does
not show two settings files and leave the live one in doubt.

## Adding a setting

One line in `Prefs::Settings::eachScalerSetting()` or `eachBoardSetting()`:

```cpp
visit.number("scanline-strength", options_.scanlineStrength, 0x60, 0x30);
```

`each()` is offered to a visitor, and which pass it is decides what happens —
the default taken, a line's value applied, or a line rendered. **That is why the
list is one list.** The file this replaces stated its field set three times, in
`loadDefaultUserOptions()`, the load path and the save path, and the defaults
drifted: `avOptions`' initialiser gave `lineDouble` 0 while the load path's
clamp gave 1, so a unit with no file and a unit with a file missing that field
came up differently.

Three kinds, because there are three things a setting can be:

| kind | written as | for |
|---|---|---|
| `number` | decimal | every bounded byte, on/off included |
| `character` | itself | the slot, which is a character in `slotIndexMap` |
| `text` | itself | the output mode, and the input |

**The output mode and the input are stored as the names they report**, not as
ids. `OutputMode::fromName()` and `VideoSourceSelection::fromName()` resolve
them, and a name nothing answers to reads as nothing chosen — which for the
input is what makes detection sweep, and for the output is repaired to 1080p by
the caller, because which modes exist is `OutputMode`'s to say and a text file
cannot be trusted to name one.

## What is NOT in it

- The per-source framings, and the named slots. `docs/framing-presets.md`.
- Anything in `runTimeOptions`. `freezeAutomation` is deliberately not persisted:
  a reboot returns to normal.
- The HC32F460's `asw_01..04`. That microcontroller keeps its own copy in its own
  flash and neither side can read the other back, which is why the ESP transmits
  the input frame at boot rather than comparing. CLAUDE.md, "The system has three
  control domains".

## One fact, three keys

`input`, `legacy-input` and `brightness-set` are one fact. `legacy-input` is
`SeleInputSource`, which carries three values against the six `input` carries, so
it cannot tell RGBs from RGsB; `brightness-set` is `BriorCon`, which nothing
reads. Both are derivable from `input` through
`VideoSourceSelection::settingsFor()`. They are carried because detection still
reads the first and the OLED still writes the second, not because the file needs
them. `docs/known-issues.md`.
