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

**The line rate is not a fourth fact.** `lineRate = lines x fieldRate`, so
naming the line rate and the frame rate states the same thing as the count and
the rate: one of the three is always derivable from the other two, and a key
built from any two of them identifies exactly the same set of sources.

**And that set is not fine enough for the standards lookup**, because the count
and the rate describe how often a line starts and say nothing about how a line
is DIVIDED. Two rows of the published table demonstrate it:

| | lines | pixels | sync | pixel clock | line rate | field rate |
|---|---|---|---|---|---|---|
| DMT 640x480@60 | 525 | 800 | 96 | 25.175 MHz | 31468.75 Hz | 59.94 |
| CEA 720x480p | 525 | 858 | 62 | 27.000 MHz | 31468.5 Hz | 59.94 |

They agree on the line rate to 0.001% and on the field rate exactly. What
separates them is the pixel clock, and this chip cannot see it: it locks to
sync edges, so the horizontal axis has no native resolution and 320x256 and
640x256 are one source here.

What it CAN measure is the sync width as a fraction of the line --
`STATUS_SYNC_PROC_HLOW_LEN` against the divider -- which is 0.120 against 0.072
for that pair. That is why `SourceTiming::matching()` takes a third argument,
and it is the only horizontal STRUCTURE available to a lookup.

## The vertical half cannot be verified

`STATUS_SYNC_PROC_HLOW_LEN` has no vertical counterpart. The chip offers
`VPERIOD_IF`, which is a period, and `STATUS_SYNC_PROC_VSPOL`, which is a
polarity; there is no vertical sync WIDTH anywhere in the register map.

So a matched row is checked on its total lines, its field rate and its
horizontal sync width, and the `activeStartLine` and `activeLines` it then
supplies are an assumption that nothing tests. A source may match on every
checked quantity and divide its frame differently:

| mode | source `v_timings` | source active starts | published row |
|---|---|---|---|
| 800x600@60 | `4,23,0,600,0,1` | 27 | 27 |
| 640x480@60 | `2,32,0,480,0,11` | 34 | **35** |

`RetroScaler-Acorn.mdf`. The 640x480 mode matches VESA on total, rate and sync
width and starts its active image a line earlier than the standard does.

**This is a property of the part, not of the lookup.** No rearrangement of the
tables can check a vertical number, so any vertical placement taken from a
standard is a guess that happens to be well-informed.

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

Three ways to close that, none of them yet chosen:

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
