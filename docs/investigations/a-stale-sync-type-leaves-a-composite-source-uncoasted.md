# A stale sync type leaves a composite source uncoasted

A composite-sync source that the engine still believes is separate-sync runs on
the uncoasted configuration. The line counter then loses the lines the vertical
pulse occupies, the count dithers, and the picture bounces vertically without
rolling.

The probe is not at fault. Two models that blamed it are refuted below, both
convincingly and both wrong.

Bench source: RiscPC on `vga`, sync type set from CMOS by ModeServ's `SYNC`.

## What the count does uncoasted

`STATUS_SYNC_PROC_VTOTAL` counts H periods between vertical syncs. Through the
vertical interval a composite H line carries broad pulses rather than sync
edges, so uncoasted those lines are not counted:

| mode | true lines | uncoasted reading | short by | vertical pulse |
|---|---|---|---|---|
| 320x256@50 | 311 | 307 / 308, dithering | 3 | 3 lines |
| 640x480@60 | 524 | 522 | 2 | 2 lines |

The engine solves the line rate as `fieldRate x (count + 1)`, so a count three
short at 50.08 Hz gives 15475 against 15625. The whole of that rate error is the
count; the field rate itself is correct.

Coasting holds the count still -- a steady 308 rather than a dithering 307/308 --
and the bounce stops, confirmed at the panel. **It does not recover the missing
lines**: 308 against 311 either way. That residual is unexplained.

## Why the source was uncoasted

**The held sync type was stale, and nothing re-probes on a sync-type change.**
The line count does not move when a RiscPC changes its sync type -- 311 at
320x256@50 either way -- so no arm fires, `establishSyncType()` reuses what it
holds, and the source runs on the configuration chosen for the type it used to
send.

Forced to re-probe, the probe is right every time:

```
source SYNC 0  ->  own V sync: yes after 203ms / 3ms / 2ms / 53ms / 2ms
source SYNC 1  ->  own V sync: no  after 1000ms / 1001ms / 1000ms
```

So `SyncMeasurement` answers correctly for this source on both sync types, and
the configuration it selects is right. What is wrong is how long the answer is
allowed to stand.

## Refuted: the probe measures the wrong thing

The model was that "does the source drive V" and "is the sync composite" are
independent, that a RiscPC in composite mode drives V as well, and that the
probe therefore answers yes about a composite source.

It is wrong at the measurement. The probe answers **no** on that source, as
above. The electrical claim that both lines carry composite signals does not
reach `STATUS_SYNC_PROC_VSACT` as a V sync, which is what the probe watches.

## Refuted: STATUS_SYNC_PROC_VSACT distinguishes them

Proposed as a one-bit discriminator on the strength of 30/30 ones on a
separate-sync source and 0/30 on a composite one. **The two readings were taken
in different configurations** -- `SP_SOG_MODE` 0 against 1 -- so the bit tracked
the configuration, which is exactly what `SyncMeasurement`'s own header says it
does. A discriminator has to be measured with everything but the source held
still, and this one was not.

## Refuted: coast unconditionally

The obvious fix, given the counts above, is to stop deciding and always coast.
It costs a separate-sync source its horizontal stability: the picture shakes
horizontally with the magnitude **graduated from the top of the frame to the
bottom**, which is the ADC PLL perturbed at the vertical interval and recovering
through the field. Removing the coast stops it.

**No register sees this.** Measured across 7/3, 9/9 and 3/3 on a separate-sync
source:

```
STATUS_SYNC_PROC_VTOTAL  311, steady, every setting
STATUS_SYNC_PROC_HTOTAL  2506 against a PLLAD_MD of 2506, unchanged
```

Both are COUNTS. A count is blind to jitter -- the lines still arrive, and still
average 2506 ADC clocks apart. A stability regression therefore scores as
perfectly healthy, which is how the change came to be committed before the panel
refuted it.

**The instrument for this is the picture and only the picture.** A gradient down
the frame separates a settling loop from steady noise, and nothing in a register
dump carries it.

## Adjacent, same shape, untaken

Two separation thresholds the separate-sync arm writes are wrong on a composite
source, which matters for exactly as long as a stale type can put one there:

- `SP_H_PULSE_IGNOR` **255** collapses the count to **97**. The real H pulse is
  about 7% of the line and 255 ADC samples is about 10% of it, so it swallows
  the sync. Walked 107 down to 0 the count reads 308 throughout, so pulse ignore
  does not recover the three missing lines either.
- `SP_DLT_REG` **0** gives a count that will not hold still. Any non-zero value
  measured -- 2, 8, 32, 128, 192 -- gives a steady 308.

The coast LENGTHS are not the residual: on a stable state 1/1, 3/3, 7/3 and 9/9
all read 308.

## How not to measure this

**Hold everything but the source still.** The `VSACT` model above died on this.

**Verify the baseline holds still before sweeping anything.** Two sweeps of the
V-active-detect registers were taken through a state whose count was already
wandering 97..187, and every point of both is worthless. A sweep needs a
stability check and an abort, not a reading at each point.

**One register at a time, restored before the next.** A `SP_DLT_REG` walk taken
with `SP_H_PULSE_IGNOR` left at 255 measures nothing but the 255.

**A register cannot close a question the picture opened.**
