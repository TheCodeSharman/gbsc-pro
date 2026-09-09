# The first probe after an absent input answers a question about the last one

A handover recorded a sync-type latch with a reproduction: select `ypbpr`, come
back to `vga`, and the unit sits on `SP_SOG_MODE` 1 with `SP_EXT_SYNC_SEL` 1 and
`SP_VTOTAL` 97 -- a separate-sync source counted through the composite path, with
no route out because the count never moves to arm a re-probe.

**It does not reproduce.** Five round trips on `787d63bfd`, every one recovered:

| away leg | dwell | outcome |
|---|---|---|
| `ypbpr`, Wii powered | 25 s | recovered 6 s |
| `rgbs`, nothing attached | 40 s | recovered 7 s |
| `ypbpr`, Wii unplugged | 40 s | recovered 5 s |
| `ypbpr`, Wii unplugged | 8 s | recovered, probe right first time |
| `ypbpr`, Wii unplugged | 90 s | recovered, probe right first time |

## What is real, and it is one retry away from the latch

On a sixth run the first probe on the return leg answered **wrong**:

    92.55  own V sync: no after 2ms
    92.60  sync type: 3/3 field rate probes plausible, own V sync no -> csync
    94.60  own V sync: yes after 24ms
    97.88  own V sync: yes after 416ms
    100.95 own V sync: yes after 2ms
    106.16 source moved: interrupt (311 lines, solved 311)

The source is the RISC PC on `vga`, separate sync, and the probe classified it as
composite. What recovered it is the latched-disturbance re-arm firing three more
probes. **Remove those and the state is exactly what the handover recorded**, so
the retry loop is the whole of the margin -- which is why
`docs/input-acquisition.md` records at step 3 that consuming the latch on a
completed solve leaves the unit on the csync path.

## Why it is intermittent: the separator level is CYCLING, not ratcheting

The obvious explanation is that a long absent excursion walks the sync separator
somewhere unfavourable and the probe then answers through it. That is testable by
excursion length, and **the test refutes it**: 8 s answered right, 90 s answered
wrong, 90 s again answered right.

What the second long run shows instead is that `ADC_SOGCTRL` does not ratchet
monotonically on an absent source. Sampled every ten seconds:

    5  6  6  2  3  3  3  4

It walks down, resets to 2, and walks again -- the `% 150` recovery block
re-acquiring the level every few seconds, for as long as nothing is there. So
there is no stable level for an incoming source to be probed against, and where
in that cycle the input change lands is what decides the probe's answer. On a
good source the level re-acquires to 12.

**So the excursion's LENGTH is not the variable and dwell-based reproduction
attempts will keep disagreeing with each other.** What would settle it is
arriving at a chosen point in the cycle rather than a chosen time.

## What it says about the fix

The probe is not missing. It runs, promptly, on the return leg -- 2 ms after the
input change. What it lacks is a settled sync path to run against: the ladder is
mid-cycle on the separator, and the probe has no knowledge of that because the
two have no relationship. It answers a question about the state the previous
input left.

That is a coordination fault rather than a missing call, so a `VideoPath` entry
point for an input change is right for a different reason than the handover gave.
Not *nothing forgets the sync type* -- something does, eventually, via the
re-arm. It is that **an input change should probe from a known state instead of
racing an acquisition loop that does not know it happened.**

`docs/input-acquisition.md` is where that lands: the layer that owns both the
ladder and the measurement can order them, and neither can today.

## Bench notes this produced

- **`/input?src=rgbs` is not an absent source while the Wii is powered.** It came
  back `state: acquired`, 310 lines x 50.24 Hz, `ADC_INPUT_SEL` 0 -- the Wii's own
  signature, on an input with nothing plugged into it. Selecting an input the
  HC32 routes elsewhere does not disconnect what the ADC is already looking at,
  so the absent-source reproduction needs the console unplugged or powered down.
- **The absent baseline**, for a step that has to withhold recovery:
  `state: absent`, `present: false`, `STATUS_SYNC_PROC_VTOTAL` 0,
  `DAC_RGBS_PWDNZ` 0, and no `No Signal Out` inside 90 s.
- The engine's own verdict was correct throughout. `state` tracked the source on
  every leg, including calling an unplugged input absent while the sync processor
  still produced counts.
