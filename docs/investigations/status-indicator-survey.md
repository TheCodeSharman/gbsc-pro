# What the segment 0 status indicators actually report

RD-5725-1.1 offers a stable indicator, an unstable indicator and a no-sync
indicator per axis, eight interrupt latches, two PLL lock bits and four
output-position flags. Three of them have been caught lying, each time by
accident mid-diagnosis. This is all of them, sampled together, across six states
whose verdict is known independently.

`tools/gbsc-pro-hwtest/status_bits.py --host <ip>` reproduces it. Segment 0 is
read whole, one request per sample, so every bit in a row comes from the same
instant.

Source: RiscPC 320x256@50, VTOTAL 311, separate sync.

| | locked | MD 1600 | MD 700 | MD zeroed | coast zeroed | trapped |
|---|---|---|---|---|---|---|
| `STATUS_IF_HT_OK` | 1 | 1 | 1 | 1 | 1 | 1 |
| `STATUS_IF_VT_OK` | 0 | 0 | 0 | 0 | 0 | 0 |
| `STATUS_IF_HT_BAD` | 0 | 0 | 0 | 0 | 0 | 0 |
| `STATUS_IF_VT_BAD` | 1 | 1 | 1 | 1 | 1 | 1 |
| `STATUS_IF_NO_SYNC` | 0 | 0 | 0 | 0 | 0 | 0 |
| `STATUS_INT_*`, all eight | 0 | 0 | 0 | 0 | 0 | 0 |
| `STATUS_SYNC_PROC_HSACT`/`VSACT` | 1 | 1 | 1 | 1 | 1 | 1 |
| `STATUS_SYNC_PROC_HSPOL`/`VSPOL` | 1 | 1 | 1 | 1 | 1 | 1 |
| `STATUS_MISC_PLL648_LOCK` | 1 | 1 | 1 | 1 | 1 | 1 |
| **`STATUS_MISC_PLLAD_LOCK`** | **1** | **0** | **0** | **unstable** | 1 | **0** |
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 311 | 155..160 | 311 | 311 | 173..194 |
| `STATUS_SYNC_PROC_HTOTAL` | 2250 | 1599..1601 | 1399..1403 | 2250 | 2250 | 711..2300 |
| `STATUS_SYNC_PROC_HLOW_LEN` | 159 | 113..114 | 1..81 | 159 | 159 | 586..2300 |
| `HPERIOD_IF` | 430..431 | 430..431 | 430..431 | 430..431 | 430..431 | 430..431 |
| `VPERIOD_IF` | 53 | 53 | 53 | 53 | 53 | 112..186 |

"MD 1600" and "MD 700" are `PLLAD_MD` written and latched by hand with
automation frozen; 700 is below the divider at which the PLL counts one line per
two sent. "Trapped" is the real fault a forced 1080p raster change produces --
see `field-rate-measured-downstream.md`.

## Seventeen of the twenty-six bits never move

Every IF stable/unstable indicator, all eight interrupt latches, both sync
processor active bits, both polarity bits and the display PLL's lock bit read
the same value in all six states, one of which is a fault the picture does not
survive. **None of them can discriminate this fault**, so nothing may branch on
one without first showing it moves for the condition being asked about.

Constant is not the same as broken, and the four IF bits are the case in point:
`HT_OK` 1 with `HT_BAD` 0, `VT_OK` 0 with `VT_BAD` 1, unchanged throughout.
`tv5725-chip.md` establishes that is **honest** -- the horizontal IF measurement
is good and the vertical one is invalid on RGBHV, in 22 of 22 archive snapshots
including confirmed-clean pictures. The bits report a condition that is constant
on this bench, and they report it correctly. What they cannot do is tell a
locked source from a trapped one.

The separate trap stands: `STATUS_IF_HT_OK` reads 1 with `HPERIOD_IF` railed, so
it is not a validity test for the value beside it.

**The interrupt latches are not merely unread, they are never set.**
`STATUS_INT_INP_SW` stays 0 across a real preset load and mode change. So the
mode-switch bit cannot be used as it stands; something has to enable the
interrupt first. The eight resets at `s0_58` map to the eight status bits at
`s0_0f` by position -- `INT_CONTROL_RST_SOGBAD` bit 0 against `SOG_BAD` bit 0,
`SOGSWITCH` 1 against `SOG_SW` 1, `NOHSYNC` 4 against `INP_NO_SYNC` 4 -- which
names the five that RD-5725-1.1 leaves as `INT_RST_2/3/5/6/7`.

## The one indicator that tracks the hardware

`STATUS_MISC_PLLAD_LOCK` reads 1 in every state where the ADC PLL is locked and
0 in every state where it is not, including the real trap. Nothing else on the
chip reports that, and a correct line count does not imply it: at `PLLAD_MD`
1600 the sync processor counts 311 perfectly while the lock bit stays 0.

Two conditions on using it.

**It depends on Mode Detect.** With `s1 0x60..0x83` zeroed it goes unstable even
though the divider is the known-good 2250 and `SP_VTOTAL`/`SP_HTOTAL` read
perfectly. So anything keyed on it must not run while Mode Detect is
unconfigured, or it will read a false 0 and correct a divider that is right.
Zeroing the sync processor's coast registers, by contrast, leaves it alone.

**It needs debouncing.** Sampled every 250 ms it holds a clean 1 while locked;
sampled every 50 ms it wobbles 0..1 in that same state. Require a run of
agreeing reads rather than trusting one.

## Two things this does not show

`STATUS_MISC_PLL648_LOCK` never read 0, so there is no evidence yet that it can
report a fault -- though the trap here is the ADC PLL, so this is not a test of
it. It remains the only measurement of whether the display output clock is
running, against `PLL648_CONTROL_01 == 0x75`, which is a sentinel the firmware
wrote rather than anything it observed.

Zeroing `SP_PRE_COAST`, `SP_POST_COAST` and `SP_DLT_REG` did **not** reproduce
the recorded reading of `STATUS_SYNC_PROC_HTOTAL` 2400 from an unconfigured sync
processor: it stayed a clean 2250. Either more than those three are involved, or
that reading needed a different starting state.
