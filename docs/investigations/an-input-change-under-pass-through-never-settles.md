# An input change taken while passed through never settles

Changing input with the output already passed through and pass-through still
permitted leaves the engine in its no-sync branch indefinitely. The same change
with `uopt->preferScalingRgbhv` set completes in about ten seconds.

## Measured

One session, both directions, on the same pair of sources.

**Stuck.** `vga` at 1024x768@60 passed through -- `OUT_SYNC_SEL` 1, `PLLAD_MD`
2039, `HD_HSYNC_RST` 2047, a clean full-screen picture -- then
`/input?src=ypbpr` with `preferScalingRgbhv` clear:

```
h: 214 v:---- PLL:4 ... m:0 ht:2039 vt:  97 hpw: 149 u: 97 s: 0 S:14 W:-57
```

`HPERIOD_IF` reads the 214 a 480p source is due, so the line rate is arriving.
`STATUS_SYNC_PROC_VTOTAL` sits at 97, `STATUS_MISC_PLLAD_LOCK` 0, and
`/geometry` holds `lineRateHz` 48913 -- the rate of the source that is gone.
`m:0` with `s: 0` is the no-sync branch on every pass. Held for over two
minutes, with `/sc?~` in the middle of it, and it does not recover: the run
reached `u: 97` and cycled there.

**Not stuck.** From `vga` at 320x256@50 **scaled**, `/uc?x` to permit
pass-through -- which changes nothing for a 15 kHz source, so the route stays
scaled -- then the same `/input?src=ypbpr`: acquired at 31395 Hz within 35 s,
passed through, `PLLAD_MD` 2039 against `STATUS_SYNC_PROC_HTOTAL` 2039,
`VTOTAL` 524, PLL locked, `HPERIOD_IF` 214, and the Wii's menu clean and full
screen.

**And leaving works the other way too.** From that passed-through Wii,
`/uc?x` to veto pass-through and then `/input?src=vga`: acquired at 15625 Hz in
under 40 s, back on the scaling path.

## What separates the two

The engine measures through whatever sampling is in force, and pass-through
holds the CHANNEL's sampling rather than the reference one:
`VideoPath::prepareToMeasure()` returns before installing the reference clock
when the output is passed through, which is right while the source under it has
not changed and is what stopped the reference clobbering a working picture.
`investigations/the-reference-clock-is-applied-to-a-working-picture.md`.

**THE STALE QUANTITY IS NOT THE DIVIDER.** `HdBypass::dividerFor()` returns the
channel bound of 2039 at every rate involved here, because the clock bound only
binds above about 79 kHz:

| line rate | clock bound | divider | ADC clock at that divider |
|---|---|---|---|
| 48913 Hz, 1024x768@60 | 3312 | 2039 | 99.7 MHz |
| 37879 Hz, 800x600@60 | 4276 | 2039 | 77.2 MHz |
| 31395 Hz, Wii 480p | 5160 | 2039 | 64.0 MHz |

So the divider the new source is measured through is the one it would have been
given anyway. What `Adc::applySampleRate()` chose from the rate held at the
switch -- the crossover row and the VCO gain -- is what does not carry: the
stuck reading is 2039 against a source wanting 64.0 MHz through a group set for
99.7, with `STATUS_MISC_PLLAD_LOCK` 0 in every sample. The row is the one
quantity in the group that a rate change invalidates on its own.

**This is a reading of the difference, not a measurement of the mechanism.**
What was measured is the two outcomes, the state either side, and the lock bit.
Two tests would separate it. Enter pass-through and change the SOURCE's mode by
enough to cross a crossover row -- 1024x768 to 800x600 is 99.7 to 77.2 MHz --
and see whether that settles, which decides whether the input change matters at
all or only the size of the rate step. And write the group for the new rate by
hand from `/sampleclock` while stuck, which says whether the row is the whole of
it.

**An earlier measurement is not the counter-example it looks like.** A 1024x768
change taken while passed through is recorded as holding the divider at 2039
with a full-screen picture, which reads as "a mode change while passed through
is fine". It does not settle this: that change is between rates the same
crossover row covers.

## What it costs, and the way out

The route is re-decided only on a settled measurement, so a source that cannot
be measured cannot leave pass-through -- the state is self-holding rather than
slow.

`/sc?~` clears it, but only once the input is one that can lock: run against the
stuck `ypbpr` it swept without settling, and run again after `/input?src=vga` it
left bypass (`OUT_SYNC_SEL` 0) and re-acquired the RiscPC in about 90 s.

`HPERIOD_IF` rails across the excursion and the first mode round trip did not
clear it -- `511`, `255`, `271` with `STATUS_IF_HT_OK` 0, and an
`ADC_INPUT_SEL` bounce left it there too. A second round trip, 1024x768 and back
to 320x256, restored 431 in 4 of 4 samples with `STATUS_IF_HT_OK` 1. So the
ladder in `../../CLAUDE.md` holds, and the rungs are worth repeating before
escalating to a cold boot.
