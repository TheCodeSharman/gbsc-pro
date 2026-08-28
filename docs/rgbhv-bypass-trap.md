# RGBHV, scaled or bypassed

**The line count decides nothing. The preference does.** `preferScalingRgbhv`
on — the default — scales an RGBHV source whatever its height; off puts it in
bypass. Both directions are covered by
`tools/gbsc-pro-hwtest/test_rgbhv_bypass.py`, which needs `--modeserv` to drive
the source across the range.

## The gate that used to decide, and why it is gone

Three branches in the sync watcher handled the RGBHV chain, and between them
they left a hole:

| condition | action |
|---|---|
| `sourceLines <= 535 && videoStandardInput == 15` | → enter **scaling** |
| `sourceLines <= 535 && videoStandardInput == 14` | → scaling setup |
| `sourceLines > 535 && videoStandardInput == 14` | → set mode **15**, apply **bypass** |

There was no branch for `sourceLines > 535 && videoStandardInput == 15`, so a
tall source took the third once, landed in mode 15, and from then on the first
rejected it for being over 535 lines while the third rejected it for already
being mode 15. Nothing moved it back, and a reboot re-armed it. The bench RISC PC
at 800x600 is **VTOTAL 627** and trapped on every boot.

**535 was not a property of the part.** Measured with the gate removed, that same
627-line source scales sharp and full screen:

| | trapped in bypass | scaled |
|---|---|---|
| `PLLAD_MD` | 1856, the switch's hardcoded value | 1124, measured |
| `STATUS_SYNC_PROC_HTOTAL` | 1856 | 1124, so the divider latched |
| `VDS_HSCALE` / `VDS_VSCALE` | 557 / 533, the last scaled load's | 594 / 549, a solve |
| `HPERIOD_IF` | 511, garbage — the IF is out of the path | 176, the value 627@60 is due |
| `GBS_OPTION_SCALING_RGBHV` | 0 | 1 |

Crossing 311 ↔ 627 four times never left the scaling path. The picture is
visibly finer than the 320x256 the bench usually runs, because it is magnified
less.

## Reading bypass on a live unit

```sh
python3 tools/gbsc-pro-hwtest/geometry.py --host <ip>     # or read the registers directly
```

In bypass, with the source at 800x600 and the preference off:

```
GBS_PRESET_ID        0x22   = PresetBypassRGBHV
GBS_OPTION_SCALING_RGBHV 0
PLLAD_MD             1856          <- hardcoded by bypassModeSwitch_RGBHV()
VDS_ENABLE           0             <- expected: bypass does not use the VDS
HPERIOD_IF           garbage       <- expected: the IF is out of the path
SP_VTOTAL            627           <- sync processor locked and correct
```

## What bypass does and does not cost you

**It does not cost the picture.** In RGBHV bypass the source passes through to
the DACs, and an 800x600 passthrough is something the display accepts happily. A
register snapshot taken in bypass restores a working 800x600 image. Two
consequences follow:

- `VDS_ENABLE == 0` and an empty segment 2/3 are **not** evidence that nothing is
  reaching the encoder. In bypass the video path does not go through the VDS at
  all. Do not read an empty VDS as "no output".
- A blank screen in bypass is a *separate* fault — most likely the MS9288A
  having wedged, which only a power drop clears (and USB backfeeds the rails, so
  pull that too). See CLAUDE.md, "No HDMI with every register perfect".

**It does cost scaling**, and therefore every geometry control that depends on
it — which is why it is now a choice rather than something a line count imposes.

## What bypass makes unreadable

Bypass takes the IF and the VDS out of the path, so every measurement derived
from either stops meaning anything. None of them announce it, and three read as
faults in their own right.

**`HPERIOD_IF` is noisy in bypass, not a stable zero.** Measured on an 800x600
source in bypass: 255, 511, 511, 275, 258, 511 across six reads, while the sync
processor stayed perfect beside it (`STATUS_SYNC_PROC_VTOTAL` 627 rock steady,
`HTOTAL` tracking `PLLAD_MD`, PLL locked). That is the exact signature of the
railing fault, and prescribing the railing recovery for it is wasted work — the
IF simply is not in the path. Establish whether you are in bypass *before*
reading anything into `HPERIOD_IF`.

**`/geometry` reports `lineRateHz: 0`** for the same reason, so the engine has
no field rate and the framing values it reports are not a solve.

**`VDS_HSCALE` and `VDS_VSCALE` keep whatever the last scaled load left**, and
bypass never clears them. They will show plausible scaling values — 636 and 475
on a unit that was unambiguously in bypass — so **non-unity scale registers are
not evidence that the VDS is in the path**. Read `DAC_RGBS_ADC2DAC` and
`OUT_SYNC_SEL` instead: both are 1 in bypass and 0 on the scaling path.

**The Info screen's frame rate is wrong in bypass.** `getOutputFrameRate()`
selects the VDS test bus (`TEST_BUS_SEL = 2`) and times the vsync pulse on
`DEBUG_IN_PIN`. In bypass the VDS is not generating the output timing, so it
measures an idle bus: 39 Hz against a real 60. It checks the result against a
47..86 Hz plausibility band, retries once, and **returns the out-of-band value
anyway**, so a number it has already judged impossible is displayed as fact.

## Not to be confused with

A **corrupt scaling preset** looks different and is a distinct failure:

```
GBS_PRESET_ID        0x15    SCALING_RGBHV 1     <- claims to be scaling
VDS_HSCALE           1023    <- railed at the 10-bit maximum
PLLAD_MD             2553    VDS_HSYNC_RST 1444
SP_VTOTAL            97      <- nonsense; corrects when geometry is restored
```

On screen that is diagonal shear with a green/magenta noise band — the scaler
failing to finish reading each line from memory in time. `/uc?3` does **not**
repair it; forcing a preset load does not rewrite a corrupt one. Restoring a
known-good snapshot does:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore snapshots/<known-good>.dump.json --segments 1,3,4,5 --repeat 2
```

Restore **segment 2 as well as 1,3,4,5**. Segment 2 holds the deinterlacer and
MADPT block; restoring the IF and VDS while leaving a stale segment 2 produces an
inconsistent set and a worse picture than you started with.

## Related

- [tv5725-chip.md](tv5725-chip.md) — what `HPERIOD_IF` measures, and why bypass
  makes it meaningless
- `bypassModeSwitch_RGBHV()` in `gbs-control.ino` — the writes the mode applies
