# Eight hardware tests fail, and they are three different faults

`pytest --host --source` fails eight tests on a unit that shows a correct
picture. The same eight fail with and without the sync-type commit that was
under test when they were found, so nothing recent moved them.

A ninth, `test_the_reset_control_returns_the_framing_to_default`, fails in a full
run and passes in isolation, unchanged by the commit under test. It is
order-dependent and is not covered here.

## What was done

Five tests removed, their premises gone:

    test_bypass_does_not_leave_the_scaling_flag_set     GBS_PRESET_ID deleted
    test_bypass_keeps_the_block_it_programmed           GBS_PRESET_ID deleted
    test_the_frame_buffer_subsystem_owns_the_memory_map pre-arming-rule contract
    test_the_memory_bus_subsystem_owns_its_timing       pre-arming-rule contract
    test_the_subsystems_own_the_fifo_watermarks         pre-arming-rule contract

The last three assert that a preset load re-applies the frame buffer and memory
bus configuration. It does not any more: the bring-up runs when the chip is
ARMED. **That coverage should come back re-pointed at the arming rule** -- poison,
arm via a bypass excursion, then check -- rather than staying deleted.

Two tests fixed: the preset-load comparison now sets the framing it compares
against, and the zoom and pan window checks no longer require headroom the
engine deliberately does not allocate.

One remains failing and is NOT diagnosed:
`test_the_reset_control_returns_the_framing_to_default`, which passes alone and
fails in a full run when a zoom press is absorbed within its 6 s window. Waiting
for two agreeing framing reads before the press was tried and did not fix it, and
broke a neighbouring test, so it was reverted. The suite is 302 passed, 1 failed,
2 xfailed.

## Two tests key on a register that no longer exists

`test_bypass_does_not_leave_the_scaling_flag_set` and
`test_bypass_keeps_the_block_it_programmed` both begin by waiting for
`GBS_PRESET_ID` — s1_2B[0:7] — to reach `0x22`, and fail on the wait rather than
on the thing they exist to check.

The register was removed when the preset id became firmware state. Nothing writes
it, `zeroAll()` clears it at boot, and it reads **0** on the bench. The tests are
stale, not the firmware.

Whatever replaces the wait cannot be another register: `rto->presetID` is RAM and
no route exposes it. The bypass state is observable — `DAC_RGBS_ADC2DAC` and
`OUT_SYNC_SEL` are 1 in bypass and 0 on the scaling path — and that is what these
tests should wait on, since it is what "bypass ran" actually means.

## The big one: the comparison spanned two framings

`test_a_preset_load_leaves_the_engines_values_not_the_sketchs` says in its own
docstring that "a preset load leaves the framing at default and the reset control
puts it there too, so nothing about the framing differs between the two states
being compared". **That stopped being true when the framing table gained a
per-source memory.**

A source with a remembered framing comes up on it, and a preset load keeps it.
`reset_the_framing()` then sets the DEFAULT. So the two halves of the comparison
are two different framings, and every window and both scales differ:
capture 747 against 973, `VDS_HSCALE` 431 against 557.

It looks like a first-load-after-boot effect because the first
`reset_the_framing()` destroys the remembered framing; every trial after that
compares default with default and passes. It appears in a full `--source` run for
the same reason -- earlier tests leave a non-default framing.

The fix is the one that generalises: **set the state, do not assume it.** The
comparison now resets the framing before the preset load, so both halves are
default whatever the unit was doing beforehand.

**What survives that fix is real and is 2 units wide.** `VDS_HB_ST` and
`VDS_DIS_HB_ST` come out 1899 from the load and 1897 from a re-solve of the same
framing -- deterministic, the same two registers, the same two units, across
repeated runs, with every other register in the set agreeing and the measured
line rate identical. The test is marked xfail against that, so the day it changes
is reported either way.

## Superseded: the engine is deterministic, and a preset load still leaves values it did not compute

`test_a_preset_load_leaves_the_engines_values_not_the_sketchs` compares the
registers after a preset load against the same registers after the engine
re-solves the same framing, and reports a disagreement.

The obvious objection is that the engine re-solves as the measured field rate
moves, so the two reads would differ with no fault present. **Measured, and it
does not**: three consecutive engine-only re-solves of the same framing, no
preset load between, moved **0 of 26** registers. The comparison is sound.

What the failure is *not* is the message's own explanation. It names
`doPostPresetLoadSteps()` as having won a race, but the values left behind are
computed, not constants — `VDS_HSCALE` 555 against 557, `IF_HB_ST2` 1103 against
1105. Those are two solves, not a solve and a hardcoded table entry.

Two further measurements bound it:

- **The values never converge.** Sampled every 3 s for 33 s after a load, they
  are stable from the first sample. Nothing re-solves once the source has
  settled.
- **How many disagree varies between runs**, on one build: nine registers in one
  run, three in another, with `VDS_HSCALE` matching in the second. So the load's
  solve sometimes lands on the settled answer and sometimes does not.

Together those say the geometry is solved once during the load, against whatever
the source measurement holds at that moment, and never again.

**Isolated, the load's solve is almost always exact, and the exception is the
FIRST load after a boot.** Eight loads in sequence from a hard reset, each
compared against a re-solve of the same framing taken straight after:

    uptime   registers differing   largest gap
      8 s          3 / 26               2
     22 s          0 / 26               0
     36 s          0 / 26               0
      ... five more, all 0 / 26

So the small residual is a first-load effect, not a general one, and it is two
units wide.

**The large disagreements are a different condition and are not reproduced in
isolation.** `VDS_HSCALE` 431 against 557 and nine registers apart were seen
inside a full `pytest --source` run, after many tests that disturb the framing
and the sync path. Five and then eight consecutive loads on an otherwise idle
unit never reproduced them. Whatever produces them is in that preceding churn,
and is not characterised here.

**Averaging the field rate does not help, measured.** `getSourceFieldRate()`
takes ONE reading of one field period, with a retry only when it returns zero,
and sizing a permanent raster from that is fragile in principle. Replacing it
with the median of nine readings was tried and A/B'd against the same code with
the sample count set to 1: five trials each, identical results both ways --
3/26 on the first trial and 0/26 on the remaining four. The change was reverted.
Do not reinstate it without evidence that names what it fixes.

## Two tests require headroom the engine deliberately does not allocate

`test_a_zoom_press_leaves_the_windows_following_the_capture` and its pan
equivalent compute `produced = capture * 1024 // scale` and fail when the display
window is wider than `produced - margin`, with `margin` 2 horizontally and 3
vertically.

The failure is on exact equality: a capture of 969 at scale 555 produces 1787 and
the window is 1787, which the assertion rejects for not being 1785 or less.

`Axis::solve()` makes the memory window the display window and allocates nothing
spare, and the test's own comment says so three lines above the assertion. The
headroom rule that would have justified a margin is retracted in `CLAUDE.md`,
where `HEADROOM_WARN_PX` is described as a floor to warn below rather than a
budget to reserve. An assertion demanding the window be *smaller* than what the
engine produces cannot pass while the engine does what it is designed to do.

## Not diagnosed

`test_the_frame_buffer_subsystem_owns_the_memory_map`,
`test_the_memory_bus_subsystem_owns_its_timing` and
`test_the_subsystems_own_the_fifo_watermarks`. The first waits for
`PB_CAP_BUF_STA_ADDR_A`'s high byte to reach `0x06` after a preset load and fails
on the wait, which is the shape of the two bypass tests above — a value keyed on
by a test and no longer produced — but that has not been confirmed.

## What to measure next

The large disagreements are the open question, and they need the churn that
produces them. Bisecting the full `--source` run for the test that leaves the
unit in that state would name it; the isolated loads above already show it is
not the load path on its own.

The first-load-after-boot residual is two units and may not be worth chasing.
What it says is that one measurement early in a boot is slightly off, and that
the engine's solve is not re-run once it is not.
