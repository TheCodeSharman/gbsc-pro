# Leaving bypass needs a count the divider cannot give

Reproduced on the bench, 2026-09-13, by accident: drive the RISC PC to 1920x1080
on `vga`, then back to 320x256. The unit enters bypass on the way up and **never
leaves it on the way down**.

## What it looks like

The console, for as long as you care to watch:

```
evt,31292746,rgbhv-leave-bypass,251,15
evt,31293375,rgbhv-leave-bypass,250,15
evt,31293461,rgbhv-leave-bypass,255,15
evt,31293505,rgbhv-leave-bypass,247,15
...
```

about nine times a second, with the line count wandering 234..267 and the
standard byte stuck at 15.

Registers at the same moment:

| | reads | should read |
|---|---|---|
| `PLLAD_MD` | 2039 | 2208 |
| `STATUS_SYNC_PROC_VTOTAL` | 239..267, never still | 311 |
| `STATUS_MISC_PLLAD_LOCK` | 0 | 1 |
| `OUT_SYNC_SEL` | 1 | 0 |

## The deadlock

**The leave-bypass arm is not gated out — it runs.** It fires on every pass and
gets nowhere, which is a different fault from the one
`the-rgbhv-ladder-is-the-only-one-on-that-path.md` describes, and it is worth
keeping the two apart:

- the arm needs a **settled line count** before it will act;
- the count comes from the sync processor, which counts in ADC clocks;
- the ADC PLL is still divided for the mode being LEFT — 2039 suits a 67.5 kHz
  line, the source is now 15.6 kHz — so it does not lock and the count wanders;
- nothing re-derives the divider, because that is what the solve does and the
  solve is what the arm is trying to reach.

**The divider is an input to the measurement that is supposed to replace it.**
That is the shape of the fault, and it is the same circularity as
`docs/sync-type-selection.md`'s: a decision taken through state that only reads
correctly once the decision is already right.

## What does and does not clear it

| tried | result |
|---|---|
| waiting 50 s with the source settled | no change |
| `/sc?~` | **no change** — it does not recover this |
| `/input?src=vga` | no change |
| `/sampleclock?md=2208&os=4` | **clears it at once** — PLL locks, `VTOTAL` settles to 311, the engine acquires at 15625 and the picture comes back |

**`/sc?~` failing here is the notable half.** `CLAUDE.md` carries it as the
recovery for a stuck divider, and that entry is about a divider stuck on the
scaling path. A divider stuck while the output is in BYPASS is not the same
state and the same recovery does not reach it.

`/sampleclock` works because it writes the whole ADC clock group through the
call the bypass switch makes and restarts the PLL afterwards — it supplies the
divider the solve would have chosen, from outside the loop that cannot reach it.
It is behind `GBS_DEBUG`, so it is not a recovery a user has.

## Why it matters to the plan

A measurement-driven escalation has to be able to run when the measurement is
the thing that is broken. The reference sampling clock exists for exactly this —
`SourceMeasurement::applyReferenceSampling()` puts a known divider on the part
so a count can be taken through something other than the last mode's clock — and
the leave-bypass arm does not use it.

**The first reading taken after a source event must be taken through a reference
divider, not through the divider the previous mode left.** That is already the
rule on the scaling path. Bypass is the path it has not reached.

## What is actually left stale, measured with the full solve set

The first pass at this compared the output registers and found them identical,
which left the HDMI encoder as the only suspect. **That conclusion was an
artefact of an incomplete set.** Re-run with `SamplingLog`'s widened solve line,
the return from 1920x1080 to 320x256 leaves this:

| register | stuck at | due | what it is |
|---|---|---|---|
| `PLL648_CONTROL_01` | **53** (`0x35`) | 117 (`0x75`) | the HD-bypass display clock seed, not the external generator |
| `IF_HBIN_SP` | **2** | 272 | the line doubler's own line reset, which PLACES the picture |
| `IF_HB_SP2` / `IF_HB_ST2` | 205 / 1160 | 129 / 1084 | the capture window, still sized for the mode that was left |
| `VDS_VSCALE` | 266 | 533 | half |
| `PLLAD_MD` | 1886 | 2208 | the sampling divider |
| `OUT_SYNC_SEL` | 1 | 0 | still routed to the HD bypass channel |

Every one is a register the engine owns, and two of them move the picture by
themselves:

- **A different clock seed is a different output pixel clock**, so the sink is
  shown a different MODE while `VDS_HSYNC_RST` and the display window read
  exactly the same. Nothing downstream has to misbehave for the picture to be
  re-framed.
- **`IF_HBIN_SP` places the picture horizontally on a line-doubled source**,
  which the bench source is. 2 against 272 is not a subtle difference.

`/geometry` reports `state: absent` and a line rate of 85882 throughout, so the
engine knows it has not solved. It simply has no route back.

**The output sync pulses are NOT the cause here**, and it is worth saying so
because they are the usual one: `VDS_HS_ST` and `VDS_HS_SP` held 0 and 32 across
the whole excursion. The gap between `VDS_HS_SP` and `VDS_DIS_HB_SP` is the back
porch the sink counts to find active video, so moving either does pan the
picture -- it just did not happen here.

**Recovered by `/sampleclock?md=2208&os=4`**, after which every value in the
table above is back where it belongs, because the solve that finally runs writes
all of them.

## The claim this refutes, and the one it does not

`the-encoder-reframes-the-output.md` stands: it probed by INTERVENTION, moving
the output window 60 px by hand, confirming the registers held the new value at
the moment of the photograph, and observing that the bar did not move. That is
evidence about a static difference between two output modes.

What does not survive is the separate round-trip claim -- *"the raster registers
identical either side, therefore the encoder re-acquires"*. The raster registers
are not a sufficient set. `PLL648_CONTROL_01` and `IF_HBIN_SP` differ across a
bypass round trip while every raster register agrees, and either is enough to
move the picture.

**An encoder explanation needs an intervention, not an absence of difference.**
Nothing on this board can configure the MS9288A, so attributing an effect to it
closes an investigation rather than advancing one -- and the effect here was in
registers we own the whole way.

