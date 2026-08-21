# The RGBHV bypass trap

An RGBHV source of more than 535 lines enters bypass once and can never leave,
so it is never scaled. It still produces a picture — the cost is scaling, not
the image.

## The gate

Three branches in the sync watcher handle the RGBHV chain, and between them they
leave a hole:

| `gbs-control.ino` | condition | action |
|---|---|---|
|  | `sourceLines <= 535 && videoStandardInput == 15` | → enter **scaling** |
|  | `sourceLines <= 535 && videoStandardInput == 14` | → scaling setup |
|  | `sourceLines > 535 && videoStandardInput == 14` | → set mode **15**, apply **bypass** |

There is no branch for `sourceLines > 535 && videoStandardInput == 15`. So a
source above 535 lines takes  once, lands in mode 15, and from then on
 rejects it for being over 535 lines while  rejects it for already
being mode 15. Nothing moves it back.

The bench RISC PC at 800x600 is **VTOTAL 627**, so it traps on every boot. It is
deterministic, not intermittent — a reboot re-arms it rather than clearing it.

## Confirming it on a live unit

```sh
python3 tools/gbsc-pro-hwtest/geometry.py --host <ip>     # or read the registers directly
```

Trapped, with the source at 800x600:

```
GBS_PRESET_ID        0x22   = PresetBypassRGBHV
GBS_OPTION_SCALING_RGBHV 0
PLLAD_MD             1856          <- hardcoded by bypassModeSwitch_RGBHV()
VDS_ENABLE           0             <- expected: bypass does not use the VDS
HPERIOD_IF           garbage       <- expected: the IF is out of the path
SP_VTOTAL            627           <- sync processor locked and correct
```

Escape is a source mode change to 535 lines or fewer —  then fires. On the
bench that is any of the `modesweep.bas` modes below the gate; 320x256 (VTOTAL
311) recovers a stable `HPERIOD_IF` of 431 immediately.

## What it does and does not cost you

**It does not cost the picture.** In RGBHV bypass the source passes through to
the DACs, and an 800x600 passthrough is something the display accepts happily. A
register snapshot taken in bypass restores a working 800x600 image. Two
consequences follow:

- `VDS_ENABLE == 0` and an empty segment 2/3 are **not** evidence that nothing is
  reaching the encoder. In bypass the video path does not go through the VDS at
  all. Do not read an empty VDS as "no output".
- A blank screen while trapped is a *separate* fault — most likely the MS9288A
  having wedged, which only a power drop clears (and USB backfeeds the rails, so
  pull that too). See CLAUDE.md, "No HDMI with every register perfect".

**It does cost scaling**, and therefore every geometry control that depends on
it. `uopt->preferScalingRgbhv` being set is silently ignored: the option says
"scale RGBHV", the source is over the gate, and the trap wins.

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
