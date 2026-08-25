# Per-source framing presets

The user tunes the framing for a source, it is remembered against that source,
and changing the source mode and coming back restores it with nobody touching a
control. It survives a reboot, and it survives changing the output resolution.

**Built, except the VESA defaults.** What is here describes the code:
`Tv5725::SourceKey` is the key, `Tv5725::FramingTable` the table,
`Tv5725::FramingLine` the record's grammar, `Tv5725::FramingText` the file,
`Tv5725::SlotTable` and `Tv5725::SlotText` the numbered slots,
`Tv5725::Geometry` the behaviour, and `gbs-control.ino` the two files
themselves. Seeding the table with VESA defaults for a recognised timing is the
part still to come.

## What it unblocks

A slot used to be a register dump, and replaying registers into a calculating
engine is what `docs/firmware-geometry-engine.md` forbids, so the slot routes
refused for as long as they had nothing else to record. **This feature is that
recording**, and they no longer refuse — see "Slots" below.

## Two coordinate spaces

**`PanAndZoom` is the user's coordinate space and holds proportions.** An origin
and an extent per axis, as fractions of the capturable region, and nothing in it
refers to a mode. It is mode-independent by construction, which is what makes it
both the live state and the stored state — there is no separate portable form to
convert to.

**`ActiveImage` knows the current mode and translates.** It turns the proportions
into the concrete window in input units for the source as it currently measures,
and that window is what the geometry engine solves the registers from. It is a
computed output of the proportions and the mode, never a second source of truth.

The invariant lives in the proportional space, where it is a property of the
representation rather than a check:

```
0 <= origin        and        origin + extent <= 1.0
```

A window outside the capturable region is not expressible, in any mode, at any
instant. Every geometry fault of 2026-08-06 was a window derived from something
that had moved; this removes the class rather than guarding it.

**`usableExtent` is the denominator, and it is not the input line.** It is
bounded by `InputLine::WriteLimitUnits` as well, so a window satisfying the
invariant cannot run past the write limit and produce the tail green of
`docs/capture-limits.md`. `MinimumCapture` bounds `extent` from below, for the
reason it does today: a control that can crop to nothing is one keypress from a
dead picture.

## The mapping must be exact, and that is a requirement

`PanAndZoom.h` currently records that a proportional form was replaced because it
could not round-trip exactly or reach a single unit. **That is a statement about
the mapping that was used, not about proportions**, and this design requires a
mapping for which it is false. Two obligations, both testable at the host layer:

- **One control step moves exactly one input unit.** The step is `1 / usableExtent`
  for the mode in force, so it is computed per mode rather than being a constant.
  This is what "what a single pixel is" resolves to.
- **Out and back returns the same window, exactly.** Applying a step and its
  inverse must return the original proportion, not a value that denormalises to
  the same unit by luck. The safe way to get this is to keep the proportion on
  the current mode's grid — every value a control produces is an exact multiple
  of `1 / usableExtent` — so the translation to units is a multiplication that
  lands on an integer rather than a rounding.

A mode change re-grids the proportions onto the new `usableExtent`. That step is
inherently lossy and is *meant* to be: it is the proportional rescale that makes
a framing carry across modes at all. It must be idempotent — re-applying the same
mode must not drift the window a unit at a time.

## Storage

Because the live state is already mode-independent, storage is the same four
proportions per source with no conversion. `/framing.txt`, one source a line:

```
# framing, one source a line: <lines>@<fieldRateHz> = originH extentH originV extentV
# in ten-thousandths of the capturable region
311@50 = 364 8525 932 8167
```

Ten-thousandths because the ESP's printf has no `%f`, and they are enough
because one input unit is at least eight of them on any line this chip
captures — so the **window** survives the round trip exactly even though the
float does not. Swept across every capturable region from 200 to 1125:
units → proportion → text → proportion → units is identity.

The invariant is a property of any record by construction, so a stored framing
cannot describe an invalid window whatever source it is restored into.

**The table holds sixteen, and full means refused.** Dropping the oldest entry
loses work the user did with nothing said; a refusal is visible and an entry can
be cleared. Re-tuning a source already in the table replaces its entry, so a
full table still takes a re-tune.

**Only a framing the user touched takes a place.** Every solve seeds the framing
from the placement it computed, so a table that stored whatever the framing held
would fill its sixteen places with computed defaults and refuse the first real
tuning.

## Slots

The table above is what the engine remembers on its own: the last framing per
source, restored when that source comes back. A **slot** is the framing the user
chose to keep and named, so there may be several for one source and none at all.

`/slots.txt`, one record a line, the slot then the record `FramingLine` owns:

```
# slot framings: <slot> <lines>@<fieldRateHz> = originH extentH originV extentV
# in ten-thousandths of the capturable region
0 311@50 = 594 8324 611 9357
```

Keyed on **(slot, source)**, so one slot holds a framing per source — which is
what the register dumps did before it, `/preset_ntsc.A` and eight more per slot,
except that those were keyed by the `videoStandardInput` classification and this
is keyed by what the chip measured.

- **Bounded at sixteen records**, and refusing when full, for the reason the
  per-source table gives: it lives in the ESP8266's globals, and dropping the
  oldest loses work the user did with nothing said. The web UI offers 72 slots
  and 72 names; sixteen of them may hold a framing.
- **Written on an explicit save**, so there is nothing to debounce. It carries
  the same read guard as the preferences and the framing table: a boot that
  could not read the file refuses to save over it.
- **Restored through the engine.** `Geometry::applyFraming()` re-solves every
  register from the stored proportions, which is what lets a slot survive an
  output resolution change and is what a register dump could not do.
- **A slot carries no output resolution.** That is a preference of its own, and
  the proportions are taken against the capturable region, which the output
  raster does not touch.

The routes: `/uc?4` saves the current framing into the current slot and `/uc?3`
restores it, `/slot/save` stores one for the index the web UI's grid names, and
`/slot/remove` forgets what a slot held.

**`/slot/remove` shuffles the names in `/slots.bin` down by one and does not
shuffle the framings with them.** It shuffles exactly one entry and stops, which
is what it did with the register dumps too. A slot removed loses its framing; the
slots after it keep theirs under the names that have moved.

## The key

Keyed on the **source**, because framing is a statement about which part of the
source to show. The output mode is not part of the key.

- **Measured rates are noisy, so the key must quantise.** The field rate wobbles
  and the engine re-solves on it; an exact-match lookup on a float misses its own
  entry. Bucket the rates, wider than the observed jitter.
- **Prefer `STATUS_SYNC_PROC_VTOTAL` as the discriminator.** It is an integer, it
  is stable, and it is already how this project names a mode. (VTOTAL,
  field-rate bucket) identifies a source better than two measured rates do.

**Modes differing only in pixel clock share one entry, and that is correct
rather than a compromise.** The chip sees sync edges, not pixels, so 320x256 and
640x256 are indistinguishable to it. Measured on the bench with the RiscPC's
stock Acorn timings, they are indistinguishable in the picture too:

| | 320x256 | 640x256 |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 311 |
| `HPERIOD_IF` | 431 | 431 |
| `PLLAD_MD` | 2250 | 2250 |
| capture window | 118..1008 | 118..1008 |
| `VDS_HSCALE` / `VDS_VSCALE` | 491 / 513 | 491 / 513 |

Every measurement is identical, and photographed on the television the test card
occupies the same screen area in both — same horizontal extent, same borders,
differing only in how finely the card itself is drawn. **The framing is a
property of the signal, not of the pixel count**, so one entry serving both is
the right answer.

**This is structural, not luck.** A CRT of the era is in exactly the position
this scaler is in: it locks to sync and has no way to know the pixel rate
either. Where the picture sits on its screen is set by the interval from sync to
active video and by how long active video lasts — so two modes with different
blanking would move and resize the image on every mode change, leaving the user
at the monitor's own H/V position and size controls each time. Consistent
blanking across the modes of one monitor definition is therefore a requirement
the display hardware imposed on whoever wrote the timings, which is why the
stock ones satisfy it and why a source built for a fixed-frequency display can
generally be expected to.

The assumption it rests on is that consistency, so a hand-written monitor
definition that changed the borders for one of a pair would break it. The fix
belongs on the source side — keep the blanking consistent, as the era's displays
already required — rather than in a key this hardware cannot make more specific.

## The preferences file is replaced, not migrated

`/preferencesv2.txt` is positional, unversioned and one character per field — its
own writer carries a comment explaining that dropping a single byte shifts
volume, input selection and every value after it. A short read of it yields a
full set of defaults silently, which has produced at least three separate
misdiagnoses.

The replacement is **text, keyed and human-readable at the filesystem**, so that
reading it over `/fs/download` answers what is stored without decoding positions
against the writer. Requirements:

- **One `key = value` per line**, unknown keys ignored on read and preserved
  where practical, so adding a setting cannot shift another.
- **A missing key takes its default; a malformed line is skipped, not fatal.**
  A partial file must degrade to defaults *for the fields it lacks only* — never
  to a full set of defaults for the whole file, which is the present failure.
- **A short or unreadable file must still refuse to save over itself**, keeping
  the existing `prefsAreSuspect` guard: a silent bad read followed by an ordinary
  save is how the loss actually happened.
- **The framing table lives in its own file**, not this one. It is variable
  length and keyed, and mixing it with scalar settings recreates the fragility
  being removed.
- **No migration.** Existing files are abandoned and defaults are taken once.
  This is agreed, and it is worth stating in the commit so it does not read as an
  oversight.

## Behaviour

- **On a source mode change**, look the key up and apply the stored proportions
  before the windows are solved. `Geometry::poll()` owns the order — raster, clock,
  windows, rate steer — and the framing is an input to the window step.
- **With no entry**, use the computed default exactly as now. A default framing
  saved and restored unchanged produces identical registers.
- **Saving is debounced, not per keypress.** A pad press must not write flash.
  The in-memory table follows every press; only the file waits, for 15 seconds
  of the framing holding still.
- **A reset forgets the entry**, not just the framing. Without that the solve
  that follows finds the entry and restores exactly what was discarded, and the
  control does nothing.
- **Changing the OUTPUT resolution keeps the framing.** The proportions are
  taken against the capturable region, so an output change keeps the user's
  intent. This is why the key is the source alone: `Geometry` keeps the framing
  when the key has not moved and looks it up when it has.

  **Within a unit, not bit-exact.** An output too short to show a doubled frame
  takes the line doubler out, which halves what the IF counts, so an output
  change *can* move the denominator and the proportion is re-gridded onto the
  new capture. That re-grid is the same one a source mode change does, and is
  lossy by design.

## Interaction with the HSCALE aliasing

Certain input-capture to `VDS_HSCALE` ratios alias, and a few pixels of
horizontal adjustment clears it.

- Framing keyed on the source is **not** a guarantee of a good ratio, because
  `VDS_HSCALE` also depends on the output raster. A framing tuned clean in one
  output mode can restore into an aliasing ratio in another.
- Do not encode a ratio-avoidance rule here. Characterising which ratios alias is
  its own experiment; until it exists the picture is the only judge, and the user
  adjusting and storing the result is exactly what this feature provides.

## Testing

The window arithmetic and the serialisation are pure, with no bus access, so they
belong in the host unit layer (`make -C test`):

- the invariant holds after every pan and zoom operation, including at the bounds
- **one control step moves the translated window by exactly one input unit**, at
  several `usableExtent` values
- **a step and its inverse return the identical proportion**, not merely one that
  translates to the same unit
- re-applying the same mode is idempotent: the window does not drift a unit at a
  time across repeated re-grids
- a record restored into a smaller capturable region still satisfies the
  invariant
- a default framing round-trips to identical registers
- key lookup tolerates rate jitter within a bucket and separates adjacent buckets
- a preferences file missing a key, carrying an unknown key, or truncated
  mid-line yields defaults for the affected fields only

The end-to-end behaviour — change mode, come back, framing restored — is
reachable over HTTP through `/geometry` and the pad commands, so it earns a
hardware acceptance test as well. `test_slot_framing.py` is the slots' one,
behind `--source` and `--preset-save`: a tuned framing saved and restored after
a reset, the record holding the proportions the engine reports against the
source the chip is counting, and an empty slot leaving the picture alone.

## Related

- [firmware-geometry-engine.md](firmware-geometry-engine.md) — why registers are
  an output and the framing is held state
- [capture-limits.md](capture-limits.md) — the write limit `usableExtent` obeys
- [scaler-geometry-model.md](scaler-geometry-model.md) — capture window to
  output blanking
