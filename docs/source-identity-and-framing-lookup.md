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
VideoPath::arrivingKey()
    key = SourceKey(lines, fieldRateHz, hsyncDuty, vsyncPositive)

VideoPath::sourceMeasured()
    timing_ = SourceTiming::matching(arrivingKey())

VideoPath::adoptSourceKey()
    if (!framings_.find(arrivingKey(), &framing_))
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

`SourceKey` is the line count, the field rate, the hsync width as a fraction of
the line, and both sync polarities. It is what both lookups are keyed on and
what is persisted against a tuned framing.

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
for that pair. It is the only horizontal STRUCTURE available to a lookup, and it
is in the key for that reason.

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

What a term has to be to join the key is a property of the SOURCE MODE. Four
qualify:

| term | kind | measured by |
|---|---|---|
| line count | with the rate, the horizontal rate | `STATUS_SYNC_PROC_VTOTAL` |
| field rate | the vertical rate | `SourceMeasurement` |
| sync width, as a fraction of the line | horizontal structure | `STATUS_SYNC_PROC_HLOW_LEN` |
| horizontal sync polarity | horizontal structure | `STATUS_SYNC_PROC_HSPOL` |
| vertical sync polarity | vertical structure | `STATUS_SYNC_PROC_VSPOL` |

A vertical sync WIDTH does not join them, because the part never measures one.

**NEITHER POLARITY SURVIVES A SYNC-TYPE CHANGE, so both are recorded as
UNDETERMINED where the arrangement states none** -- a third value equal only to
itself, never a wildcard, since one matching both would make equality
non-transitive. Composite sync and sync on green are those arrangements.

`SourceKey` is persisted in the framing file, so each term that joins it changes
the stored format: a record written before it reads as malformed and is skipped,
which loses stored framings once.

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

## Both polarities are the mode's, and only on an arrangement that carries them

Measured across a source mode round trip, 40 samples a
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

**The horizontal polarity may not go in the identity and the vertical may**,
because only one of them is a property of the MODE. Measured at 800x600@60 with
the source's sync type changed under it and nothing else touched:

| sync type | `SP_SOG_MODE` | VTOTAL | HSPOL | VSPOL | duty |
|---|---|---|---|---|---|
| separate | 0 | 627 | 1 | 1 | 0.12204 |
| composite | 1 | 623 | **0** | 1 | 0.12017 |
| separate again | 0 | 627 | 1 | 1 | 0.12204 |

HSPOL moves, and what it loses does not come back. Measured on three modes, it
reads 0 on composite whatever the mode states, where separate sync gives 1 for
an H+ mode and 0 for an H- one:

| mode | stated | separate | composite |
|---|---|---|---|
| 800x600@60 `sync_pol:0` | H+ | 1 | 0 |
| 640x352@60 `sync_pol:2` | H+ | 1 | 0 |
| 640x480@60 `sync_pol:3` | H- | 0 | 0 |

So it is not inverted by the arrangement, it is constant under it, and no
correction keyed on the sync type recovers the mode's polarity. Composite sync
is sync-tip-low and carries no separate HSync line for the bit to report.

**The source's own datasheet says why, and names which composite it emits.**
VIDC20 offers two, on two pins: the HSYNC pin carries `CSYNCnor`, the NOR of H
and V, and the VSYNC pin carries `CSYNCxnor`. A VGA connector's composite sync
is on the HSync pin, so the NOR is what arrives -- and a NOR saturates, holding
one level for the whole vertical interval whatever H does underneath it. The
mode's horizontal polarity is not on the wire to report.
`investigations/the-risc-pc-composite-sync-is-not-serrated.md`.

**VSPOL GOES THE SAME WAY, WHICH IS MEASURED AND WAS ONCE RECORDED OTHERWISE.**
An earlier reading of this page had it tracking the mode on both sync types. It
does not. Two modes the monitor definition gives as V positive, sampled with
nothing else touched:

| mode | stated | separate | composite |
|---|---|---|---|
| 800x600@60 `sync_pol:0` | V+ | 1 | **0** | 
| 320x256@50 `sync_pol:0` | V+ | 1 | **0** |

Ten samples on composite and six on separate at 800x600, no dither in either.
The three modes the original table covered included only ONE that is V positive,
so that row carried the whole claim on its own.

**One mechanism destroys both.** A NOR is sync-tip-low for whichever pulse is
asserted, so composite states neither polarity -- it is not that the horizontal
is lost and the vertical survives.

A key that moves when the sync arrangement moves loses the framing a user tuned
by doing nothing but changing sync type, which is the opposite of one mode
looking the same on both. Undetermined is what keeps the two legs from claiming
a polarity they cannot see; the count already separates them regardless.

**The sync width survives that change**, shifting 0.0019 -- an eighth of the
tolerance the standards lookup already allows, and a twenty-fifth of the
separation it has to achieve.

**And the line count does NOT survive it: 627 against 623.** So the identity is
already unstable across a sync-type change, before any term is added to it.
`known-issues.md`.

A matched row is checked on its total lines, its field rate and its horizontal
sync width, and the `activeStartLine` and `activeLines` it then supplies are an
assumption that nothing tests, because testing them needs a width the part does
not measure. A source may match on every checked quantity and divide its frame
differently:

| mode | source `v_timings` | source active starts | published row |
|---|---|---|---|
| 800x600@60 | `4,23,0,600,0,1` | 27 | 27 |
| 640x480@60 | `2,32,0,480,0,11` | 34 | **35** |

`RetroScaler-Acorn.mdf`. The 640x480 mode matches VESA on total, rate and sync
width and starts its active image a line earlier than the standard does.

**That much is a property of the part, not of the lookup.** No rearrangement of
the tables can check a vertical BLANKING SPLIT, so any vertical placement taken
from a standard is a guess that happens to be well-informed. Choosing the right
row is a separate question, and the polarity the key now carries is evidence for
it that the table does not yet read.

## Where the implementation departs from the design

**Steps 2 and 3 are one table and the precedence runs the wrong way.**
`SourceTiming::Published[]` holds the DMT rows first and the two CEA rows
(720x480p, 720x576p) last, and `lookUp()` returns the first row that matches. So
where both standards could match, VESA wins, and the design calls for CEA.
Nothing records which standard a row came from, so the precedence is a property
of the array order rather than a stated rule.

The 525-line 60 Hz pair above is the case that would collide, and today it does
not, because the sync width separates them well inside `SyncDutyTolerance`.

**The polarities are in the key and not yet in the table.**
`SourceTiming::lookUp()` takes the whole key and matches a row on the count, the
rate and the sync width, so a source is told from a row by three of the five
terms it is identified by. The standards publish the polarity pair, and matching
on it would let two rows sharing all three be told apart -- which is the
discrimination the vertical blanking split needs and the part cannot supply
directly.

## What this does not explain

At 800x600@60 the published row states the source's own vertical layout exactly
and the top of the picture is still clipped. So the clip has a cause other than
the table, and restating the lookup will not move it.
`investigations/the-vertical-capture-window-is-placed-late.md` carries what is
measured and why its magnitude is not yet established.
