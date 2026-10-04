# The green YPbPr boot is outside the register file

**THE TITLE CLAIM IS REFUTED, AND SO IS THE HC32 ATTRIBUTION.** The fault is on
the TV5725: detection's YPbPr branch claims the source without a preset load, so
`doPostPresetLoadSteps()` never runs. `/sc?#` cures it without reaching the HC32
at all, and selecting `rgbs` bounces `ADC_INPUT_SEL` without curing anything.
`the-ypbpr-detection-branch-skips-the-preset-load.md` is the current page.

This one is kept for the measurements that stand and for why they read the way
they did. The fifteen fields below are the sixteen the missing phases write;
they sit at reset defaults because nothing wrote them, which is why a write-back
of final values is negative in both directions — the setup is a sequence with
block resets in it, and a value cannot carry an edge.

A boot that lands on `ypbpr` emits solid green with faint vertical bars while
every TV5725 register reads correct.

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

**A write-back tests a STATE, and a transition is not one.** Writing a field to
the value a clean unit holds cannot reproduce something whose effect is in the
act of changing it, and `ADC_INPUT_SEL` is on record as clearing this class of
fault sometimes and causing it others. The round trip that cures the green
bounces it 0 to 1 and back, so the transition was the gap the write-back left.

Bounced on its own against a faulted unit, it cures nothing. The byte is being
rewritten by the engine's recovery ladder throughout -- `s5_02` read `0x5d` and
then `0x5b` between two writes a few seconds apart -- so what is excluded is the
bounce reaching the picture, not the register being quiet.

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
| `ADC_INPUT_SEL` bounced 0 -> 1 -> 0 | scaler | no |
| `/input?src=av` excursion, then back | HC32 and scaler | no, and `av` never acquired |
| `/input?src=vga` then `/input?src=ypbpr` | HC32 and scaler | **usually** -- see below |

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

## The frames move the switches, and no sequence of them is enough

Swept against a faulted boot, each frame sent on its own with nothing else
written. A flat field reads a high `cast` -- the spread between the brightest
and dimmest channel -- and a low `spread`; a real picture is the reverse.

| frame sent | emitted |
|---|---|
| `rgbs`, `rgsb`, `ypbpr` | dim field, `cast` 0.52, `spread` 8 |
| `vga`, `sv`, `av` | nothing at all |
| a clean picture, for scale | `cast` 0.13, `spread` 93 |

So the switches answer the frames -- routing the S-Video or composite pair takes
the output to black, and routing a component or RGB pair brings a field back --
and **no sequence of frames alone reaches a correct picture**. `/input` does. The
scaler therefore carries a second contribution that the frames leave stale.

## The boot selects an input differently from every other caller

`/input` reaches `applyInputSelection()`. The boot reaches
`applySavedInputSource()`, which is a partial copy of it:

| `applyInputSelection()` | `applySavedInputSource()` |
|---|---|
| sends the frame | sends the frame |
| `Tv5725::Adc::installReferenceSamplingClock()` | — |
| `inputFormatter.applyScan(BringUpDivider, …)` | — |
| `resetSyncProcessor()` | — |
| `sourceAbsence.selectionChanged()` | — |
| `applyInputRegisters(settings)` | `applyInputRegisters(settings)` |

The boot therefore takes its first measurement of the arriving source through
whatever divider the chip was left holding, rather than through a reference one.
The boot log reads that way: the early samples on a faulted boot are
`270 lines x 121.42 Hz` against a source at 60, and the console's
`input selected: …, reference divider 2506` line is only ever emitted on the
selection path.

Collapsing the two is what the conventions call for, and it is the candidate the
remaining evidence points at. It is not yet proven to be the green: `/sc?~`
resets the sync processor without curing it, so the reference clock and the
frame together are the part not yet tried.

## The cure needs an acquisition on the other input

Five boots, each walked through the candidates in turn:

| | |
|---|---|
| boots that came up faulted | 5 of 5 |
| cured by `/sc?~` | 0 of 5 |
| cured by the round trip | 5 of 5 |
| round trips needed | 1, 1, 1, 3, 1 |

The trial needing three spent the first two with `vga` reporting no rate at all,
and went clean on the attempt where `vga` acquired. **Across every trial the
round trip cured the fault whenever `vga` acquired and never when it did not**,
so what is unreliable is the acquisition rather than the cure.

That is the sharper statement, and it moves the question: a cure which needs the
other input to have genuinely acquired is not explained by any register the
round trip writes, because those are written either way. It is explained by the
part that decides whether a signal arrives at all.

A boot lands on a bright green field, a dim one or no output, with the source
untouched throughout -- a family of wrong analog states rather than one.

## The RGB-family frames move nothing, and `sv`/`av` do

A `/restart` onto `ypbpr` reproduced the fault as a FLAT NEUTRAL WHITE field --
a fourth variant beside the bright green, the dim one and the black output.
Scored inside the picture rather than over the whole frame, so the pillarbox
does not count: luma 236.1, standard deviation 5.3, and R, G and B equal to a
tenth of a level. The sync path was perfect throughout --
`STATUS_SYNC_PROC_VTOTAL` 260, the Wii's 480i count, with
`STATUS_SYNC_PROC_HTOTAL` 2200 against `PLLAD_MD` 2200.

**The scaler is locked to the source while the video under it is a flat field**,
which places the fault in what arrives at the ADC rather than in anything timing.

Sweeping every AV module frame on that state, with no register written:

| frame | luma | spread | verdict |
|---|---|---|---|
| `rgbs` 0x40 | 236.17 | 5.28 | unchanged |
| `rgsb` 0x50 | 236.14 | 5.29 | unchanged |
| `vga` 0x61 | 236.14 | 5.28 | unchanged |
| `ypbpr` 0x70 | 236.08 | 5.29 | unchanged |
| `sv` 0x10 | 0.00 | 0.00 | black |
| `av` 0x20 | 0.00 | 0.00 | black |

**The four RGB-family frames change the output by less than a tenth of a grey
level and the other two take it to black.** So the HC32 is listening -- the
switches answer `sv` and `av` -- and nothing in the RGB family moves the analog
path while the fault is in force. `vga` in particular should have routed a RISC
PC sending a test card and did not.

## The vendor steers detection with the saved input and restores no hardware

Read out of tag `1.3`, the vendor's own V1.3 for this board, which builds
against this toolchain unchanged -- 824288 bytes of flash against this fork's
800724, and 43808 bytes of globals against 53832.

`SeleInputSource` is loaded from the preferences file, and the two writes that
would put the hardware into the state it names sit directly beneath the load,
**commented out**:

```
SeleInputSource = (uint8_t)(f.read() - '0');

// GBS::SP_EXT_SYNC_SEL::write((uint8_t)(f.read() - '0'));
// GBS::ADC_INPUT_SEL::write((uint8_t)(f.read() - '0'));
```

What the value does reach is three gates inside `detectAndSwitchToActiveInput()`,
each of the form `SeleInputSource == S_VGA || SeleInputSource == S_RGBs`. So it
STEERS DETECTION and restores nothing. `S_YUV` is 3 and appears in no live gate,
so a saved YPbPr does not even steer.

And no AV module frame is sent at boot. Every send -- `Checksum_Sendmode()` for
the four RGB-family frames and `sender.send()` for `Ypbpr` -- is inside an OLED
menu action handler, and there is no web or serial route to any of them: on the
vendor firmware the input is changed by a person at the panel or the remote, and
by nothing else.

**So the boot this fault appears on does not exist on the vendor firmware.**
`applySavedInputSource()` and what became `VideoSourceSelector::restore()` write
`ADC_INPUT_SEL` and transmit the frame before anything has been measured, which
is the behaviour whose two register writes the vendor author commented out.
That does not say which part of the restore is wrong. It does say the comparison
has an answer, and that the answer is not "the vendor did this and it worked".

**The empirical half is not done and needs the bench.** Driving the vendor
firmware onto `ypbpr` takes the remote or the OLED menu, there being no route,
so whether a vendor boot with the Wii attached comes up clean is untested. The
reading above is source, not measurement.

## Open

Which switch state the HC32 holds is still not established, and the sweep above
narrows rather than settles it: the part answers two of the six frames and
ignores four, which is a state the frames cannot address rather than a state
they set wrongly.
