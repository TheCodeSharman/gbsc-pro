# The RISC PC's composite sync is a NOR, and carries no serrations

`sourceHasSerratedSync()` is `sourceLowLineRate() && SyncMeasurement::isCsync()`
-- 15 kHz plus composite sync, therefore broadcast structure. Measured on the
bench source, the structure is not there.

## The source's datasheet names which composite it emits

VIDC20 builds two different composite signals and puts them on two different
pins. `ereg[19:16]` selects per pin: the HSYNC pin offers `HSYNC`, `nHSYNC`,
`CSYNCnor` and `nCSYNCnor`, and the VSYNC pin offers `VSYNC`, `nVSYNC`,
`CSYNCxnor` and `nCSYNCxnor`. A VGA connector carries composite sync on the
HSync pin, which is the one this board takes on `vga`, so **the NOR form is
what arrives.**

That settles what the vertical interval can contain. `!(H | V)` with V asserted
is one level for the whole interval whatever H does underneath it, so the
horizontal edges are not attenuated or inverted, they are absent. The XOR form
would have kept an edge per line -- shifted by the sync width and inverted --
and would have cost no lines at all.

**The count discriminates between the two forms, and it picks the NOR.** The
shortfall below is exactly the vertical sync width on four modes; the XOR form
leaves no shortfall to explain.

Broadcast composite sync puts two things in the vertical interval that a
computer's does not: *equalising pulses*, narrow and at half-line spacing, which
exist so an integrator triggers at the same point on both fields of an
interlaced signal; and *serrations*, brief returns to high inside the broad
pulses, which exist so the horizontal oscillator keeps receiving edges while
vertical sync is asserted. A progressive computer mode needs neither.

## The width filter is inert

`SP_H_PULSE_IGNOR` is the filter that rejects them -- RD-5725-1.1: "H pulse less
than this value will be ignored, this counter is start when sync large
different". The firmware writes 0x6b where it believes the source is serrated
and 0x02 where it does not.

At `MODE X320 Y256 C256 F50` with `SYNC 1`, automation frozen, every write read
back, and the shipped state repeated as a control between every treatment:

| state | `STATUS_SYNC_PROC_VTOTAL` |
|---|---|
| control, five repeats | 308 x10 each time |
| `SP_H_PULSE_IGNOR` 0x02 -> 0x6b | 308 x10 |
| `SP_VSYNC_TGL_THD` 3 -> 1 | 308 x10 |

Raising the threshold from 2 to 107 rejects every pulse narrower than 107 units
on a line whose sync pulse measures `STATUS_SYNC_PROC_HLOW_LEN` 144. **Nothing
changes.** A signal carrying equalising pulses at half-line spacing could not
survive that untouched, so the pulses are not there.

**The filter is not applied by the sync type in any case.**
`SyncProcessor::applyForSyncType()` calls `applyPulseIgnore()` on its
separate-sync branch and not on its csync one, so on a csync source the field
holds whatever `prepareSyncProcessor()` computed earlier in the load. Measured on
the bench csync leg it holds 2, the unserrated value, while
`sourceHasSerratedSync()` is true.

## The missing lines ARE the vertical sync

An unserrated broad pulse carries no horizontal edges, so there is nothing for
the line counter to count while vertical sync is asserted. The count comes up
short by exactly that interval, measured on four modes whose vsync widths
differ:

| mode | `v_timings` vsync | separate | composite | short by |
|---|---|---|---|---|
| 800x600@60 | 4 | 627 | 623 | 4 |
| 640x352@60 | 3 | 363 | 360 | 3 |
| 640x480@60 | 2 | 524 | 522 | 2 |
| 320x256@50 | 3 | 311 | 308 | 3 |

So the shortfall is not a fixed number of lines and not an equalisation interval
of some standard size: it is the mode's own vertical sync width, and it follows
that width wherever the mode puts it.

**It is a consequence of the signal rather than a fault in the coasting.** A
serrated source keeps feeding the counter through the vertical interval; this
one cannot, so no coast setting recovers the lines, and a count taken across
vertical sync is short on any unserrated composite source.

**What it costs is the raster match, and through it the picture.**
`SourceTiming::lookUp()` matches on the count plus one against a published
total, so 623 asks for a 624-line raster where the table holds 628 and nothing
matches at all. The timing goes unpublished, the solve falls through to its
default guess, and `VDS_HSCALE` lands at 1023 against the 958 separate sync
solves -- near enough unity that a near-full-line capture plays out unmagnified.
The card then repeats about 1.7 times across over heavy green tearing.

It also moves the source's identity, which is keyed on the count.
`../source-identity-and-framing-lookup.md`, `../known-issues.md`.

### Putting the interval back

The count is short by a known-shaped amount, so it can be reconstructed -- but
not from the standard that the count itself selects. **Taking the vsync width
off the matched row is circular**: the wrong count is what stopped the row
matching, and the part measures no vertical sync width to break the loop with.

Two ways that do not close the circle:

- **Time the line, do not count it.** The lines whose edges are missing still
  occupy TIME, so a period survives what a count cannot: the true total is the
  vertical period over the horizontal one, and both are tick counts off
  `DEBUG_IN_PIN`, so the tick clock cancels and no Hz conversion is needed.
  `debugPinPulseTicks()` already returns an interval between two adjacent edges
  rather than a count over a window, and `TestBusRateMeasurement` is what points
  the bus at a signal and inverts it. It needs no table, so it also answers for
  a source matching no published raster.

  **An edge COUNT taken anywhere recovers nothing**, including one taken over
  the test bus: the edges are not on the wire, so every instrument that counts
  them is short by the same lines. The distinction is the whole difference
  between the two.

  **One sample in the vertical interval reads long, never short**, being an
  interval of several lines rather than one. So the minimum of several samples
  is the line period and the straddling sample rejects itself.
- **Carry the vertical sync width in the table and reconstruct during the
  match**, accepting `measured + syncLines + 1 == totalLines` on a csync source.
  The standards publish the width, and the field rate and the sync duty still
  separate the rows. Cheap and host-testable, but it only answers for a mode
  the table holds.

Neither is implemented.

## Coast is binary, not proportional

The coast window is the other setting `sourceHasSerratedSync()` drives. Removing
it costs ten lines and the steadiness with them:

| `SP_PRE_COAST` / `SP_POST_COAST` | `STATUS_SYNC_PROC_VTOTAL` |
|---|---|
| 0 / 0 | 297 x1, 298 x8, 299 x1 |
| 7 / 3 (shipped) | 308 x10 |

Ten lines is exactly `7 + 3`, which suggests the count tracks the coast total.
**It does not.** Holding the stop at 3 and sweeping the start, with the shipped
7/3 repeated between every treatment:

| pre | sum | predicted | measured |
|---|---|---|---|
| 5 | 8 | 306 | 308 x10 |
| 9 | 12 | 310 | 308 x10 |
| 10 | 13 | 311 | 308 x10 |
| 11 | 14 | 312 | 308 x10 |
| 13 | 16 | 314 | 308 x10 |

Any non-zero coast gives 308; zero gives 298. The model is refuted -- coast is
on or off as far as the line count is concerned, and its length buys nothing
between 8 and 16.

## What is left unexplained

**Whether the coast circuit regenerates the missing edges on some internal
stage.** Coasting exists to keep a horizontal oscillator running through a
vertical interval, so a stage downstream of it may carry a line-rate signal the
counter is not reading -- in which case the count is taken upstream of a signal
that already has what it needs. `SP_TEST_MODULE` puts one stage at a time on
the test bus, so a sweep on the csync leg answers it.

It needs a window long enough for the difference to show: the shortfall is 3
lines in 311, so a 25 ms window at 50 Hz cannot separate the two, where a
one-second window expects 15625 edges against 15475.
