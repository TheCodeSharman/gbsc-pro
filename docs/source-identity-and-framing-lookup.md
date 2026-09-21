# Source identity and the framing lookup

How the engine decides where to put the capture window on a source it has just
measured. Four steps, in order, each standing in for the one before it when
that one has nothing to say.

| step | source of the answer | keyed on |
|---|---|---|
| 1 | the framing the user tuned | `SourceKey` |
| 2 | CEA-861 | the measurement |
| 3 | VESA DMT | the measurement |
| 4 | a default guess | the axis alone |

## What the code does

```
VideoPath::sourceMeasured()
    timing_ = SourceTiming::matching(lines, fieldRateHz, hsyncDuty)

VideoPath::adoptSourceKey()
    key = SourceKey(lines, fieldRateHz)
    if (!framings_.find(key, &framing_))
        framing_.reset()                      // leaves the axis untuned

ActiveImage::place(), on an untuned axis
    from = timing.published() ? timing.activeStart(axis)
                              : axis.activeStart()
    start = line.videoAt(from)
```

So the chain exists and runs in the intended order. Steps 2 and 3 are one
table, and step 4 is `Axis::activeStart()` -- the PAL or NTSC capture window
selected on field rate, which `vesa-gtf.md` settles.

## What identifies a source

`SourceKey` is the line count and the field rate, quantised. It is what the
framing table is keyed on and what is persisted against a tuned framing.

**The line rate is not a third fact.** `lineRate = frameRate x VTOTAL`, so any
two of the three determine the remaining one and a key built from any pair
identifies the same set of sources. A frame rate ALONE does not fix the count,
which is why the pair is what the key carries.

**And no pair of them is fine enough for the standards lookup**, because all
three describe how often a line starts and none says how a line is DIVIDED. Two
rows of the published table agree on every one of them:

| | VTOTAL | frame rate | line rate | pixels | sync | pixel clock |
|---|---|---|---|---|---|---|
| DMT 640x480@60 | 525 | 59.94 | 31468.75 Hz | 800 | 96 | 25.175 MHz |
| CEA 720x480p | 525 | 59.94 | 31468.5 Hz | 858 | 62 | 27.000 MHz |

Same count, same field rate, and the same line rate to 0.001%. What separates
them is the pixel clock, and this chip cannot see it: it locks to sync edges, so
the horizontal axis has no native resolution and 320x256 and 640x256 are one
source here.

What it CAN measure is the sync width as a fraction of the line --
`STATUS_SYNC_PROC_HLOW_LEN` against the divider -- which is 0.120 against 0.072
for that pair. That is why `SourceTiming::matching()` takes a third argument,
and it is the only horizontal STRUCTURE available to a lookup.

## The sync width is repeatable enough to key on

The question a persisted key asks is not whether the reading is ACCURATE but
whether it REPEATS: a duty that moves between acquisitions changes the key and
loses the framing the user tuned.

Measured on the bench. Within one acquisition, 2499 samples over 45 s at
800x600@60 give two adjacent readings and nothing else -- 196 and 197 counts of
1606 -- for a mean duty of 0.12218, a standard deviation of 0.00026 and a total
spread of 0.00062. Across six re-acquisitions driven by source mode changes:

| mode | readings | duty |
|---|---|---|
| 800x600@60 | 196/1606, 196/1606, 196/1606 | 0.12204 every time |
| 640x480@60 | 187/1614, 186/1614, 187/1614 | 0.11586, 0.11524, 0.11586 |

So the reading repeats to the same one-count dither it shows while standing
still, about 0.0006 either way, and the separation it has to achieve on the
colliding pair above is 0.048 -- a margin of roughly 77 times.

**Sync polarity comes with it and is free.** `STATUS_SYNC_PROC_HSPOL` is
measured beside the width and was stable across the same six acquisitions,
negative on the bench 640x480 and positive on its 800x600. It is a second
horizontal discriminator already in hand.

The decision that follows is that the sync width belongs in the source's
identity rather than beside it. `SourceKey` is persisted in the framing file, so
adding a field changes the stored format and existing entries need migrating or
discarding; that cost is what the three options below were weighing, and the
measurement removes the doubt about whether the term is worth paying it for.

## What the chip can tell a lookup

Four kinds of fact, and the part supplies three of them:

| | rate | structure |
|---|---|---|
| horizontal | line rate, the field rate times the count | sync WIDTH and POLARITY |
| vertical | field rate | POLARITY only |

Both rates describe how often a line and a frame start -- the raster's outline.
Structure is the division of the interval, and it is a different kind of fact:
the outline says nothing about where sync ends and video begins.

**There is no vertical sync WIDTH anywhere in the register map.**
`STATUS_SYNC_PROC_HLOW_LEN` has no counterpart; the part offers `VPERIOD_IF`,
which is a period, and `STATUS_SYNC_PROC_VSPOL`, which is a polarity. So the one
cell that would verify a row's vertical blanking split cannot be filled.

## Sync polarity is measured, stable, and unused

`STATUS_SYNC_PROC_HSPOL` and `STATUS_SYNC_PROC_VSPOL` are read on every pass and
nothing keys on either. Measured across a source mode round trip, 40 samples a
landing:

| mode | mode file | HSPOL | VSPOL |
|---|---|---|---|
| 800x600@60 | `sync_pol:0` | 1 | 1 |
| 640x480@60 | `sync_pol:3` | 0 | 0 |
| 800x600@60 again | | 1 | 1 |

No dither at all, which is tighter than the sync width's one count, and the pair
repeats across re-acquisition. It agrees with what the standards publish --
DMT gives 640x480@60 as H-/V- and 800x600@60 as H+/V+ -- and the polarity PAIR
is a long-standing way of telling DMT modes apart.

**So a vertical term IS available to the lookup**, and it is the only one. It
cannot check where a row puts active video, which is a width; it can check that
the row is the right row on a vertical axis rather than on horizontal evidence
alone.

A matched row is checked today on its total lines, its field rate and its
horizontal sync width -- polarity is read and discarded -- and the
`activeStartLine` and `activeLines` it then supplies are an assumption that
nothing tests, because testing them needs a width the part does not measure. A source may match on every
checked quantity and divide its frame differently:

| mode | source `v_timings` | source active starts | published row |
|---|---|---|---|
| 800x600@60 | `4,23,0,600,0,1` | 27 | 27 |
| 640x480@60 | `2,32,0,480,0,11` | 34 | **35** |

`RetroScaler-Acorn.mdf`. The 640x480 mode matches VESA on total, rate and sync
width and starts its active image a line earlier than the standard does.

**That much is a property of the part, not of the lookup.** No rearrangement of
the tables can check a vertical BLANKING SPLIT, so any vertical placement taken
from a standard is a guess that happens to be well-informed. Choosing the right
row is a separate question, and polarity is evidence for it that is currently
thrown away.

## Where the implementation departs from the design

**Steps 2 and 3 are one table and the precedence runs the wrong way.**
`SourceTiming::Published[]` holds the DMT rows first and the two CEA rows
(720x480p, 720x576p) last, and `lookUp()` returns the first row that matches. So
where both standards could match, VESA wins, and the design calls for CEA.
Nothing records which standard a row came from, so the precedence is a property
of the array order rather than a stated rule.

The 525-line 60 Hz pair above is the case that would collide, and today it does
not, because the sync width separates them well inside `SyncDutyTolerance`.

**The two lookups are keyed differently.** The framing table is keyed on
`SourceKey(lines, rate)`; the standards lookup adds the measured sync duty. One
source therefore has two spellings of its identity, and only the narrower one is
persisted.

The sync width is going into the identity, the reading being repeatable enough
to key on. Three ways to arrange that:

- carry the sync duty in `SourceKey` itself, so both lookups share one key. It
  is persisted in the framing file, so the stored format changes and existing
  entries need migrating or discarding.
- leave `SourceKey` alone and keep the duty as an argument to the standards
  lookup only. No file change; the two keys stay different.
- add a second, non-persisted identity carrying all three, used for the
  standards lookup. No file change; two types to keep in step.

## What this does not explain

At 800x600@60 the published row states the source's own vertical layout exactly
and the top of the picture is still clipped. So the clip has a cause other than
the table, and restating the lookup will not move it.
`investigations/the-vertical-capture-window-is-placed-late.md` carries what is
measured and why its magnitude is not yet established.
