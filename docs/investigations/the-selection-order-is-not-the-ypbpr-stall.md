# The selection order is not the ypbpr stall

**REFUTED ON THE BENCH, BOTH WAYS. Do not reorder `VideoSourceSelector::apply()`
to fix an acquisition.** The order looks wrong on the page and measures right.

`apply()` runs the sequence as:

```
sendFrame -> selectionChanged -> installReferenceSamplingClock -> applyBringUpScan
          -> resetSyncProcessor -> applyRegisters
```

`installReferenceSamplingClock()` calls `Adc::applySampleRate()`, which ends in
`restartPll()`. `applyRegisters()` is what writes `ADC_INPUT_SEL`, the
sync-on-green enable and `SP_EXT_SYNC_SEL`. So the ADC PLL is restarted while
the ADC is still looking at the outgoing input, and on sync on green before the
separator its reference arrives through is switched on — which reads as an
obvious ordering fault, and is the shape the recovery ladder's `Reconfigure` act
appears to repair ten seconds later.

## Both reorderings measure worse

Twenty-leg runs with `acquisition_legs.py`, 800x600@60 on `vga` as the
predecessor, Wii in 480i, scored to `sync pad: driven`:

| | shown a picture | spread |
|---|---|---|
| as committed | **14 of 20** | 15.1..28.3 s, median 22.2 |
| reference clock moved last | **0 of 5** | — |
| `applyRegisters` moved ahead of the clock only | **3 of 6** | 24.2..27.4 s |

`vga` was 20 of 20 at median 6.3 s in every one of them, so the runs are
comparable and the tool, host and network are not what moved.

## Why moving the clock last destroys the leg

The sync processor counts in ADC clocks, so `resetSyncProcessor()` has to run
after the clock it counts through is in force. Moved last, the clock changes
under a processor that was reset immediately before it, and detection's first
pass — about 300 ms later — finds no hsync at all. Console across one such leg:

```
 0.00  input selected: ypbpr, reference divider 1400, applied +65ms
 0.25  source absent: 0 lines, 0 samples against divider 1400
 0.30  evt,377369,det hsact,0
 0.60  DETECT: 455ms, syncFound 0
 0.76  LOWPOWER: entered at t=377816ms (no sync found)
 1.60  evt,378678,det hsact,1
 1.61  DETECT: 23ms, syncFound 2
```

`hsact` is 0 on the first pass and 1 on the second, a second later, so the
selection answers a source that is arriving by entering low power. Nothing then
happens until the ladder's first act at ten seconds.

## What the refutation does not reach

It says nothing about whether a PLL restart after the mux would help — only that
**moving the existing one there does not**, and that the sequence as committed
is the best of the three measured. A restart *added* after `applyRegisters()`,
leaving the reference clock early so it has settled before detection runs, is
untried.

It also leaves the ladder's own asymmetry standing, which is the lever the
handover record calls unexploited: `/sampleclock` resets the video blocks, the
sync processor and the memory bus and *then* restarts the PLL, clearing a stall
in about two seconds, where the ladder restarts the PLL at ten seconds
(`Reconfigure`) and resets the blocks at twenty (`ResetBlocks`) — the opposite
order, on separate rungs, so the combination that works is unreachable.

## The baseline this was scored against had moved

The 14 of 20 above is not what the previous twenty-leg run of the same commit
recorded. On `a6b797373` the figures were 20 of 20 shown, 4 of 20 inside ten
seconds, 6.8..25.7 s, median 15.5. Re-flashed and re-measured, the same commit
gave 14 of 20, 0 of 20 inside ten seconds, median 22.2 — while `vga` reproduced
to the same median 6.3 s. `known-issues.md` already records `ypbpr` legs at
24.1..25.4 s with a never among six, so the later run is the one in family and
the 20 of 20 is the outlier.

**So a `ypbpr` build comparison needs its control measured in the same session.**
A seven-variant bisection of `reconfigureForSource()` scored against a remembered
baseline would be comparing each variant against noise, which is the failure
`acquisition_legs.py`'s own docstring records from four variants and three
withdrawn conclusions.
