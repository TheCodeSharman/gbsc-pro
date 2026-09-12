# RGBHV, scaled or bypassed

**THE MEASUREMENT DECIDES.** `SourceMeasurement::bypassSuitsCount()` passes a
source through when the line doubler is not needed for it and its line rate
reaches the sink — 640x480 and up, which is everything a sink taking HDMI is
required to accept — and scales everything below. The sync watcher's steering
reads it in both directions, so a source that changes mode across the boundary
crosses on its own.

**Scaling is what costs the picture, which is why the boundary sits there.** The
capture's write limit bounds a line at about 1024 IF units however it is placed,
so the sampling divider has to fall as the line rate rises: a VESA-class source
is softer scaled than handed over untouched. `capture-limits.md`.

`preferScalingRgbhv` is the user's override, defaulting off, and no longer the
decision. A global boolean cannot express a per-source choice and it is due to
be replaced by one stored against the `SourceKey` the framing uses.
`tools/gbsc-pro-hwtest/test_rgbhv_bypass.py` covers both directions and needs
`--modeserv` to drive the source across the range.

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

**`/geometry` reports the rate held from BEFORE bypass, not `0`.** Bypass
measures nothing and `VideoPath::enterBypass()` keeps the last measurement
deliberately, so the field the engine reports names the mode bypass was entered
on. Measured: `lineRateHz: 31690` with the source counting 311 lines at 50 Hz,
thirty seconds after it changed mode. The framing values beside it are the
previous mode's solve, not this source's.

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

## A source that slows while bypassed

Bypass hands the source's own timing to the encoder, so it works only where the
display can show that timing. `SourceMeasurement::BypassMinLineRateHz` is the
floor, bracketed by measurement on the bench panel rather than taken from the
VGA standard — 26650 Hz locks and 21780 Hz gives no signal.

**The question has to be re-asked while bypassed, and it cannot be asked of the
held rate.** A mode change does not re-enter bypass, so nothing else re-asks;
and nothing measures in bypass, so the held rate goes on naming the mode bypass
was entered on however far the source slows. Asked that way the answer never
changes, the branch that would leave never fires, and the panel stays blank for
ever.

Reproduced in about thirty seconds, `preferScalingRgbhv` off:

| | |
|---|---|
| source at 640x480@60 | 524 lines, 31690 Hz held, bypass entered, picture fine |
| source to 320x256@50 | 311 lines counted, `DAC_RGBS_ADC2DAC` and `OUT_SYNC_SEL` still 1 |
| held rate 30 s later | **31690 Hz**, the mode before it |
| the panel | *Retro Scaler — No signal* |

Nothing in a register dump distinguishes this from a bypass that is working: the
sync processor counts the source correctly throughout, the DACs stay powered,
and the divider is the switch's own 1856 either way. The whole difference is a
number in ESP RAM that stopped describing the source.

So the re-ask asks the COUNT, which is live — `countCanBypass()`, against the
held FIELD rate rather than the held line rate, because a mode change moves the
count and usually leaves the field rate where it was. A source that changes both
at once is the one case it cannot see, and it costs no vsync spin to be right
about the rest. The count is confirmed still by `countHeldStill()` before it is
acted on, because leaving costs a preset load and a source mid-change counts
anything at all.

`test_a_bypassed_source_that_slows_leaves_bypass_on_its_own` is the
reproduction, and it needs `--modeserv` because only a source mode change
reaches it.

## Not to be confused with

A **corrupt scaling preset** looks different and is a distinct failure:

```
GBS_PRESET_ID        0x15                        <- claims to be scaling
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
