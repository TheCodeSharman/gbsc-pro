# The equalisation probe cannot see serration, and the coast needs serration

`Tv5725::EqualisationMeasurement` compares the sync processor's line count at
two `SP_H_PULSE_IGNOR` thresholds -- 49/1000 of the line, which rejects pulses
at half-line spacing, against 28/1000, which admits them -- and calls the
difference a vertical interval. The coast was gated on that answer. **It is the
wrong fact**, and the two differ on a bench source.

## What the probe can and cannot see

Measured with automation frozen, eleven counts per threshold, the held value
restored on the way out:

| source | divider | rejecting | admitting | difference |
|---|---|---|---|---|
| Wii 480i | 2200 | 260 | 270 | **+10** |
| Wii 480p | 1212 | 524 | 524 | **0** |

Both medians are over counts that never moved: 480p read 524 eleven times at
each threshold.

So the probe sees **equalising pulses**, which sit at half-line spacing and
double the edge rate over the lines they occupy. It cannot see **serration**,
because a serrated broad pulse is one long low period per line -- far wider than
either threshold -- which clears both and is counted once per line. The counts
agree and the source reads as carrying nothing.

Equalising pulses serve interlace: they exist so a vertical integrator triggers
at the same point on both fields despite the half-line offset that distinguishes
them. A progressive source has no such offset and no reason to carry them.

## 480p is serrated, and the count says so

The discriminator is **how many lines the separator loses**. A vertical sync
held at one level for N lines takes the horizontal edges with it and the counter
loses N. A serrated one delivers an edge every line and the counter loses none.

| source | sync | frame lines | `STATUS_SYNC_PROC_VTOTAL` | counted | lost |
|---|---|---|---|---|---|
| RISC PC 320x256@50, separate | separate H | 312 | 311 | 312 | 0 |
| RISC PC 320x256@50, composite | NOR csync | 312 | 308 | 309 | **3** |
| Wii 480p, `ypbpr` | SOG csync | 525 | 524 | 525 | **0** |

The RISC PC's composite sync is a NOR, and
[`the-risc-pc-composite-sync-is-not-serrated.md`](the-risc-pc-composite-sync-is-not-serrated.md)
measures its shortfall as exactly the vertical sync width on four modes. The Wii
at 480p loses nothing on a composite-style signal, so an edge is arriving every
line through its vertical interval: **its vertical sync is serrated.**

It is therefore a source that carries no equalisation and is serrated, which is
the combination the probe cannot distinguish from carrying nothing at all.

## And the coast answers to serration, not to equalisation

With `SP_PRE_COAST`/`SP_POST_COAST` held at 0/0 on the Wii at 480p, the engine
never settles: the count dithers 521..525, the measured rate alternates between
31348 and 31468, and `sync pad: away` / `sync pad: driven` repeat about every
2.2 seconds for as long as the pair is held. Restoring the coast recovers it
immediately.

So a source reading **no equalisation** can still require the coast, and gating
the pre lines on the equalisation answer alone admits a state the picture cannot
be held in. The gate takes both that answer and
`VideoPath::measureVerticalInterval()`'s, which asks the functional question --
whether the input formatter can still measure the interval uncoasted:

| source | equalisation | interval at 0/0 | coast that holds |
|---|---|---|---|
| RISC PC composite | none | 8 of 8 | **0/0** |
| Wii 480p | none | **0 of 8** | 7/6 |
| Wii 576i | +9 | 8 of 8 | 7/2 |
| Wii 480i | +10 | 8 of 8 | 7/2 |

Every state measured good satisfies both; dropping either admits one that is
not.

Measured on the gate as it stands, both inputs in one session: `vga` solves
`SP_PRE_COAST`/`SP_POST_COAST` 0/0 with count 308 and
`STATUS_SYNC_PROC_HTOTAL` 2200 against the divider, and `ypbpr` on the Wii at
480p solves 7/6 with count 524 and `HTOTAL` 1212 against 1212, steady over four
samples sixteen seconds apart. The console names the measurement it used --
`the formatter measured it uncoasted in 0 of 8, post coast 6` on 480p -- and
the equalisation probe read a false `equalisation` in the same acquisition
without changing the pair, which is the gate's point.

## A second instrument for serration exists and needs no poking

`VPERIOD_IF` measures the frame through the input formatter, which counts the
interval whatever the sync processor does with it. The shortfall between the two
is the serration answer, readable at the held coast in one pass:

| source | `VPERIOD_IF` | in lines | counted | shortfall |
|---|---|---|---|---|
| RISC PC composite | 623 | 311.5 | 308 | **3.5** |
| Wii 480p | 524 | 524 | 524 | **0** |

Both steady over 60 samples with `STATUS_IF_VT_OK` 1 throughout. The unit
differs because the RISC PC is line doubled and 480p is not, so the comparison
is `period / count` against its exact integer rather than a raw difference.
`tools/gbsc-pro-hwtest/serration_probe.py` reads the pair.

## What the probe is still for

Choosing `SP_H_PULSE_IGNOR`, which is what it was built for and which genuinely
asks about half-line pulses --
[`the-composite-count-dithers-where-the-ignore-nears-the-pulse.md`](the-composite-count-dithers-where-the-ignore-nears-the-pulse.md).

It is also the better of the two interlace instruments on this bench.
`SourceMeasurement::measureScanType()` keys on the count alternating by one, and
the equalisation answer separates the interlaced Wii modes from every
progressive source where the count does not:

| source | equalisation | count alternates |
|---|---|---|
| Wii 480i | +10 | yes, 259/260 |
| Wii 576i | +9 | not measured at a usable rate |
| Wii 480p | 0 | no |
| RISC PC composite | 0 | no |

Two cautions before wiring it to the deinterlacer. Equalisation implies
interlace by convention rather than by identity. And the asymmetry
`measureScanType()` states still holds -- deinterlacing a progressive source
corrupts the picture where leaving an interlaced one combed does not -- so a
second instrument may raise confidence in an interlaced verdict and must not
override a progressive one on its own.

## The RISC PC's own interlace is detectable by neither

ModeServ's `INTERLACE ON` sets VIDC20's interlace from here, so the case is
testable without a bench trip. Sampled with `Tv5725::SamplingLog` at 25 ms over
20 s -- fast enough to see a per-field alternation, which HTTP point reads at
seconds apart cannot:

| | `sp_vtotal` | consecutive pairs differing by 1 | `vperiod_if` |
|---|---|---|---|
| `INTERLACE OFF` | 308 x744, 309 x8 | 16/751 = **2.1%** | 623 x752 |
| `INTERLACE ON` | 309 x746, 310 x10 | 20/755 = **2.6%** | 624 x756 |

An interlaced source alternates on about half of consecutive pairs, which the
Wii in 480i does. 2.1% against 2.6% is one noise floor, so
`SourceMeasurement::measureScanType()` cannot see this and neither can the
equalisation probe -- a NOR composite signal carries no half-line pulses whether
VIDC20 interlaces or not.

**The shortfall does carry a hint, and it is not usable.** Both counters move by
one unit, but in different units, so the shortfall changes by half a line:

| | counted | `vperiod_if` in lines | shortfall |
|---|---|---|---|
| `INTERLACE OFF` | 309 | 311.5 | 2.5 |
| `INTERLACE ON` | 310 | 312.0 | 2.0 |

Both counters are flat across every sample, so the half line is measured rather
than inferred from noise. It is still not a detector: it is a DIFFERENCE between
two states of one source, where the engine only ever sees one, and a shortfall of
2.0 lines is equally what a different unserrated source reads.

**`VPERIOD_IF`'s unit is the thing worth pinning.** It is taken as half-lines
here because `period / count` is about 2, but it measures 2.016 and 2.013 rather
than 2.000 and that residual is unexplained. Pinned, the shortfall becomes an
absolute figure comparable against what serration predicts, and one reading
stands alone -- which is what both this and the serration gate want.
