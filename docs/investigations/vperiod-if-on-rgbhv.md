# Why `VPERIOD_IF` never completes a measurement on RGBHV

**Status:** CLOSED. **The discriminator is the SYNC ROUTE, not the video
standard and not RGBHV as such**: `VPERIOD_IF` is valid whenever the sync
separator is in the path and debris when it is not. Measured on one input, one
cable, one mode, with only the RISC PC's sync type moving --

| `vga`, 320x256@50 | separate | composite |
|---|---|---|
| `VPERIOD_IF` | 62, debris | **623** |
| `STATUS_IF_VT_BAD` | **1** in 582/582 | **0** in 53/53 |
| `SP_SOG_MODE` | 0 | 1 |

-- which confirms the separator hypothesis below on a stricter test than the
RGBS experiment it proposed, because that one moved the input port and the video
standard as well. `docs/sync-type-selection.md` carries the register detail.

**The experiment described at the end is no longer blocked the way it says.**
ModeServ changes the RISC PC's sync type live over TCP, so no CMOS change and no
reboot is involved, and the sync route can be moved without touching a cable.
The RGBS-through-the-Sync-port version remains the only way to separate the
PORT from the sync route, which nothing so far has needed. What the firmware does about it
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

## Measured: it works on component, and not on RGBHV

A Wii at 576i on the YPbPr input against the RISC PC on VGA, same build, minutes
apart:

| | Wii 576i, YPbPr | RISC PC, RGBHV |
|---|---|---|
| `VPERIOD_IF` | **624** | **12**, debris |
| `STATUS_IF_VT_OK` | **1** | 0 |
| `STATUS_IF_VT_BAD` | **0** | 1 |
| `STATUS_SYNC_PROC_VTOTAL` | 310 | 311 |
| `SP_SOG_MODE` | 1, csync | 0, separate |

624 is the 625-line PAL frame, so the measurement is not merely non-zero but
correct. **`VPERIOD_IF` is therefore not broken on this board**, and the fallback
experiment's question -- whether it ever works here -- is answered yes.

This is a **better** data point than the fallback anticipated, because YPbPr is a
direct analog path: the ADV7280 is not involved, so a decoded-and-re-encoded
signal is not the explanation. What differs from RGBHV is the sync route --
component carries sync on Y, so `ASW_01` is low and the separator is in the path
-- and the video standard. That is much closer to the preferred experiment below
than to the fallback.

**It does not yet separate sync route from standard.** Both changed together. The
RGBS experiment below still does that, and is still worth running.

## The experiment that would isolate the cause

Two options, and the first is much better than the second.

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

**That `VPERIOD_IF` works in the SD modes is now observed rather than inferred.**
The firmware keys exact equality on 522/524/526/622/624/626 for field parity, and
the measured 624 is one of them.

## Interlace is not measurable here, by any register tried

The chip has dedicated bits -- `STATUS_IF_INP_INT` and `STATUS_IF_INP_PRG` at
s0_04[6] and [7], with `STATUS_IF_INP_NTSC_INT` and `STATUS_IF_INP_PAL_INT`
naming the standard. They do not answer the question:

| state | `INP_INT` | `INP_PRG` | `PAL_INT` | `VT_OK` | `VPERIOD_IF` |
|---|---|---|---|---|---|
| Wii 576i, genuinely interlaced | 1 | 0 | 1 | 1 | 624 |
| RISC PC csync, genuinely **progressive** 311 | **1** | 0 | **1** | 1 | 623 |
| RISC PC separate sync | 0 | 0 | 0 | 0 | 54, debris |

**A 311-line progressive source is reported as PAL interlace**, bit-identical to
a real 576i one, and on separate sync every bit reads 0.

The cause is not a chip defect. `VPERIOD_IF` counts **half-lines**, and in half
lines a 311-line progressive 50 Hz source is 623 while 576i is 624 -- the same
signal to within one count, at the same line rate and the same field rate. What
separates them is the half-line offset between alternate fields, which nothing
reachable here resolves. Sampled 45 times on each, both are a single steady value
with no alternation, so field parity is not visible either.

**So interlace cannot be established from the registers on this board**, and
anything keying on it is keying on a guess. Note that both sources produce a good
picture under the same treatment, so this is a classification the pipeline has not
so far needed.

`INTERLACE_PROGRESSIVE_RECOGNIZE` at s0_04[7:6] is a second name for those same
two bits, which the one-name-per-field rule forbids.

## It survives every failure the sync processor has

Where `VPERIOD_IF` is live it is not merely a second opinion, it is unmoved by
what breaks the first one. Measured on the Wii while the sync processor was in
each of its failure modes, `VPERIOD_IF` read **624 with `STATUS_IF_VT_OK` 1 in
every sample**:

| `STATUS_SYNC_PROC_VTOTAL` | `Geometry::sourceState()` | `VPERIOD_IF` |
|---|---|---|
| 310, correct | `acquired` | 624 |
| 254 / 160 / 149 / 230, wandering | `absent` | 624 |
| 97, the no-lock value, held 40 s | `absent` | 624 |

So a source the engine calls absent is being measured correctly and continuously
by the input formatter, with a validity flag saying so. That is the witness the
coast lengths lack: `SP_PRE_COAST`/`SP_POST_COAST` corrupt the sync processor's
count and cannot reach this one, so the pair can in principle be steered to make
the two agree rather than held as a constant.
`two-owners-of-the-coast-lengths-double-the-count.md`.

**What is not established** is the arithmetic tying them together. The counts are
in different units -- `VPERIOD_IF` in half-lines of the frame, the sync processor
in lines of the field -- and the one comparison that would pin the relation, a
coast parked long enough to force the doubled count while both are read, cannot
be taken any more: with one owner a hand-written pair is overwritten within
1.5 s. Forcing it needs the engine held off, not a register write.

## What this costs the geometry engine

`VPERIOD_IF` measures the FRAME while `STATUS_SYNC_PROC_VTOTAL` measures the
FIELD, so the two disagreeing by a factor of two is what interlace looks like --
but it counts half-lines and so does not distinguish interlace by magnitude.
Being dead on separate sync means it supplies nothing at all there. Combined with
the status bits above, **no register on this board establishes interlace**.
`docs/video-source-acquisition.md`.

## What the scan-type measurement does with it, and why that still works

`VPERIOD_IF` carries the half line an interlaced field adds, and
`SourceMeasurement::scanType()` reads it against the line doubling the engine
holds -- doubling is what puts the count in half lines, so which parity means
interlaced inverts with it. The motion-adaptive path runs off nothing else.

**It is gated on `STATUS_IF_VT_OK`, which is what keeps the debris out.**
Measured on the two bench sources:

| source | `VPERIOD_IF` | `STATUS_IF_VT_OK` |
|---|---|---|
| RISC PC on `vga`, RGBHV, progressive | 0, 33, 57, 101, 112 across runs | 0 |
| Wii on `ypbpr`, PAL 576i | 624, every sample | 1 |

So the bit separates the two sources exactly, and the measurement never sees a
period from the RGBHV path at all. A count too short to be a vertical total
answers `ScanUnknown` behind that gate, and the caller leaves the deinterlacer
where it is.

**The parity generalises, and the doubling is what makes it do so.** Six states
on the RISC PC -- three modes either side of the doubling boundary, interlace
toggled on each -- and both Wii scan types agree with one rule.
`interlaced-source-measurement.md`.


## The test bus reading was taken through an undriven bus

`TEST_BUS_SEL` picks which block drives `DEBUG_IN_PIN`, and `/testbus` counts its
transitions over 25 ms from inside `loop()`. Swept on the bench RISC PC at
320x256@50 with only the sync type moving:

| `TEST_BUS_SEL` | separate | composite | |
|---|---|---|---|
| `0x00` input vsync | 2 | 4 | field rate on both |
| `0x02` output vsync | 2 | 2 | field rate on both |
| **`0x0a`** | **0** | **2** | **field rate on composite only** |
| `0x05`, `0x06`, `0x07`, `0x0e`, `0x0f`, `0x10`, `0x12` | 1300-3900 | 1300-4400 | line rate on both |

**Vertical sync reaches the chip on separate sync** -- selector `0x00` carries it
either way, which is the same conclusion `STATUS_SYNC_PROC_VSACT` and a correct
`STATUS_SYNC_PROC_VTOTAL` already supported. What changes is one bus. `0x0a` is
recorded in `framesync.h` as the selector the sync watcher and the HTotal search
use.

That is corroboration for the separator hypothesis rather than proof of it: it
shows a vertical signal that exists only with the separator in the path, and it
does not establish that the input formatter's vertical measurement reads that
particular bus. The next step is a sweep with `SP_TEST_MODULE` and `IF_TEST_SEL`
set, which `/testbus` takes as parameters.


## What is refuted, and what the test bus actually shows

**The retiming vertical window is not the cause.** `SP_RT_VS_ST` is 2 against
`SP_RT_VS_SP` 0, a window whose stop precedes its start, and both are static
constants never varied by source -- so it looked like the whole fault. Swept on
separate sync with the stop at 8, 64, 311 and 1000, every write read back, 1854
samples: `VPERIOD_IF` never moved off 136 and `STATUS_IF_VT_OK` never left 0.

**The earlier `0x0a` comparison was between two states of a bus nothing was
driving.** `SP_TEST_MODULE` reads 7, which drives nothing. Point it somewhere
live and the bus carries traffic -- `sp=5` gives 8522 transitions in 25 ms,
`sp=6` 664, `sp=4` 48. The same applies to selector `0x00`, read at the time as
"input vsync": it is the input formatter's test output, and `IF_TEST_SEL`
changes it completely, from 2 at `if=3` to 8325 at `if=4`. So the reading that
one bus stops carrying vertical sync on separate sync does not stand.

**Nor is any test-bus stage a scan-type detector.** Swept across a real
interlace change, three passes per configuration, every apparent difference sits
inside the pass-to-pass spread of a line-rate count. The one exception is
`sp=4`, `vs_act_det`, at 46-47 progressive against 48 interlaced -- one
transition out of 47, which more sampling could erase, and not something to
build on.

**The fault is narrower than "the IF gets no vertical sync".** Within one
settled state `VPERIOD_IF` is a rock-steady constant -- 136 in 371 of 371, in
every window, and unchanged across an interlace change. It takes a different
constant per acquisition episode. It is stuck, not noisy, which points at the
counter being held or reset rather than counting rubbish.
