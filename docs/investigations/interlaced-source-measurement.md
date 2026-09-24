# The sync processor cannot measure an interlaced source accurately

Measured on a Wii at PAL 576i over YPbPr, 2026-08-26 — the first interlaced
source this project has had.

| quantity | reads | truth | error |
|---|---|---|---|
| line rate from `HPERIOD_IF` (431) | 15625.0 Hz | 15625.0 | exact |
| the engine's `lineRateHz` | 15969 | 15625 | **+2.2%** |
| `STATUS_SYNC_PROC_VTOTAL` | 310 | 312.5 | −0.8% |
| `VPERIOD_IF` | 624 | 625 | −0.16% |

`STATUS_SYNC_PROC_VTOTAL` counts a **field** and `VPERIOD_IF` a **frame**, which
is why 310 against 624 is not a disagreement. But 576i carries **312.5** lines
per field, and a line counter cannot hold the half. The engine derives its line
rate from that count and a field rate measured beside it, and lands 2.2% high.

`HPERIOD_IF` is exact because it does not count lines at all: it measures the
line period against the chip's own 27 MHz, so interlace is invisible to it.
`27e6 / ((431 + 1) * 4)` is 15625.0 Hz to the digit.

## What it costs today

The framing a user tunes is stored against the source's measured identity, and
this source saved as:

```
0 310@52 = 462 9356 387 8887
```

**52 Hz**, for a 50 Hz source. The key is wrong, so a later solve that measures
51 or 50 will not match it and the framing will not come back. On a progressive
source the same mechanism works — `311@50` and `524@60` are both correct in that
same file.

## What it does not establish

That the engine should switch to `VPERIOD_IF`/`HPERIOD_IF`. The geometry model
is built on `STATUS_SYNC_PROC_*` throughout, and **those count in ADC samples
while the IF registers count in IF units against 27 MHz** — the downstream
arithmetic assumes the former. `HPERIOD_IF` also has its own failure modes;
`hperiod-if-railing.md` records three, one of which is a stable wrong value that
every health check scores as healthy.

What is established is narrower: on an interlaced source the sync processor's
line count is short by half a line by construction, and anything derived from it
inherits that. A source key built from it is wrong by enough to miss.

## The other thing interlace does

The last captured line alternates between fields, so a capture window that
reaches into input blanking shows a single shimmering line at the bottom while
everything above it sits still. It reads as a deinterlacing fault and is not one
— it is the framing, and pulling the vertical extent in clears it. The rest of
the picture is stable throughout, which is the tell: bob judder moves the whole
image.


## NTSC 480i alternates where PAL 576i held steady

The same console, the other output mode, measured 2026-09-12 over 1417 samples:

```
STATUS_SYNC_PROC_VTOTAL   260 x736, 259 x681   -- two values, near evenly
VPERIOD_IF                524 x1417
```

576i reads a single steady 310, so **the half-line is expressed as an
alternation in one mode and absorbed into a constant undercount in the other**.
Both are short of the true field: 259.5 against 262.5, and 310 against 312.5.

**The 576i reading survives dense sampling, so the hole is real.** The original
was a single value taken before the sampling log existed, which invited the
explanation that it had simply been under-sampled -- this register is known to
mislead over HTTP, where point reads gave 149, 160, 230 and 299 among the 310s
that the log reported 1050 times out of 1050. Re-measured on the device:

```
Wii PAL 576i    STATUS_SYNC_PROC_VTOTAL   310 in 1186 of 1186, 0 changes
                VPERIOD_IF                624 in 1186 of 1186
                IF_HS_DEC_FACTOR 1, STATUS_IF_VT_OK 1
```

**So an alternating count is not an interlace detector.** One interlaced source
alternates and another does not, which rules out the obvious reading of the
2026-09-12 measurement. Nor is the ratio `VPERIOD_IF / STATUS_SYNC_PROC_VTOTAL`,
which is about 2 on both -- and is 2.02 on the progressive RISC PC under
composite sync as well, because `VPERIOD_IF` counts the doubled IF line rather
than the source's.

**What it costs is acquisition.** `VideoSourceAcquisition::countHeld()` needs four
consecutive identical counts, which an alternating field count never supplies, so
480i never reaches `acquired`: no solve runs for the mode, the output clock is
never seeded, and the picture rolls while every register reads correct.
`mode-detect-answers-before-any-measurement.md`.


## The parity of `VPERIOD_IF` is not a scan type, and is no longer read as one

An interlaced field carries a half line, and `VPERIOD_IF` can hold one -- but
only where the input formatter doubles the line, which is what puts the count in
half lines. Where the line is not doubled the bottom bit is the bottom bit of a
line count and carries nothing.

The rule drawn from that premise was one line:

    interlaced  <=>  (VPERIOD_IF + lineDoubled) is odd

and it was measured unanimous over 3163 samples. Every one of those states was
line-doubled SD, so the premise held throughout and never had to be checked;
`scanTypeFor()` then applied the parity to any count between 200 and 1300.

**It misreads an undoubled source whose frame total is even.** A RiscPC at
800x600@60 over composite sync reads 1255 -- odd, inside the range, and the
source progressive. Measured 721 samples from `loop()`, 1255 in 539 of them,
with the motion-adaptive deinterlacer engaged on it: `MAPDT_VT_SEL_PRGV` 0 and
`WFF_ENABLE`/`RFF_ENABLE` 1, against a clean 640x480 reading 524 with the
deinterlacer off. Separate sync then left it engaged, because `STATUS_IF_VT_OK`
0 makes the period 0, which scored as neither scan type and advanced no run.

So the parity is withdrawn and the alternation below is the whole rule.
`ScanUnknown` is withdrawn with it: a source that cannot be shown to be
interlaced is taken as progressive and the ladder says so. The costs are not
symmetric -- deinterlacing a progressive source corrupts the picture, where
leaving an interlaced one alone combs it and the deinterlacer's own preference
turns it on.

**What that costs is stated where it is paid.** A Wii at PAL 576i holds a steady
310 while genuinely interlaced, 1186 samples with zero changes, so the
alternation does not see it and it is steered as progressive.

**`VPERIOD_IF` still measures the frame, and that is what it is read for**:
against the sync processor's count it gives the vertical sync an unserrated
composite source loses.
[the-risc-pc-composite-sync-is-not-serrated.md](the-risc-pc-composite-sync-is-not-serrated.md)

**The classification cannot supply this.** `s0_00..05` is byte-identical across
a real interlace change at 15 kHz and 50 Hz, `a7 00 00 00 40 10` both ways, so
`STATUS_IF_INP_INT` and `STATUS_IF_INP_PAL_INT` report a vertical-period family
and nothing about scan. Consulted first, as `Deinterlacer` used to, it engaged
the motion-adaptive deinterlacer on the progressive RISC PC and held it there
through a real interlace change in both directions -- measured on the bench,
`MAPDT_VT_SEL_PRGV` 0 with `WFF_ENABLE` and `RFF_ENABLE` 1 in all three states.
With the measurement answering, the same three states read off, engaged, off.

**It only works where `VPERIOD_IF` does**, which is with the sync separator in
the path. On separate sync the register holds debris -- 33 to 101 on the bench
source -- and `STATUS_IF_VT_OK` reads 0 beside it, which is the gate the caller
gives it. There the scan type has no source at all.

### The measured scan type cannot replace the classification here, and the reason is circular

The obvious cleanup -- feed `countIsSerrations()` the measured scan type
instead of the STATUS_00 bits -- does not work, and it fails in the direction
that matters.

`scanTypeFor()` needs the line doubling, and the doubling is solved from the
line count. On a count the serrations have doubled, the scan mode is solved for
that corrupted count and comes out undoubled; the parity rule then reads the
period as progressive, and a progressive source cannot have doubled -- so the
check that exists to catch the doubling disables itself on exactly the source it
was written for. Measured: a 607-line count against a 624 period stops widening
the coast, and the source never comes up.

The gate has to be independent of the count under suspicion, and the
classification bits are. They do not answer the scan type -- nothing here
retracts that -- but they are not derived from the count, which is the property
this use needs.

**`SourceMeasurement::countIsSerrations()` still takes the classification**, and
it is the second consumer of the same unreliable bit. It asks whether a count
could have doubled, which only an interlaced source can do. On every state
measured here it reaches the same verdict either way, so there is no fault to
chase -- but the measured scan type is the better input and is now available
beside it.


## A PAL interlaced source is where the alternation is blind

576i reads `VPERIOD_IF` 624 against a count of 310 and holds both steady, 1186
samples with zero changes on each. The parity rule named it interlaced from
that single sample; with the parity withdrawn, nothing here does.

**Why it holds steady is not established.** The sync-route table above is the
closest thing to a mechanism -- the separator retimes vertical sync and can
absorb the half line into a constant undercount -- and the Wii 480i row
contradicts it, being SOG with the separator in path and alternating anyway. A
broad-pulse width explains the two Wii readings and predicts the four RISC PC
rows wrongly, since a pulse width is a property of the signal where the
behaviour follows the route.

So the bound on the alternation is stated as measured rather than as derived:
**it is blind to at least one genuinely interlaced source, and which sources
those are is not predictable from the timings.**

## On separate sync the count alternates at every raster tried

The alternation is the only scan-type signal that survives separate sync, and it
had been measured at one raster. A second one, same machine and cable, only the
mode and the interlace flag moving:

| mode | field | progressive | interlaced |
|---|---|---|---|
| 320x256@50 | 312.5 | 311 steady, 0 changes in 372 | 311/312, 271 in 369 |
| 640x200@60 | 262.5 | 261 steady, 0 changes in 384 | 261/262, 188 in 382 |

Steps are exactly plus or minus one in both, and no progressive sample ever
moves.

**Set against every interlaced state measured, the hole belongs to the sync
separator rather than to the raster:**

| sync route | field | interlaced |
|---|---|---|
| separate | 312.5 | alternates |
| separate | 262.5 | alternates |
| composite, separator in path | 312.5 | steady 309 |
| composite, separator in path | 262.5 | steady 259 |
| SOG, Wii 576i | 312.5 | steady 310 |
| SOG, Wii 480i | 262.5 | alternates 259/260 |

The separator retimes vertical sync and can absorb the half-line into a constant
undercount; separate sync passes the VSync pin through more directly and the
half-line reaches the counter. **That puts the hole only where the alternation is
not needed**, since `VPERIOD_IF` is a measurement wherever the separator is in
the path.

**What this does NOT test is the signal structure.** Every interlaced state the
RISC PC can produce comes from `*TV vert,interlace`, which offsets the fields but
does not synthesise broadcast equalisation and serration pulses. So the raster is
tested and the vertical interval is not, and a genuinely broadcast-interlaced
source on separate sync is the one thing this bench cannot make. A mode file does
not close that gap -- it would match the active area, not the pulse structure.
The one separator-path source with real serrations, the Wii at 480i, does
alternate, which is at least not evidence against.
