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
line out of 2200. The vertical jump at onset is **+138 emitted rows**, and it
is quantised -- +138 for three or four frames, then -69, then back to -2 --
across all three events.

**THE VERTICAL JUMP IS THE COUNT ANOMALY, IN SOURCE LINES.** At `VDS_VSCALE`
488 on a doubled line, one source line is 4.20 emitted rows, so +138 rows is
**32.9 source lines**. The register reads 337 against 308, which is 29 counted
lines, and a NOR'd csync has no H edges for the 3 lines of vertical sync -- so
29 counted plus 3 uncounted is 32 source lines against the 32.9 measured. The
vertical sync is detected about 32 lines late on one frame, and the capture's
vertical origin moves with it.

## What the sync type changes

Same source, same mode, same divider, same raster, 180 s a leg, 1 ms sampling,
four minutes apart:

| | composite | separate |
|---|---|---|
| `STATUS_MISC_PLLAD_LOCK` duty | **74.3%** | **97.3%** |
| lock transitions | 67.6/s | 7.7/s |
| `STATUS_SYNC_PROC_HTOTAL` | 2200 throughout | 2200 throughout |
| `STATUS_SYNC_PROC_VTOTAL` | 308 x26827, 309 x373, 310 x4, **337 x3** | **311 in 28343 of 28343** |
| shear events | 1 in 183 s | 0 in 183 s |

**THE LOCK DUTY REPEATS.** Four runs on composite spanning two hours and two
cadences: 73.8% of 7511 samples at 25 ms, 74.3% of 27207 at 1 ms, and 74.1%
and 74.5% of about 6900 each on two 45 s runs taken afterwards, with the flip
rate 67.5..68.8/s in every one. It is a property of the arrangement, not a
reading.

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

## A third arrangement puts the lock duty and the shear on different axes

The Wii at 480p on `ypbpr` is sync on green -- `SP_SOG_MODE` 1, a sliced
arrangement like composite -- on a direct analog path, with a serrated vertical
interval where the RISC PC's NOR'd csync has none. Measured the same way, 180 s
at 1 ms:

| | lock duty | flips/s | shear events |
|---|---|---|---|
| RISC PC, composite | 74.3% | 67.6 | yes |
| **Wii, `ypbpr`, sync on green** | **84.7%** | **47.0** | **none in 183 s** |
| RISC PC, separate | 97.3% | 7.7 | none |

**SO A DEPRESSED LOCK DUTY IS NOT SUFFICIENT FOR THE SHEAR.** The Wii sits
between the two and does not shear, and a long session of play shows none. The
duty tracks whether the arrangement is SLICED, not whether it shears, so it is
a companion of the sync type rather than the mechanism. 183 s is about one
expected event's worth at the composite rate, so the Wii leg bounds the rate
rather than proving zero.

The Wii also carries the byte tearing independently: `hlow_len` 87 and 1122 are
genuine and 98 (`0x062`) and 1112 (`0x458`) are their crossings, which is the
same structure on a different source and divider.

## Registers that do not move the lock

Recorded so they are not tried again. Everything here is scored on the lock
duty at 1 ms, 7000 to 18000 rows a leg.

| leg | lock duty | flips/s |
|---|---|---|
| composite, the engine's own coast 7/2 | 74.4% | 67.5 |
| composite, `SP_NO_COAST_REG` 1 -- coast off | 77.1% | 63.1 |
| composite, `SP_DIS_SUB_COAST` 1 | 72.7% | 69.2 |
| `ADC_SOGCTRL` 3, 5, 7, 9, 12, 16, 20, 26 | 73.5..75.0% | 67..69 |

**`SP_HS2PLL_INV_REG` AND `SP_HS_POL_ATO` ARE STILL UNTESTED ON COMPOSITE**, and
a run that appeared to test them did not: the source had been left on separate
sync by an interrupted script, so all four of its legs read the separate-sync
figure of about 97.6%. The two fields do differ between the arrangements --
composite holds `SP_HS2PLL_INV_REG` 1 and `SP_HS_POL_ATO` 0, separate sync 0
and 1, and `applyForSyncType()` writes neither on the csync branch. On separate
sync, inverting `SP_HS2PLL_INV_REG` costs 2.5 points and doubles the flip rate,
which is the only thing that run established. **Read `SP_SOG_MODE` back before
scoring any leg.**

## What is open

Which part of the composite path holds the ADC PLL at three-quarters lock --
though the Wii leg makes this a side question rather than the fault.

**THE LIVE CANDIDATE IS THE TWELVE SINGLETON `hlow_len` READINGS.** They are
the only outliers the tearing does not explain: measured against the frame
period the interval's own readings find, they sit at a phase concentration of
0.151 against a control of 0.124, so they fall anywhere in the frame, where the
crossings sit on the interval at 0.906. One of them -- `hlow_len` 1 -- is the
sample immediately before the shear, 30 ms ahead of the 337 count. Ten
occurrences in 27207 samples at 1 ms is one line in 420 sampled, so the rate
they really occur at is unknown and needs an instrument that reads the register
rather than the interval.

Whether the 74% lock duty is the shear's cause or its companion. The duty does
not dip at the event: 0.738 inside the window against a 0.743 run mean. So the
proximate trigger is the 29-line count anomaly, and the lock duty is so far a
property of the arrangement rather than of the event.

`STATUS_SYNC_PROC_HTOTAL` reads 2200 in every sample of both legs, which is not
evidence either way: it counts ADC clocks per line against a PLL locked to
hsync, so it echoes the divider by construction and can see a frequency error
but not a phase one.

## The instruments

`picture_shear.py` reads the displacement per band and, with `--rows`, the ramp
itself. `sampling_stats.py` queues the log at 1 ms, holds the socket, and flags
an outlier that decomposes as a byte crossing.
