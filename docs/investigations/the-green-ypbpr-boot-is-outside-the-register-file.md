# The green YPbPr boot is outside the register file

A boot that lands on `ypbpr` emits solid green with faint vertical bars while
every TV5725 register reads correct. The fault is not expressed anywhere in the
chip's 1536 registers, and the HC32's analog switches are in its causal path.

This page is the evidence. The open defect is `known-issues.md`.

## The register file is excluded, in both directions

Two states exist on demand: green from a boot that lands on `ypbpr`, clean from
the same input after a round trip through `vga`. A full `snapdiff.py --save` of
each resolves to **fifteen fields** outside the geometry solve, every one of
them at a reset default in the green state.

A green-to-green control pair separates the solve from the rest. Two green
states taken either side of a detection pass differ **only** in the capture
window, the display window, both scales, the coast pair and the SOG level —
the set the geometry engine rewrites on every re-solve. None of the fifteen
appears in it.

Both directions of the obvious experiment are negative:

| written | onto | picture |
|---|---|---|
| the fifteen at their green values | a clean unit | stays clean, in colour |
| the fifteen at their clean values | a green unit | stays green |

So the fifteen are neither sufficient nor necessary for the green, and the
complete difference between a green state and a clean one, made identical,
leaves the picture where it was. **A register dump cannot reach this fault**,
which is why every earlier pass over one found nothing.

The fifteen are a separate defect and are described in `known-issues.md`.

## A signal is missing, and the registers do not explain it

`/testbus` counts transitions on each `TEST_BUS_SEL` over one window.
RD-5725-1.1 tabulates no meanings for the selector, so the reading is a
comparison rather than a name.

| `TEST_BUS_SEL` | green | green, registers made identical to clean | clean |
|---|---|---|---|
| 5, 6, 7 | 2351, 1415, 1600 | 2202, 1406, 1722 | 2535, 1397, 1666 |
| 14 | **0** | **0** | 3482 |
| 15 | 1110 | 706 | 3494 |
| 16 | **0** | **0** | 3521 |

Three selectors carry a line-rate signal whenever the picture is good and carry
nothing, or a fifth, when it is green. They stay dead with every register
identical to a clean unit's, so the difference is not downstream of anything the
registers control. They return to ~3500 at the moment the picture does.

Selectors 5, 6 and 7 — the sync front end — read the same in both states, so
sync and luma arrive in the green state. Something parallel to them does not.

## The analog switches are in the causal path

`ASW_01`-`ASW_04` decide what is actually connected to the ADC input the scaler
has selected. They live on the HC32, they appear in no register dump, and the
ESP cannot read them back: `ESP_RXD` is driven only by the CH340T, and the
HC32's `USART4` TX reaches J18 and SW2 but never the ESP.

`/avframe?src=<name>` sends the 7-byte frame and nothing else. Against a green
boot, sending `vga` and then `ypbpr`:

- the output goes from saturated green to a neutral field
- `ADC_INPUT_SEL` stays 0 throughout
- no register is written

That is the first measurement placing the fault outside the TV5725 rather than
merely failing to find it inside. `/input`, the OLED and the IR handler all move
the scaler in the same breath as the frame, so none of them can separate the two
halves of the input path.

## What the recoveries say

| action | reaches | clears the green |
|---|---|---|
| `/input?src=ypbpr`, the input already selected | HC32 and scaler | no |
| `/sc?~`, a full detection pass | scaler | no |
| the fifteen fields written to their clean values | scaler | no |
| `/avframe` `vga` then `ypbpr` | HC32 only | **partly** — green to neutral |
| `/input?src=vga` then `/input?src=ypbpr` | HC32 and scaler | **yes** |

The ESP re-sends the frame unconditionally on every selection — `InputYUV()`
calls `sendInputFrame()` before anything else — so a repeat selection that
changes nothing is the HC32 declining a command for the routing it already
holds, not the ESP skipping the send.

## Two models this refutes

**The fifteen missing static fields are the green.** They are written by
`doPostPresetLoadSteps()`, they are absent for a real and reproducible reason,
and they are not this fault. Both directions of the write-back are negative.

**`BOOT: reason='External System'` means the rails stayed up.** `ESP.getResetReason()`
reports that string on a true mains-and-USB power cycle on this board as well,
so it does not distinguish a power-on from an external reset here, and nothing
may be inferred from it about whether the HC32 restarted. The green also
survives a true power cycle, so it is not an artefact of resetting the ESP
alone.

## Open

Which switch state the HC32 holds is not established, and neither is why a boot
reaches it. `/avframe` makes both sweepable: the frames are enumerable and the
emitted frame is readable off the USB capture, so a sequence that restores the
routing without the scaler moving would name the state.

The neutral field the frames reach is not a correct picture either, and a
detection pass after them does not finish it. Only the full round trip does, so
the scaler carries a second contribution that the frames alone leave stale.
