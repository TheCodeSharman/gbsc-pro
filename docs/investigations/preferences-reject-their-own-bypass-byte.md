# The preferences file rejected a byte it wrote itself

`saveUserPrefs()` writes byte 0 as `presetPreference + '0'`. `OutputBypass` is
**10**, so selecting bypass puts `':'` (0x3a) there — one past `'9'`.

`prefsLookPlausible()` bounded byte 0 to `'0'`..`'9'`, so from the next boot
onwards the loader declared a perfectly intact file unreadable.

## What that costs

The unreadable path is deliberate and correct in itself: defaults would be
wrong, and writing them destroys the settings still on flash, so the boot sets
`prefsAreSuspect` and runs on whatever `loadDefaultUserOptions()` left in RAM.
Three consequences follow, and none of them names the cause:

- **the saved input is never sent.** `applySavedInputSource()` sees
  `SeleInputSource == 0`, takes its `default:` branch and transmits nothing, so
  the HC32's analog switches stay wherever they were. The unit comes up on
  another input, measuring whatever is on it — on this bench a steady 15 kHz
  that ignores every mode change the RISC PC makes, which reads exactly like a
  scaler that has stopped following its source.
- **every save is refused, silently.** `saveUserPrefs()` returns early, and it
  prints only to the serial cable, not to the web console. Preferences appear to
  change — the running firmware honours them — and none of it survives a boot.
- **nothing in the UI repairs it**, because "restore defaults" saves too. The
  only way back is deleting the file: `/fs/rm?path=/preferencesv2.txt`, then a
  restart, which takes the "no file yet, creating" branch.

## The signature

`/bootlog` names it outright, and needs `BOOTLOG_BYTES=2048`:

```
PREFS: attempt 1 t=7158ms open=1 size=39 got=39 plausible=0 first=[3a 31 41 30]
...
PREFS: UNREADABLE after 10 attempts; NOT overwriting them
PREFS: loaded presetPreference=5 ... SeleInputSource=0 suspect=1
INPUT: nothing stored, Info=0
```

**`got` equals `size` on every attempt**, which is what separates this from the
short read the retry loop exists for. The file is whole; the gate rejects it.
`first=[3a ...]` is the byte.

Without the boot log, the reading from over the network is: byte 0 of
`/preferencesv2.txt` is `':'`, and a `/uc?` preference command answers 200 and
changes nothing in the file.

## The bound

Byte 0 is a value, not a digit, so the bound is the largest value the enum
holds: `'0' + OutputBypass`. A gate on the encoding rather than on the range is
what let the save path and the load path disagree about the same byte.
