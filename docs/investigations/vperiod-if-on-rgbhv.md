# Why `VPERIOD_IF` never completes a measurement on RGBHV

**Status:** structural and reproducible; the cause is a hypothesis, and the
experiment that would settle it has not been run. What the firmware does about it
— substitute `STATUS_SYNC_PROC_VTOTAL`, treat a non-zero value as debris — is in
[`tv5725-chip.md`](../tv5725-chip.md).

## What is ruled out

**`IF_VS_SEL` is not the cause.** It is the obvious candidate: s1 `0x00` bit 5,
*"choose the periodical or virtual vertical timing; 0: VCR mode timing
generation, 1: normal mode timing generation"*, and it reads 0 here.


- `gbs-control.ino` writes 0 for *every* preset whose ID is not `0x06`/`0x16`
  ( writes 1 for those two), so it does not discriminate RGBHV from the SD
  modes where the measurement works. This unit runs `GBS_PRESET_ID` `0x01`.
- Setting it to 1 changes nothing. Frozen, written and **read back as `0x22` to
  confirm the write landed**, then sampled for 3 s: `VPERIOD_IF` stayed 129,
  `VT_OK` 0, `VT_BAD` 1. Adding a Mode Detect reset on top gave 0, exactly as it
  does with the bit at its normal value.

`IF_SEL_ADC_SYNC` (s1 `0x28` bit 2) is likewise written once at  for all
presets and reads 1, so it does not discriminate either.

**VSync does reach the chip**, so the obvious explanation is wrong. Measured at
the same time: `STATUS_SYNC_PROC_VSACT` (s0 `0x16` bit 3) = 1 and `VSPOL` = 1,
with `STATUS_16` reading `0x0f`, and the sync processor counts 311 lines off it.
The analog path — and therefore the HC32F460's `ASW_01`-`ASW_04` routing, which no
register dump can see — is delivering vertical sync correctly. The failure is
**internal to the TV5725**, between a sync processor that has vertical timing and
an input formatter that never completes a vertical measurement.

**What is not established** is why. The surviving hypothesis is that the IF takes
its vertical timing from the **sync separator** — the block that extracts vsync
from composite sync or sync-on-green — rather than from the external VSync pin
the sync processor reads. RGBHV is the only input on this board with genuinely
separate sync: it is the only source that raises `ASW_01`, which per the schematic
selects the dedicated HSync pin over sync-on-green (`ASW_01 = 0, HS_IN = SOGIN`),
while every other input carries sync embedded in the video. If the IF is fed from
the separator, a separate-sync source leaves its vertical input with nothing to
extract, while the sync processor stays happy — which is exactly the asymmetry
observed. This is a hypothesis, not a finding.


## The surviving hypothesis

**VSync does reach the chip**, so the obvious explanation is wrong.
`STATUS_SYNC_PROC_VSACT` (s0 `0x16` bit 3) = 1 and `VSPOL` = 1, with `STATUS_16`
reading `0x0f`, and the sync processor counts 311 lines off it. The analog path —
and therefore the HC32F460's `ASW_01`-`ASW_04` routing, which no register dump can
see — delivers vertical sync correctly. The failure is **internal to the TV5725**,
between a sync processor that has vertical timing and an input formatter that
never completes a vertical measurement.

The hypothesis is that the IF takes its vertical timing from the **sync
separator** — the block that extracts vsync from composite sync or sync-on-green —
rather than from the external VSync pin the sync processor reads.

RGBHV is the only input on this board with genuinely separate sync: it is the only
source that raises `ASW_01`, which per the schematic selects the dedicated HSync
pin over sync-on-green (`ASW_01 = 0, HS_IN = SOGIN`), while every other input
carries sync embedded in the video. If the IF is fed from the separator, a
separate-sync source leaves its vertical input with nothing to extract while the
sync processor stays happy — which is exactly the asymmetry observed.

## The experiment that would settle it

Not yet run. Two options, and the first is much better than the second.

**Preferred — RGBS through the dedicated Sync port.** The board has separate
`R`/`G`/`B` and a `Sync` input, and the HC32 command set includes `0x4n` RGBs and
`0x5n` RGsB alongside `0x6n` VGA. So the same RISC PC at the same 320×256 can be
fed as RGBS: **RGB stays analog into the TV5725's ADC**, never touching the
ADV7280, so picture quality is unchanged and the only variable is the sync route
— from the dedicated HSync pin (`ASW_01` raised, VGA) to the separator path
(`ASW_01 = 0, HS_IN = SOGIN`).

Two things it needs:

- **csync into the Sync port.** The RISC PC emits separate H and V, so either set
  it to composite sync (`*Status Sync` to read the current setting, `*Configure
  Sync` to change it, then reboot — it is a CMOS setting, and MDF cannot do this:
  `sync_pol` is polarity only, bit 0 inverts HSync and bit 1 inverts VSync), or
  combine H and V externally with an XOR or LM1881-type circuit.
- **Select `RGBs` on the OLED** so the HC32 actually re-routes. Changing the cable
  without changing the input selection changes nothing — two muxes in series, and
  only the TV5725's is visible to you.

A free second data point: feeding **HSync alone** into the Sync port should, if
the separator hypothesis holds, reproduce today's exact signature — `HPERIOD_IF`
good, `VPERIOD_IF` dead — on a completely different input path.

**Fallback — an SD composite source** (a Wii, for instance). Weaker, and it tests
a different question: whether `VPERIOD_IF` *ever* works on this board, rather than
whether sync type is the cause. Composite enters via the **ADV7280**, so the
TV5725 sees a decoded-and-re-encoded signal rather than the source's own, and the
video standard changes as well as the sync arrangement. Expect ~523 on NTSC 480i.
Worth doing only if the RGBS route is unavailable: a live `VPERIOD_IF` localises
the fault to the RGBHV path, and a dead one means the hypothesis above is wrong
and the problem is broader than sync type.

**What to capture, either way.** The chip reports what actually arrived, which
matters because csync can land on pins you did not intend — a nonsense
`SP_VTOTAL` is the detector for that, since it is a trustworthy 311 today.

```sh
H=192.168.88.108
rd(){ curl -s -m 6 "http://$H/getreg?s=$1&r=$2" | grep -o '0x[0-9a-f]*"}' | tr -d '"}'; }
r0=$(rd 0 0x00); r5=$(rd 0 0x05); r6=$(rd 0 0x06); r7=$(rd 0 0x07); r8=$(rd 0 0x08)
rb=$(rd 0 0x1b); rc=$(rd 0 0x1c); r16=$(rd 0 0x16); p=$(rd 1 0x2b)
echo "VPERIOD_IF=$(( ((r7>>1)|(r8<<7)) & 0x7FF ))  HPERIOD_IF=$(( (r6|(r7<<8)) & 0x1FF ))"
echo "SP_VTOTAL=$(( (rb|(rc<<8)) & 0x7FF ))  VT_OK=$(( r0&1 ))  VT_BAD=$(( (r5>>3)&1 ))"
echo "VSACT=$(( (r16>>3)&1 ))  HSACT=$(( (r16>>1)&1 ))  presetID=$(printf 0x%02x $((p & 0x7F)))"
```

Healthy would be `VPERIOD_IF` ≈ 311 (or ≈523 on NTSC composite), `VT_OK` 1,
`VT_BAD` 0 — against today's 0 / 0 / 1.

Note what is and is not measured here: everything above is recorded on the
**invalid** side only. That `VPERIOD_IF` works in the SD and HD modes is
*inferred* from the firmware keying exact equality on 522/524/526/622/624/626 for
field parity, which could not work otherwise — it has never been observed on this
bench, because this bench has only ever had an RGBHV source.

