# The capture floor followed a polarity the hardware had already normalised

`SourceMeasurement::readSource()` writes `SP_HS_INV_REG` before the hsync low
time is counted, so a high-active source and a low-active one both reach the
sync processor's line counter as a pulse at the head of the line. That is the
whole point of the write: `STATUS_SYNC_PROC_HLOW_LEN` is the LOW time, so an
uncorrected high-active source reports the line minus the pulse -- around 0.9,
which `VideoSourceLine::forDuty()` refuses -- and every consumer downstream sees
one shape once it is corrected.

The polarity read *before* that write was nevertheless carried on into
`HsyncPulse`, and `VideoSourceLine::firstCapture()` branched on it:

```
return lagUnits_ + headBlankingUnits_ + (syncAtHead_ ? syncUnits_ : 0);
```

`videoAt()` carried the matching term, subtracting the sync interval on the
same branch. Both described a line origin that the normalisation had already
moved.

## What it cost

A low-active source got a capture floor a whole sync width early, so the window
opened inside the hsync pulse and captured the pulse and the back porch as
picture. Measured on the RiscPC at 640x480@60, `PLLAD_MD` 1566:

| | before | after |
|---|---|---|
| `STATUS_SYNC_PROC_HSPOL` | 0 | 0 |
| `STATUS_SYNC_PROC_HLOW_LEN` | 180 | 180 |
| `IF_HB_SP2` | **72** | **253** |
| `IF_HB_ST2` | 1565 | 1564 |
| capture | 1493 | 1311 |

At a 100% framing that put a black bar down the left of about a fifth of the
screen, and the window then ran out of line before the source's active video
ended, taking the test card's right-hand castellations with it.

The same bench at 800x600@60 sits on the same divider and sends high-active
hsync, so it took the other branch and was placed correctly -- which is why the
fault read as something peculiar to one mode rather than as a branch. The
comparison is the one that identifies it: same source, same cable, same line
length, capture floor 72 against 265.

## The shape to recognise

A value normalised at the hardware boundary must not be carried into the solver
as well. Here the polarity had two owners -- the sync processor, which corrected
it, and the capture window, which compensated for it -- and the second
compensation was applied to a signal that no longer needed it. Every register
read self-consistent throughout, and the arithmetic was right for a premise that
had stopped being true one function call earlier.

`CLAUDE.md` states the rule the fix restores: normalise at the hardware
boundary, write it once, and let everything downstream see one shape.

## It came back, through the type that replaced the float

The branch was removed once and returned: `forDuty()` was given an `HsyncPulse`
in place of a bare duty, and the polarity was carried into the new type as
`syncAtHead` and read straight back out. The refactor was about refusing a
substituted duty and changed nothing else, so the term arrived with it unnoticed
and both the floor and `videoAt()` branched again.

Measured on the bench at 640x480@60 on `vga`, separate sync, read off the
emitted frame with the USB capture rather than off the panel:

| | with the branch | without |
|---|---|---|
| `IF_HB_SP2` | **89** | **260** |
| the standard's 18.0% of a 1447-unit line | 260 | 260 |
| black columns at the left of the emitted frame | **283** | **4** |
| the card's right-hand columns | off the end of the line | on the panel |

171 units early is 171/1158 x 1920 = 283 emitted columns, which is the band
measured. Translating the window by the sync width with automation frozen --
`IF_HB_SP2` 89 -> 260, `IF_HB_ST2` 1247 -> 1418, nothing else touched -- took
the band to 4 px and brought the whole card back, before any firmware change.

800x600@60 sends high-active hsync, took the other branch and was correct
throughout: 14 px before and after. That pairing is the same one this page
opened with, and it is what identifies the fault as a branch rather than as
something peculiar to one mode.

**The polarity is off the type entirely now**, rather than left on it unread. It
has two live owners -- the normalising write, and `SourceKey`'s identity for the
framing lookup -- and a third copy sitting on the pulse is what let a refactor
reintroduce the compensation without anyone deciding to.
