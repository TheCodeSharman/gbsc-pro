# A YPbPr detection that succeeds on its first pass skips the only preparation

An input change to `ypbpr` sometimes leaves the sync processor reporting
`STATUS_SYNC_PROC_VTOTAL` 97 with every configuration register correct, and the
source takes 25..32 s to acquire instead of 5..7 s. What decides it is whether
detection's **first** pass claims the source.

The preparation a new source needs -- the reference sampling clock and the block
resets -- is reached only through detection's FAILURE branch.
`inputAndSyncDetect()` seeing `syncFound 0` with no signal present calls
`goLowPowerWithInputDetection()`, and that calls `setResetParameters()`, which
installs `Adc::BringUpDivider` through `Adc::applyResetParameters()`. A first
pass that returns 2 skips all of it, and the chip then measures the new source
through the previous source's ADC clock.

**So the healthy path depends on detection failing.** Nothing on the selection
path prepares the chip.

## The correlation is exact over 32 acquisitions

Measured on the Wii at 480p over `ypbpr`, cycling `/input` against the RISC PC on
`vga` at 800x600@60, across two runs on one build.

| | first `DETECT` | low-power passes | acquired in | n |
|---|---|---|---|---|
| healthy | 441..464 ms, `syncFound 0` | 1 | 4.8..7.1 s | 27 |
| wedged | 96..441 ms, **`syncFound 2`** | 0 | 25.2..32.1 s | 5 |

No exceptions either way. **The determinant is `syncFound`, not how long the pass
took**: one wedge claimed the source after 441 ms, longer than several healthy
first passes.

The rate is erratic -- 1 in 20, then 4 in 12, then 0 in 12 -- so absence over a
dozen cycles is not evidence that a change fixed it. Pooled, 5 in 44.

## What the wedged chip holds

Read by name through a wedge, against a healthy `ypbpr` acquisition read the same
way:

| | wedged | healthy `ypbpr` |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 97..105 | 524 |
| `STATUS_SYNC_PROC_HSACT` | 0 | 1 |
| `PLLAD_MD` / `STATUS_SYNC_PROC_HTOTAL` | 1438 / 1438, `vga`'s | 1448 / 1448 |
| `SP_CLAMP_MANUAL` | 1 | 0 |
| `SP_H_PROTECT` | 1 | 0 |
| `HPERIOD_IF` | 13..14 | 214 |
| `ADC_SOGCTRL` | 14 | 14 |

The clamp held and overflow protect on are the states detection sets as it works,
so the wedged chip is one left part-way through a sequence that concluded early.

**A baseline taken on `vga` does not serve.** `vga` is the RGBHV route, where
`SP_H_PROTECT` is 0 and the clamp is not held by `applySyncProcessorDynamic()`, so
both rows above read as faults against it and neither is one on that input.

## Two single-element repairs are refuted

Measured with `/freeze?on=1` holding `runSourceRecovery()` off, so nothing but the
write under test could act. Three wedges each.

| tried | measured |
|---|---|
| `SFTRST_SYNC_RSTZ` 0 -> 1 | **not enough.** `STATUS_MISC_PLLAD_LOCK` 0 -> 1 and `HPERIOD_IF` 14 -> 214, so the horizontal side does restart -- and `VTOTAL` stays 97 and the lock decays again |
| `PLLAD_LAT` rising edge | no change; the divider in force is already the one the register holds |
| neither (control) | still wedged, so it does not clear itself |

**The earlier claim that `SFTRST_SYNC_RSTZ` is what clears it does not survive
this.** That rested on re-selecting the same input, which also runs
`LoadDefault()` and re-applies the input registers, so it isolated nothing.

## The escalation cost, and why a reordering made it worse

A wedge is left alone for 15 s, because `VideoSourceAcquisition` holds the ladder
off a first acquisition for that long. The rungs then fire about ten per second;
`SyncRecovery::FullReset` sits at position 150 and lands roughly 10 s later. The
nine rungs before it change nothing. 15 + 10 is the 25..32 s measured.

The early rungs do move it, but only into a second state: divider 1448 with
`VTOTAL` 524 correct and `STATUS_SYNC_PROC_HTOTAL` pinned at 1704..1706 with the
lock bit 0 in every sample over 13 s. 1704 x 31468 Hz is about 53.6 MHz, a
free-running VCO. `SyncRecovery::RestartSamplingClock` -- the rung written for an
unlocked ADC PLL -- fires at position 60 and changes nothing there;
`FullReset` clears it in under a second, `1704 samples against divider 1448`
becoming `1448 against 1448`.

**And the reordering that made the fault three times more frequent now has an
explanation.** Resetting the block after the input registers with 200 ms for the
AV module lets the mux settle before detection runs, which makes a first-pass
claim MORE likely -- so the preparation ran less often.

## Why the obvious fix is not one line

The RGB branch already gates on a count: it waits `DetectCountWaitMs` for
`VideoSignal::countIsSource()` and reports not-found without one, which is what
`docs/investigations/a-boot-that-detects-too-early-can-never-recover.md` put
there. `countIsSource(97)` is false, so that gate would reject this state. The
YPbPr branch has no such gate and returns 2 on two status bits.

Adding it there runs into two things the tree already records:

- **The count cannot answer on that branch yet.** Nothing can be counted until a
  divider is sized for the arriving source, which happens after detection
  returns, so the gate would reject every first pass.
- **A rejection led nowhere.** `syncFound 0` with a signal present calls
  `SourceAbsence::undecided()`, which was a no-op, so the engine neither
  prepared the chip nor ended the run. That half is repaired below.

Rejecting every first pass is in fact the healthy sequence -- it is what the
27 healthy acquisitions do -- but it reaches the preparation only because
`signalPresent()` happens to read false. Resting a fix on that is resting it on
the same accident.

**So the repair is to prepare the chip on the selection path**, where the input
is known to have changed, rather than to make detection fail more reliably.
`Tv5725::Adc::installReferenceSamplingClock()` is what installs it, as a whole
PLL group. Installing the bare divider is separately refuted: `PLLAD_MD`
2506 written into a live wedge leaves the count at 97, and `IF_HSYNC_RST` cannot
hold 2506 so it is left describing another line.

## A double selection wedged it permanently, and that part is deterministic

Selecting `ypbpr` twice about 1.5 s apart, from a settled `vga` acquisition, left
the unit unable to see the source at all: **6 of 6**, `VTOTAL` 0, `HSACT` 0,
`PLLAD_MD` 2506, `state: absent` held for the full 90 s each attempt was given.
The reference divider being installed says the low-power pass did run;
`isInLowPowerMode` then guarded against another, and nothing else prepared the
chip. `/sc?~` recovered it in about 6 s. **This is the deterministic reproduction
the repair below is measured against.**

This is the absence run that has no end, with a reproduction it did not have
before. It is **not** the wedge above -- that one holds the previous source's
divider and counts 97, this one holds the reference divider and counts nothing.

## Method

`Tv5725::SamplingLog` at 25 ms, armed as one continuous tape from the process
holding the console, with `/input` driven against it so each change lands inside
the record. The tape's columns carry `STATUS_SYNC_PROC_HTOTAL` beside `PLLAD_MD`,
which is what separates an unlocked PLL from a stale divider; a point read over
HTTP cannot, and the lock bit alone cannot either, being a duty cycle.

A source is identified by `lineRateHz`. The solved capture height varies between
acquisitions of one source -- 525, 526 and 529 all measured on the Wii -- so a
harness keyed on it reports false stalls. A settle step must also check the rate
rather than `state: acquired`, or a stale acquisition of the other input is
accepted and the cycle never changes input at all.

## The repair

Two changes, each for one of the two defects above.

**A source is measured through a reference clock rather than the last one's.**
`Adc::installReferenceSamplingClock()` is the whole PLL group at
`Adc::BringUpDivider`, named for what it does and extracted from
`Adc::applyResetParameters()`, which was the only way to reach it.
`applyInputSelection()` calls it before it resets the sync processor, so the
block comes out of reset with a clock the arriving source can be counted
through, whether or not detection's first pass claims the source. The
preparation no longer depends on detection failing.

The healthy path is unchanged by it: the first pass still finds nothing after
about 455 ms, the low-power teardown still runs, and the second pass claims the
source in 24..28 ms. What changes is only the case where the first pass claims
it.

**An unclaimed signal advances the absence run.** `SourceAbsence::undecided()`
advances it exactly as `missed()` does, so detection finding nothing while a
signal IS reaching the sync processor reaches the teardown that repairs it,
rather than leaving a run that neither advances nor ends.

**A second guard made the teardown a one-shot, and it had to go with it.** The
call site ran the teardown only `if (rto->isInLowPowerMode == false)`, and that
flag is cleared only where detection claims a source, so a source never claimed
got one teardown and then nothing however long the run counted. Advancing the run
alone would have reached a threshold nobody acted on. So
`SourceAbsence::poweredDown()` re-arms the run once a teardown has been made and
the call site acts on every threshold. **Recovery is retried, never abandoned.**

## The guard is an ORDER, because neither duration nor the condition repeats

A timing threshold cannot gate a build on this. The wedge cost 25..32 s against a
healthy worst case of 12.6 s, the two distributions overlap, and an attempt at a
threshold both failed and skipped on a healthy unit inside two runs. Nor can the
condition be provoked: a first-pass claim was 1 in 20, then 4 in 12, then 0 in 12,
and the double selection above reproduces the OTHER fault instead.

What does repeat, on every selection and whichever way the first pass goes, is
where the preparation sits relative to detection. Before the repair it could only
follow detection's first report, being inside the failure branch; after it, it
always precedes it. So `applyInputSelection()` says what it did once the sequence
is complete:

    input selected: ypbpr, reference divider 2506, reset +18ms, registers +18ms, saved +24ms

and `test_input_selection_prepares.py` asserts that line arrives before the first
`DETECT`. **Said after the sequence rather than during it**, because a console
write inside the window perturbs what it is timing.

**The divider is compared against the one the previous source solved, never
against `Adc::BringUpDivider`.** A build that stopped installing the reference
clock still reports a divider on that line, and the one it reports is the last
source's -- so a test asserting the line names *a* divider passes through the
regression it exists to catch. Proven by taking the call out and flashing: the
selection reports `reference divider 1438`, `vga`'s own, and the guard fails
naming it. The ordering assertion passes there, correctly, which is why the two
are separate tests.

The offsets are the other half of what that line is for: nothing marked the mux
frame, the `SFTRST_SYNC_RSTZ` pulse or the `ADC_INPUT_SEL` write, so their
ordering against the block's counters could not be read at all. Measured, the
whole selection costs 24 ms of `loop()` -- 18 of them the reset's own delay, the
input registers free beside it, and 6 for the preferences write. The line itself
lands about 250 ms after the request, `/input` being queued for `loop()`.

## What the repair measures

| | before | after |
|---|---|---|
| double selection 1.5 s apart, from settled `vga` | 6 of 6 held `state: absent` for the full 90 s | **0 of 6**, acquired in 4.1..6.4 s |
| `ypbpr` acquisitions stalled | 5 of 44 | **0 of 70** |
| of those, first pass claimed the source | 5, all wedged, 25.2..32.1 s | **1, acquired in 1.6 s** |

**The one first-pass claim after the repair is the result that matters**, because
it is the condition itself rather than its absence: every one of the five before
wedged, and this one acquired in 1.6 s, faster than the 4.5..6.7 s the other 41
took. So the fault is not merely avoided, it is survived.

The condition also became rarer -- 1 in 70 against 5 in 44 -- which is the
mechanism working upstream of that: installing and latching the group leaves the
sync processor with nothing to report for a moment, so the first pass usually
fails and the preparation runs by design rather than by luck.

The double selection is the deterministic arm and is what carries the weight for
the absence run, the wedge's own rate being too erratic for absence over a few
dozen cycles to say much alone.
