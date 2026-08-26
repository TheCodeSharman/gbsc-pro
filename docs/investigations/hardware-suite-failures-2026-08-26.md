# Eight hardware tests fail, and they are three different faults

`pytest --host --source` fails eight tests on a unit that shows a correct
picture. The same eight fail with and without the sync-type commit that was
under test when they were found, so nothing recent moved them.

A ninth, `test_the_reset_control_returns_the_framing_to_default`, fails in a full
run and passes in isolation, unchanged by the commit under test. It is
order-dependent and is not covered here.

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

## The engine is deterministic, and a preset load still leaves values it did not compute

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
the source measurement holds at that moment, and never again. The disagreement is
the difference between an unsettled measurement and a settled one, and the run-to-
run variation is how unsettled it happened to be.

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

The preset-load finding is the one with a firmware behaviour behind it. The
question it leaves is whether a mode change should re-solve once the source
measurement settles, and what currently decides that it does not.
