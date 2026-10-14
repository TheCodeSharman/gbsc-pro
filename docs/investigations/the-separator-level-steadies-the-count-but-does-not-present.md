# Composite-sync pass-through: the separator level steadies the count, and three other candidates do not

A composite-sync RGBHV source in pass-through emits nothing. The same source,
same cable, same mode, on separate sync fills the screen. Four candidates were
measured on 800x600@60 into 1920x1080 with `scale-rgbhv = 0`; three are refuted
and the fourth moves the count without producing a picture.

Horizontal is exact throughout and is not implicated:
`STATUS_SYNC_PROC_HTOTAL` 2038 against `PLLAD_MD` 2038, line rate 37879 against
the source's 37919. What is wrong is the vertical count, against a correct 628.

## The separate-sync baseline, for comparison

`SP_SOG_MODE` 0, coast 0/0, `SP_H_PULSE_IGNOR` 255, `STATUS_SYNC_PROC_VTOTAL`
627, and a full-screen frame at mean luma 81.8. Reproduced at the start and end
of every run below.

## Refuted: the encoder holding a stale timing

`PAD_SYNC_OUT_ENZ` dropped and re-driven by hand does not recover it, and the
reason is that the pad was never stuck. Across 80 s the console carries **six
`sync pad: away` and six `sync pad: driven`**, paired, because the engine
re-solves continuously and each solve takes the output away:

```
 2.20 away   21.60 driven   21.87 away   35.00 driven   35.60 away
53.73 driven/away   56.21 driven   56.33 away   60.94 driven   60.97 away
```

A hand-written 1 read back as 0 within 2.5 s -- the firmware re-drives it. Every
black capture and every `present: false` reading lands in a withdrawal window,
so `present` is not a stable property of this state and a single reading of it
says only which window it fell in.

## Refuted: the coast pair

Eleven pairs swept on the live engine through `/coast`, which is the right
instrument here because freezing would hold the re-solve that makes the count
visible. **Every one emits nothing.**

```
coast  0/ 4   sd=  28.1   vt=[521, 622, 623, 624]
coast 10/ 4   sd= 234.5
coast  7/ 4   sd= 317.6
coast  7/ 6   sd= 415.1   the default
coast  3/ 4   sd= 545.4
```

The pair does tighten the count -- 0/4 is an order of magnitude steadier than
the default 7/6 -- so it is not inert, and it is not the reason nothing is
emitted.

## Refined, not refuted: the separator level

`ADC_SOGCTRL` reaches the sync processor only with `SP_SOG_MODE` 1, so it is
live on composite sync and inert on separate sync, which matches the asymmetry.
Held against the ladder that rewrites it every 500 ms, 279 writes over the run:

| level | `STATUS_SYNC_PROC_VTOTAL` | sd | frame |
|---|---|---|---|
| 24, free-running | 375, 518, 533, 591, 602, 622 | 384.5 | flat |
| 4 | 110, 240, 341, 416, 429, 594 | 295.7 | flat |
| 8 | 594, 622, 623, 624, 682, 720 | 319.9 | flat |
| 13 | 622, 623, 624, 1248, 1791 | 311.6 | flat |
| **20** | **623, 624** | **0.4** | flat |

**The level is the lever for the count and it is not monotonic**: 20 is steady
to 0.4 where the 24 the engine free-runs at scatters by 384. So a walk that
passes through 20 without an acceptance test has no way to notice it, and the
level the engine settles on is worse than one four steps below it.

**A steady count is not sufficient.** At level 20 the count is still four lines
short of 627/628, the engine stays `absent`, and nothing is emitted. So the
level is necessary and something downstream of the count is still refusing.

**The earlier reading that 4 and above gives a steady count does not transfer.**
That was measured at 640x480@60, where 4 sufficed; at 800x600@60 level 4 is
among the worst. The level is per source, which is the argument for finding it
by probing rather than by constant.

## The full-field sweep exists and nothing calls it

`SyncOnGreen::sweep()` walks the level across the whole field, up before down,
and stops at the first level `SourceMeasurement::sourceIsMeasurable` accepts.
Its only caller is `VideoSourceAcquisition::establishSeparatorLevel()`, which
**has a declaration and a definition and no call site**. The three live sites
call `acquireSeparatorLevel()` instead, which runs `SyncOnGreen::acquire()` --
the walk the header states only descends.

So the machinery that could find level 20 from a start of 13 is present and
unreachable, and the walk that does run cannot climb. Consistent with the
console, where `separator level:` appears **zero times** in 80 s across six
solve cycles while `sync arrangement: composite or SOG` does appear.

The console drops bursts, so an absent line is weak evidence on its own; the
absent call site is what carries this.

## What is left

The count is steadiable and the engine still does not acquire, so the remaining
question is what rejects a steady 623/624 -- the four-line deficit against
627/628, or the held rate, or the field-rate measurement that
`known-issues.md` records reading a line-derived signal on this path.
