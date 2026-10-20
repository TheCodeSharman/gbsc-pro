# The composite shear rides a half-locked ADC PLL

On `vga` at 320x256@50 the picture shears a few times a minute on composite
sync and never on separate sync. Each line is displaced further than the one
above, the picture is displaced down with it, and black fills what the playback
does not reach. It lasts 8 or 9 capture frames at 60 fps -- about seven source
frames -- and returns.

**THE SHEAR IS HORIZONTAL AND `picture_stability.py` IS A VERTICAL
INSTRUMENT.** That one collapses each frame to a ROW profile, so it reports
whole-line jumps and is largely blind to this. `picture_shear.py` is the other
axis.

## The waveform is deterministic

Three events in one 150 s clip, at frames 1989, 3568 and 6076, give the same
per-band displacement frame for frame -- the first displaced frame of each
reads `-124 -192 +412 +832 -712 -40 -416` against `-124 -196 +412 +832 -712 -40
-416` and `-120 -196 +412 +832 -712 -40 -416`, and all three end on
`+440 +376 +324 +608 +240 +220 +164 +100`. Noise does not repeat; a settling
transient does.

Resolved per row group at full width, the displacement is a clean ramp:
**-0.333 emitted px per emitted row**, which is 0.58 ADC samples per captured
line out of 2200. The vertical jump at onset is **+138 emitted rows**, which is
33 source lines at this framing.

## What the sync type changes

Same source, same mode, same divider, same raster, 180 s a leg, 1 ms sampling,
four minutes apart:

| | composite | separate |
|---|---|---|
| `STATUS_MISC_PLLAD_LOCK` duty | **74.3%** | **97.3%** |
| lock transitions | 67.6/s | 7.7/s |
| `STATUS_SYNC_PROC_VTOTAL` | 308 x26827, 309 x373, 310 x4, **337 x3** | **311 in 28343 of 28343** |
| shear events | 1 in 183 s | 0 in 183 s |

The lock bit is one bit in one register, so it carries across the comparison
where a wide field does not --
`a-wide-status-field-tears-across-its-two-bytes.md`. 25.7% of a frame unlocked
is 80 lines of 312, and 67.6 flips against a 50.08 Hz field rate is 1.35 per
frame: a once-a-frame disturbance with a long re-settle, not a continuous
jitter.

**337 IS A GENUINE READING**, 29 lines over the usual 308, and it lands beside
the shear in both runs that caught one -- the only such reading in 7511 samples
at 25 ms, 40 ms before the only event, and three readings in 27207 at 1 ms,
immediately before the only event. It never occurs on separate sync.

## What does not move the lock

Scored on the lock duty at 1 ms, 18000+ rows a leg:

| leg | lock duty | flips/s |
|---|---|---|
| composite, the engine's own coast 7/2 | 74.4% | 67.5 |
| composite, `SP_NO_COAST_REG` 1 -- coast off | 77.1% | 63.1 |
| composite, `SP_DIS_SUB_COAST` 1 | 72.7% | 69.2 |

Turning the coast off entirely buys 2.7 points against a 23-point gap, and
disabling the sub coast is worse. **The coast is at most a tenth of the
instability, and its sign is wrong for a mechanism that is working.**

`ADC_SOGCTRL` is flat across its usable range -- 74.4, 75.0, 73.5, 74.5, 74.5,
74.0, 74.2, 73.7 at levels 3, 5, 7, 9, 12, 16, 20 and 26, with flips 67..69/s
throughout. **The slicing level does not reach it.**

## What is open

Which part of the composite path holds the ADC PLL at three-quarters lock.
Neither the coast nor the slicing level does, and the remaining differences are
`SP_SOG_MODE`, `SP_EXT_SYNC_SEL` and the signal itself -- the same wire carries
csync rather than hsync, so the vertical interval arrives on the PLL's own
reference.

Whether the 74% lock duty is the shear's cause or its companion. The duty does
not dip at the event: 0.738 inside the window against a 0.743 run mean. So the
proximate trigger is the 29-line count anomaly, and the lock duty is so far a
property of the arrangement rather than of the event.
